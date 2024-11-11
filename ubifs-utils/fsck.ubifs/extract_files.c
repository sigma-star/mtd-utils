// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Huawei Technologies Co, Ltd.
 *
 * Authors: Zhihao Cheng <chengzhihao1@huawei.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/stat.h>

#include "linux_err.h"
#include "bitops.h"
#include "kmem.h"
#include "crc32.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "misc.h"
#include "fsck.ubifs.h"

static void parse_node_header(int lnum, int offs, int len,
			      unsigned long long sqnum,
			      struct scanned_node *header)
{
	header->exist = true;
	header->lnum = lnum;
	header->offs = offs;
	header->len = len;
	header->sqnum = sqnum;
}

static inline bool inode_can_be_encrypted(struct ubifs_info *c,
					  struct scanned_ino_node *ino_node)
{
	if (!c->encrypted)
		return false;

	if (ino_node->is_xattr)
		return false;

	/* Only regular files, directories, and symlinks can be encrypted. */
	if (S_ISREG(ino_node->mode) || S_ISDIR(ino_node->mode) ||
	    S_ISLNK(ino_node->mode))
		return true;

	return false;
}

/**
 * parse_ino_node - parse inode node and check it's validity.
 * @c: UBIFS file-system description object
 * @lnum: logical eraseblock number
 * @offs: the offset in LEB of the raw inode node
 * @node: raw node
 * @key: key of node scanned (if it has one)
 * @ino_node: node used to store raw inode information
 *
 * This function checks the raw inode information, and stores inode
 * information into @ino_node. Returns %true if the inode is valid,
 * otherwise %false is returned.
 */
bool parse_ino_node(struct ubifs_info *c, int lnum, int offs, void *node,
		    union ubifs_key *key, struct scanned_ino_node *ino_node)
{
	bool valid = false;
	int data_len, node_len;
	unsigned int flags;
	unsigned long long sqnum;
	struct ubifs_ch *ch = (struct ubifs_ch *)node;
	struct ubifs_ino_node *ino = (struct ubifs_ino_node *)node;
	ino_t inum = key_inum(c, key);

	if (!inum || inum > INUM_WATERMARK) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node(bad inum %lu) at %d:%d, in %s",
				 inum, lnum, offs, c->dev_name);
		else
			log_out(c, "bad inode node(bad inum %lu) at %d:%d",
				inum, lnum, offs);
		goto out;
	}

	if (ch->node_type != key_type(c, key)) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node %lu(inconsistent node type %d vs key_type %d) at %d:%d, in %s",
				 inum, ch->node_type, key_type(c, key),
				 lnum, offs, c->dev_name);
		else
			log_out(c, "bad inode node %lu(inconsistent node type %d vs key_type %d) at %d:%d",
				inum, ch->node_type, key_type(c, key),
				lnum, offs);
		goto out;
	}

	node_len = le32_to_cpu(ch->len);
	sqnum = le64_to_cpu(ch->sqnum);
	key_copy(c, key, &ino_node->key);
	flags = le32_to_cpu(ino->flags);
	data_len = le32_to_cpu(ino->data_len);
	ino_node->is_xattr = !!(flags & UBIFS_XATTR_FL) ? 1 : 0;
	ino_node->is_encrypted = !!(flags & UBIFS_CRYPT_FL) ? 1 : 0;
	ino_node->mode = le32_to_cpu(ino->mode);
	ino_node->nlink = le32_to_cpu(ino->nlink);
	ino_node->xcnt = le32_to_cpu(ino->xattr_cnt);
	ino_node->xsz = le32_to_cpu(ino->xattr_size);
	ino_node->xnms = le32_to_cpu(ino->xattr_names);
	ino_node->size = le64_to_cpu(ino->size);

	if (inum == UBIFS_ROOT_INO && !S_ISDIR(ino_node->mode)) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node %lu(root inode is not dir, tyoe %u) at %d:%d, in %s",
				 inum, ino_node->mode & S_IFMT, lnum, offs,
				 c->dev_name);
		else
			log_out(c, "bad inode node %lu(root inode is not dir, tyoe %u) at %d:%d",
				inum, ino_node->mode & S_IFMT, lnum, offs);
		goto out;
	}

	if (ino_node->size > c->max_inode_sz) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node %lu(size %llu is too large) at %d:%d, in %s",
				 inum, ino_node->size, lnum, offs, c->dev_name);
		else
			log_out(c, "bad inode node %lu(size %llu is too large) at %d:%d",
				inum, ino_node->size, lnum, offs);
		goto out;
	}

	if (le16_to_cpu(ino->compr_type) >= UBIFS_COMPR_TYPES_CNT) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node %lu(unknown compression type %d) at %d:%d, in %s",
				 inum, le16_to_cpu(ino->compr_type), lnum, offs,
				 c->dev_name);
		else
			log_out(c, "bad inode node %lu(unknown compression type %d) at %d:%d",
				inum, le16_to_cpu(ino->compr_type), lnum, offs);
		goto out;
	}

	if (ino_node->xnms + ino_node->xcnt > XATTR_LIST_MAX) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node %lu(too big xnames %u xcount %u) at %d:%d, in %s",
				 inum, ino_node->xnms, ino_node->xcnt,
				 lnum, offs, c->dev_name);
		else
			log_out(c, "bad inode node %lu(too big xnames %u xcount %u) at %d:%d",
				inum, ino_node->xnms, ino_node->xcnt,
				lnum, offs);
		goto out;
	}

	if (data_len < 0 || data_len > UBIFS_MAX_INO_DATA) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node %lu(invalid data len %d) at %d:%d, in %s",
				 inum, data_len, lnum, offs, c->dev_name);
		else
			log_out(c, "bad inode node %lu(invalid data len %d) at %d:%d",
				inum, data_len, lnum, offs);
		goto out;
	}

	if (UBIFS_INO_NODE_SZ + data_len != node_len) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node %lu(inconsistent data len %d vs node len %d) at %d:%d, in %s",
				 inum, data_len, node_len, lnum, offs, c->dev_name);
		else
			log_out(c, "bad inode node %lu(inconsistent data len %d vs node len %d) at %d:%d",
				inum, data_len, node_len, lnum, offs);
		goto out;
	}

	if (ino_node->is_xattr) {
		if (!S_ISREG(ino_node->mode)) {
			if (FSCK(c)->mode == REBUILD_MODE)
				dbg_fsck("bad inode node %lu(bad type %u for xattr) at %d:%d, in %s",
					 inum, ino_node->mode & S_IFMT,
					 lnum, offs, c->dev_name);
			else
				log_out(c, "bad inode node %lu(bad type %u for xattr) at %d:%d",
					inum, ino_node->mode & S_IFMT,
					lnum, offs);
			goto out;
		}
		if (data_len != ino_node->size) {
			if (FSCK(c)->mode == REBUILD_MODE)
				dbg_fsck("bad inode node %lu(inconsistent data_len %d vs size %llu for xattr) at %d:%d, in %s",
					 inum, data_len, ino_node->size,
					 lnum, offs, c->dev_name);
			else
				log_out(c, "bad inode node %lu(inconsistent data_len %d vs size %llu for xattr) at %d:%d",
					inum, data_len, ino_node->size,
					lnum, offs);
			goto out;
		}
		if (ino_node->xcnt || ino_node->xsz || ino_node->xnms) {
			if (FSCK(c)->mode == REBUILD_MODE)
				dbg_fsck("bad inode node %lu(non zero xattr count %u xattr size %u xattr names %u for xattr) at %d:%d, in %s",
					 inum, ino_node->xcnt, ino_node->xsz,
					 ino_node->xnms, lnum, offs, c->dev_name);
			else
				log_out(c, "bad inode node %lu(non zero xattr count %u xattr size %u xattr names %u for xattr) at %d:%d",
					inum, ino_node->xcnt, ino_node->xsz,
					ino_node->xnms, lnum, offs);
			goto out;
		}
	}

	switch (ino_node->mode & S_IFMT) {
	case S_IFREG:
		if (!ino_node->is_xattr && data_len != 0) {
			if (FSCK(c)->mode == REBUILD_MODE)
				dbg_fsck("bad inode node %lu(bad data len %d for reg file) at %d:%d, in %s",
					 inum, data_len, lnum, offs, c->dev_name);
			else
				log_out(c, "bad inode node %lu(bad data len %d for reg file) at %d:%d",
					inum, data_len, lnum, offs);
			goto out;
		}
		break;
	case S_IFDIR:
		if (data_len != 0) {
			if (FSCK(c)->mode == REBUILD_MODE)
				dbg_fsck("bad inode node %lu(bad data len %d for dir file) at %d:%d, in %s",
					 inum, data_len, lnum, offs, c->dev_name);
			else
				log_out(c, "bad inode node %lu(bad data len %d for dir file) at %d:%d",
					inum, data_len, lnum, offs);
			goto out;
		}
		break;
	case S_IFLNK:
		if (data_len == 0) {
			/*
			 * For encryption enabled or selinux enabled situation,
			 * uninitialized inode with xattrs could be written
			 * before ubifs_jnl_update(). If the dent node is
			 * written successfully but the initialized inode is
			 * not written, ubifs_iget() will get bad symlink inode
			 * with 'ui->data_len = 0'. Similar phenomenon can also
			 * occur for block/char dev creation.
			 * Just drop the inode node when above class of
			 * exceptions are found.
			 */
			if (FSCK(c)->mode == REBUILD_MODE)
				dbg_fsck("bad symlink inode node %lu(bad data len %d) at %d:%d, in %s",
					 inum, data_len, lnum, offs, c->dev_name);
			else
				log_out(c, "bad symlink inode node %lu(bad data len %d) at %d:%d",
					inum, data_len, lnum, offs);
			goto out;
		}
		break;
	case S_IFBLK:
		fallthrough;
	case S_IFCHR:
	{
		union ubifs_dev_desc *dev = (union ubifs_dev_desc *)ino->data;
		int sz_new = sizeof(dev->new), sz_huge = sizeof(dev->huge);

		if (data_len != sz_new && data_len != sz_huge) {
			if (FSCK(c)->mode == REBUILD_MODE)
				dbg_fsck("bad inode node %lu(bad data len %d for char/block file, expect %d or %d) at %d:%d, in %s",
					 inum, data_len, sz_new, sz_huge, lnum,
					 offs, c->dev_name);
			else
				log_out(c, "bad inode node %lu(bad data len %d for char/block file, expect %d or %d) at %d:%d",
					inum, data_len, sz_new, sz_huge, lnum,
					offs);
			goto out;
		}
		break;
	}
	case S_IFSOCK:
		fallthrough;
	case S_IFIFO:
		if (data_len != 0) {
			if (FSCK(c)->mode == REBUILD_MODE)
				dbg_fsck("bad inode node %lu(bad data len %d for fifo/sock file) at %d:%d, in %s",
					 inum, data_len, lnum, offs, c->dev_name);
			else
				log_out(c, "bad inode node %lu(bad data len %d for fifo/sock file) at %d:%d",
					inum, data_len, lnum, offs);
			goto out;
		}
		break;
	default:
		/* invalid file type. */
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node %lu(unknown type %u) at %d:%d, in %s",
				 inum, ino_node->mode & S_IFMT, lnum, offs, c->dev_name);
		else
			log_out(c, "bad inode node %lu(unknown type %u) at %d:%d",
				inum, ino_node->mode & S_IFMT, lnum, offs);
		goto out;
	}

	if (ino_node->is_encrypted && !inode_can_be_encrypted(c, ino_node)) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad inode node %lu(encrypted but cannot be encrypted, type %u, is_xattr %d, fs_encrypted %d) at %d:%d, in %s",
				 inum, ino_node->mode & S_IFMT, ino_node->is_xattr,
				 c->encrypted, lnum, offs, c->dev_name);
		else
			log_out(c, "bad inode node %lu(encrypted but cannot be encrypted, type %u, is_xattr %d, fs_encrypted %d) at %d:%d",
				inum, ino_node->mode & S_IFMT, ino_node->is_xattr,
				c->encrypted, lnum, offs);
		goto out;
	}

	valid = true;
	parse_node_header(lnum, offs, node_len, sqnum, &ino_node->header);

out:
	return valid;
}

/**
 * parse_dent_node - parse dentry node and check it's validity.
 * @c: UBIFS file-system description object
 * @lnum: logical eraseblock number
 * @offs: the offset in LEB of the raw inode node
 * @node: raw node
 * @key: key of node scanned (if it has one)
 * @dent_node: node used to store raw dentry information
 *
 * This function checks the raw dentry/(xattr entry) information, and
 * stores dentry/(xattr entry) information into @dent_node. Returns
 * %true if the entry is valid, otherwise %false is returned.
 */
bool parse_dent_node(struct ubifs_info *c, int lnum, int offs, void *node,
		     union ubifs_key *key, struct scanned_dent_node *dent_node)
{
	bool valid = false;
	int node_len, nlen;
	unsigned long long sqnum;
	struct ubifs_ch *ch = (struct ubifs_ch *)node;
	struct ubifs_dent_node *dent = (struct ubifs_dent_node *)node;
	int key_type = key_type_flash(c, dent->key);
	ino_t inum;

	nlen = le16_to_cpu(dent->nlen);
	node_len = le32_to_cpu(ch->len);
	sqnum = le64_to_cpu(ch->sqnum);
	inum = le64_to_cpu(dent->inum);

	if (node_len != nlen + UBIFS_DENT_NODE_SZ + 1 ||
	    dent->type >= UBIFS_ITYPES_CNT ||
	    nlen > UBIFS_MAX_NLEN || dent->name[nlen] != 0 ||
	    (key_type == UBIFS_XENT_KEY &&
	     strnlen((const char *)dent->name, nlen) != nlen) ||
	    inum > INUM_WATERMARK || key_type != ch->node_type) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad %s node(len %d nlen %d type %d inum %lu key_type %d node_type %d) at %d:%d, in %s",
				 ch->node_type == UBIFS_XENT_NODE ? "xattr entry" : "directory entry",
				 node_len, nlen, dent->type, inum, key_type,
				 ch->node_type, lnum, offs, c->dev_name);
		else
			log_out(c, "bad %s node(len %d nlen %d type %d inum %lu key_type %d node_type %d) at %d:%d",
				ch->node_type == UBIFS_XENT_NODE ? "xattr entry" : "directory entry",
				node_len, nlen, dent->type, inum, key_type,
				ch->node_type, lnum, offs);
		goto out;
	}

	key_copy(c, key, &dent_node->key);
	dent_node->can_be_found = false;
	dent_node->type = dent->type;
	dent_node->nlen = nlen;
	memcpy(dent_node->name, dent->name, nlen);
	dent_node->name[nlen] = '\0';
	dent_node->inum = inum;

	valid = true;
	parse_node_header(lnum, offs, node_len, sqnum, &dent_node->header);

out:
	return valid;
}

/**
 * parse_data_node - parse data node and check it's validity.
 * @c: UBIFS file-system description object
 * @lnum: logical eraseblock number
 * @offs: the offset in LEB of the raw data node
 * @node: raw node
 * @key: key of node scanned (if it has one)
 * @ino_node: node used to store raw data information
 *
 * This function checks the raw data node information, and stores
 * data node information into @data_node. Returns %true if the data
 * node is valid, otherwise %false is returned.
 */
bool parse_data_node(struct ubifs_info *c, int lnum, int offs, void *node,
		     union ubifs_key *key, struct scanned_data_node *data_node)
{
	bool valid = false;
	int node_len;
	unsigned long long sqnum;
	struct ubifs_ch *ch = (struct ubifs_ch *)node;
	struct ubifs_data_node *dn = (struct ubifs_data_node *)node;
	ino_t inum = key_inum(c, key);

	if (ch->node_type != key_type(c, key)) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad data node(inconsistent node type %d vs key_type %d) at %d:%d, in %s",
				 ch->node_type, key_type(c, key),
				 lnum, offs, c->dev_name);
		else
			log_out(c, "bad data node(inconsistent node type %d vs key_type %d) at %d:%d",
				ch->node_type, key_type(c, key), lnum, offs);
		goto out;
	}

	if (!inum || inum > INUM_WATERMARK) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad data node(bad inum %lu) at %d:%d, in %s",
				 inum, lnum, offs, c->dev_name);
		else
			log_out(c, "bad data node(bad inum %lu) at %d:%d",
				inum, lnum, offs);
		goto out;
	}

	node_len = le32_to_cpu(ch->len);
	sqnum = le64_to_cpu(ch->sqnum);
	key_copy(c, key, &data_node->key);
	data_node->size = le32_to_cpu(dn->size);

	if (!data_node->size || data_node->size > UBIFS_BLOCK_SIZE) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad data node(invalid size %u) at %d:%d, in %s",
				 data_node->size, lnum, offs, c->dev_name);
		else
			log_out(c, "bad data node(invalid size %u) at %d:%d",
				data_node->size, lnum, offs);
		goto out;
	}

	if (le16_to_cpu(dn->compr_type) >= UBIFS_COMPR_TYPES_CNT) {
		if (FSCK(c)->mode == REBUILD_MODE)
			dbg_fsck("bad data node(invalid compression type %d) at %d:%d, in %s",
				 le16_to_cpu(dn->compr_type), lnum, offs, c->dev_name);
		else
			log_out(c, "bad data node(invalid compression type %d) at %d:%d",
				le16_to_cpu(dn->compr_type), lnum, offs);
		goto out;
	}

	valid = true;
	parse_node_header(lnum, offs, node_len, sqnum, &data_node->header);

out:
	return valid;
}

/**
 * parse_trun_node - parse truncation node and check it's validity.
 * @c: UBIFS file-system description object
 * @lnum: logical eraseblock number
 * @offs: the offset in LEB of the raw truncation node
 * @node: raw node
 * @key: key of node scanned (if it has one)
 * @trun_node: node used to store raw truncation information
 *
 * This function checks the raw truncation information, and stores
 * truncation information into @trun_node. Returns %true if the
 * truncation is valid, otherwise %false is returned.
 */
bool parse_trun_node(struct ubifs_info *c, int lnum, int offs, void *node,
		     union ubifs_key *key, struct scanned_trun_node *trun_node)
{
	bool valid = false;
	int node_len;
	unsigned long long sqnum;
	struct ubifs_ch *ch = (struct ubifs_ch *)node;
	struct ubifs_trun_node *trun = (struct ubifs_trun_node *)node;
	loff_t old_size = le64_to_cpu(trun->old_size);
	loff_t new_size = le64_to_cpu(trun->new_size);
	ino_t inum = le32_to_cpu(trun->inum);

	if (!inum || inum > INUM_WATERMARK) {
		dbg_fsck("bad truncation node(bad inum %lu) at %d:%d, in %s",
			 inum, lnum, offs, c->dev_name);
		goto out;
	}

	node_len = le32_to_cpu(ch->len);
	sqnum = le64_to_cpu(ch->sqnum);
	trun_node->new_size = new_size;

	if (old_size < 0 || old_size > c->max_inode_sz ||
	    new_size < 0 || new_size > c->max_inode_sz ||
	    old_size <= new_size) {
		dbg_fsck("bad truncation node(new size %ld old size %ld inum %lu) at %d:%d, in %s",
			 new_size, old_size, inum, lnum, offs, c->dev_name);
		goto out;
	}

	trun_key_init(c, key, inum);
	valid = true;
	parse_node_header(lnum, offs, node_len, sqnum, &trun_node->header);

out:
	return valid;
}

/**
 * insert_file_dentry - insert dentry according to scanned dent node.
 * @file: file object
 * @n_dent: scanned dent node
 *
 * Insert file dentry information. Returns zero in case of success, a
 * negative error code in case of failure.
 */
static int insert_file_dentry(struct scanned_file *file,
			      struct scanned_dent_node *n_dent)
{
	struct scanned_dent_node *dent;
	struct rb_node **p, *parent = NULL;

	p = &file->dent_nodes.rb_node;
	while (*p) {
		parent = *p;
		dent = rb_entry(parent, struct scanned_dent_node, rb);
		if (n_dent->header.sqnum < dent->header.sqnum)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	dent = kmalloc(sizeof(struct scanned_dent_node), GFP_KERNEL);
	if (!dent)
		return -ENOMEM;

	*dent = *n_dent;
	rb_link_node(&dent->rb, parent, p);
	rb_insert_color(&dent->rb, &file->dent_nodes);

	return 0;
}

/**
 * update_file_data - insert/update data according to scanned data node.
 * @c: UBIFS file-system description object
 * @file: file object
 * @n_dn: scanned data node
 *
 * Insert or update file data information. Returns zero in case of success,
 * a negative error code in case of failure.
 */
static int update_file_data(struct ubifs_info *c, struct scanned_file *file,
			    struct scanned_data_node *n_dn)
{
	int cmp;
	struct scanned_data_node *dn, *o_dn = NULL;
	struct rb_node **p, *parent = NULL;

	p = &file->data_nodes.rb_node;
	while (*p) {
		parent = *p;
		dn = rb_entry(parent, struct scanned_data_node, rb);
		cmp = keys_cmp(c, &n_dn->key, &dn->key);
		if (cmp < 0) {
			p = &(*p)->rb_left;
		} else if (cmp > 0) {
			p = &(*p)->rb_right;
		} else {
			o_dn = dn;
			break;
		}
	}

	if (o_dn) {
		/* found data node with same block no. */
		if (o_dn->header.sqnum < n_dn->header.sqnum) {
			o_dn->header = n_dn->header;
			o_dn->size = n_dn->size;
		}

		return 0;
	}

	dn = kmalloc(sizeof(struct scanned_data_node), GFP_KERNEL);
	if (!dn)
		return -ENOMEM;

	*dn = *n_dn;
	INIT_LIST_HEAD(&dn->list);
	rb_link_node(&dn->rb, parent, p);
	rb_insert_color(&dn->rb, &file->data_nodes);

	return 0;
}

/**
 * update_file - update file information.
 * @c: UBIFS file-system description object
 * @file: file object
 * @sn: scanned node
 * @key_type: type of @sn
 *
 * Update inode/dent/truncation/data node information of @file. Returns
 * zero in case of success, a negative error code in case of failure.
 */
static int update_file(struct ubifs_info *c, struct scanned_file *file,
		       struct scanned_node *sn, int key_type)
{
	int err = 0;

	switch (key_type) {
	case UBIFS_INO_KEY:
	{
		struct scanned_ino_node *o_ino, *n_ino;

		o_ino = &file->ino;
		n_ino = (struct scanned_ino_node *)sn;
		if (o_ino->header.exist && o_ino->header.sqnum > sn->sqnum)
			goto out;

		*o_ino = *n_ino;
		break;
	}
	case UBIFS_DENT_KEY:
	case UBIFS_XENT_KEY:
	{
		struct scanned_dent_node *dent = (struct scanned_dent_node *)sn;

		dent->file = file;
		err = insert_file_dentry(file, dent);
		break;
	}
	case UBIFS_DATA_KEY:
	{
		struct scanned_data_node *dn = (struct scanned_data_node *)sn;

		err = update_file_data(c, file, dn);
		break;
	}
	case UBIFS_TRUN_KEY:
	{
		struct scanned_trun_node *o_trun, *n_trun;

		o_trun = &file->trun;
		n_trun = (struct scanned_trun_node *)sn;
		if (o_trun->header.exist && o_trun->header.sqnum > sn->sqnum)
			goto out;

		*o_trun = *n_trun;
		break;
	}
	default:
		err = -EINVAL;
		log_err(c, 0, "unknown key type %d", key_type);
	}

out:
	return err;
}

/**
 * insert_or_update_file - insert or update file according to scanned node.
 * @c: UBIFS file-system description object
 * @file_tree: tree of all scanned files
 * @sn: scanned node
 * @key_type: key type of @sn
 * @inum: inode number
 *
 * According to @sn, this function inserts file into the tree, or updates
 * file information if it already exists in the tree. Returns zero in case
 * of success, a negative error code in case of failure.
 */
int insert_or_update_file(struct ubifs_info *c, struct rb_root *file_tree,
			  struct scanned_node *sn, int key_type, ino_t inum)
{
	int err;
	struct scanned_file *file, *old_file = NULL;
	struct rb_node **p, *parent = NULL;

	p = &file_tree->rb_node;
	while (*p) {
		parent = *p;
		file = rb_entry(parent, struct scanned_file, rb);
		if (inum < file->inum) {
			p = &(*p)->rb_left;
		} else if (inum > file->inum) {
			p = &(*p)->rb_right;
		} else {
			old_file = file;
			break;
		}
	}
	if (old_file)
		return update_file(c, old_file, sn, key_type);

	file = kzalloc(sizeof(struct scanned_file), GFP_KERNEL);
	if (!file)
		return -ENOMEM;

	file->inum = inum;
	file->dent_nodes = RB_ROOT;
	file->data_nodes = RB_ROOT;
	file->xattr_files = RB_ROOT;
	INIT_LIST_HEAD(&file->list);
	err = update_file(c, file, sn, key_type);
	if (err) {
		kfree(file);
		return err;
	}
	rb_link_node(&file->rb, parent, p);
	rb_insert_color(&file->rb, file_tree);

	return 0;
}

/**
 * destroy_file_content - destroy scanned data/dentry nodes in give file.
 * @c: UBIFS file-system description object
 * @file: file object
 *
 * Destroy all data/dentry nodes and xattrs attached to @file.
 */
void destroy_file_content(struct ubifs_info *c, struct scanned_file *file)
{
	struct scanned_data_node *data_node;
	struct scanned_dent_node *dent_node;
	struct scanned_file *xattr_file;
	struct rb_node *this;

	this = rb_first(&file->data_nodes);
	while (this) {
		data_node = rb_entry(this, struct scanned_data_node, rb);
		this = rb_next(this);

		rb_erase(&data_node->rb, &file->data_nodes);
		kfree(data_node);
	}

	this = rb_first(&file->dent_nodes);
	while (this) {
		dent_node = rb_entry(this, struct scanned_dent_node, rb);
		this = rb_next(this);

		rb_erase(&dent_node->rb, &file->dent_nodes);
		kfree(dent_node);
	}

	this = rb_first(&file->xattr_files);
	while (this) {
		xattr_file = rb_entry(this, struct scanned_file, rb);
		this = rb_next(this);

		ubifs_assert(c, !rb_first(&xattr_file->xattr_files));
		destroy_file_content(c, xattr_file);
		rb_erase(&xattr_file->rb, &file->xattr_files);
		kfree(xattr_file);
	}
}

/**
 * destroy_file_tree - destroy files from a given tree.
 * @c: UBIFS file-system description object
 * @file_tree: tree of all scanned files
 *
 * Destroy scanned files from a given tree.
 */
void destroy_file_tree(struct ubifs_info *c, struct rb_root *file_tree)
{
	struct scanned_file *file;
	struct rb_node *this;

	this = rb_first(file_tree);
	while (this) {
		file = rb_entry(this, struct scanned_file, rb);
		this = rb_next(this);

		destroy_file_content(c, file);

		rb_erase(&file->rb, file_tree);
		kfree(file);
	}
}

/**
 * destroy_file_list - destroy files from a given list head.
 * @c: UBIFS file-system description object
 * @file_list: list of the scanned files
 *
 * Destroy scanned files from a given list.
 */
void destroy_file_list(struct ubifs_info *c, struct list_head *file_list)
{
	struct scanned_file *file;

	while (!list_empty(file_list)) {
		file = list_entry(file_list->next, struct scanned_file, list);

		destroy_file_content(c, file);
		list_del(&file->list);
		kfree(file);
	}
}

/**
 * lookup_file - lookup file according to inode number.
 * @file_tree: tree of all scanned files
 * @inum: inode number
 *
 * This function lookups target file from @file_tree according to @inum.
 */
struct scanned_file *lookup_file(struct rb_root *file_tree, ino_t inum)
{
	struct scanned_file *file;
	struct rb_node *p;

	p = file_tree->rb_node;
	while (p) {
		file = rb_entry(p, struct scanned_file, rb);

		if (inum < file->inum)
			p = p->rb_left;
		else if (inum > file->inum)
			p = p->rb_right;
		else
			return file;
	}

	return NULL;
}

static void handle_invalid_file(struct ubifs_info *c, int problem_type,
				struct scanned_file *file, void *priv)
{
	struct invalid_file_problem ifp = {
		.file = file,
		.priv = priv,
	};

	if (FSCK(c)->mode == REBUILD_MODE)
		return;

	fix_problem(c, problem_type, &ifp);
}

static int delete_node(struct ubifs_info *c, const union ubifs_key *key,
		       int lnum, int offs)
{
	int err;

	err = ubifs_tnc_remove_node(c, key, lnum, offs);
	if (err) {
		/* TNC traversing is finished, any TNC path is accessible */
		ubifs_assert(c, !get_failure_reason_callback(c));
	}

	return err;
}

static int delete_dent_nodes(struct ubifs_info *c, struct scanned_file *file,
			     int err)
{
	int ret = 0;
	struct rb_node *this = rb_first(&file->dent_nodes);
	struct scanned_dent_node *dent_node;

	while (this) {
		dent_node = rb_entry(this, struct scanned_dent_node, rb);
		this = rb_next(this);

		if (!err) {
			err = delete_node(c, &dent_node->key,
				dent_node->header.lnum, dent_node->header.offs);
			if (err)
				ret = ret ? ret : err;
		}

		rb_erase(&dent_node->rb, &file->dent_nodes);
		kfree(dent_node);
	}

	return ret;
}

int delete_file(struct ubifs_info *c, struct scanned_file *file)
{
	int err = 0, ret = 0;
	struct rb_node *this;
	struct scanned_file *xattr_file;
	struct scanned_data_node *data_node;

	if (file->ino.header.exist) {
		err = delete_node(c, &file->ino.key, file->ino.header.lnum,
				  file->ino.header.offs);
		if (err)
			ret = ret ? ret : err;
	}

	this = rb_first(&file->data_nodes);
	while (this) {
		data_node = rb_entry(this, struct scanned_data_node, rb);
		this = rb_next(this);

		if (!err) {
			err = delete_node(c, &data_node->key,
				data_node->header.lnum, data_node->header.offs);
			if (err)
				ret = ret ? ret : err;
		}

		rb_erase(&data_node->rb, &file->data_nodes);
		kfree(data_node);
	}

	err = delete_dent_nodes(c, file, err);
	if (err)
		ret = ret ? : err;

	this = rb_first(&file->xattr_files);
	while (this) {
		xattr_file = rb_entry(this, struct scanned_file, rb);
		this = rb_next(this);

		ubifs_assert(c, !rb_first(&xattr_file->xattr_files));
		err = delete_file(c, xattr_file);
		if (err)
			ret = ret ? ret : err;
		rb_erase(&xattr_file->rb, &file->xattr_files);
		kfree(xattr_file);
	}

	return ret;
}

/**
 * insert_xattr_file - insert xattr file into file's subtree.
 * @c: UBIFS file-system description object
 * @xattr_file: xattr file
 * @host_file: host file
 *
 * This inserts xattr file into its' host file's subtree.
 */
static void insert_xattr_file(struct ubifs_info *c,
			      struct scanned_file *xattr_file,
			      struct scanned_file *host_file)
{
	struct scanned_file *tmp_xattr_file;
	struct rb_node **p, *parent = NULL;

	p = &host_file->xattr_files.rb_node;
	while (*p) {
		parent = *p;
		tmp_xattr_file = rb_entry(parent, struct scanned_file, rb);
		if (xattr_file->inum < tmp_xattr_file->inum) {
			p = &(*p)->rb_left;
		} else if (xattr_file->inum > tmp_xattr_file->inum) {
			p = &(*p)->rb_right;
		} else {
			/* Impossible: Same xattr file is inserted twice. */
			ubifs_assert(c, 0);
		}
	}

	rb_link_node(&xattr_file->rb, parent, p);
	rb_insert_color(&xattr_file->rb, &host_file->xattr_files);
}

/**
 * file_is_valid - check whether the file is valid.
 * @c: UBIFS file-system description object
 * @file: file object
 * @file_tree: tree of all scanned files
 * @is_diconnected: reason of invalid file, whether the @file is disconnected
 *
 * This function checks whether given @file is valid, following checks will
 * be performed:
 * 1. All files have none-zero nlink inode, otherwise they are invalid.
 * 2. The file type comes from inode and dentries should be consistent,
 *    inconsistent dentries will be deleted.
 * 3. Directory type or xattr type files only have one dentry. Superfluous
 *    dentries with lower sequence number will be deleted.
 * 4. Non-regular file doesn't have data nodes. Data nodes are deleted for
 *    non-regular file.
 * 5. All files must have at least one dentries, except '/', '/' doesn't
 *    have dentries. Non '/' file is invalid if it doesn't have dentries.
 * 6. Xattr files should have host inode, and host inode cannot be a xattr,
 *    otherwise they are invalid.
 * 7. Encrypted files should have corresponding xattrs, otherwise they are
 *    invalid.
 * Xattr file will be inserted into corresponding host file's subtree.
 *
 * Returns %1 is @file is valid, %0 if @file is invalid, otherwise a negative
 * error code in case of failure.
 * Notice: All xattr files should be traversed before non-xattr files, because
 *         checking item 7 depends on it.
 */
int file_is_valid(struct ubifs_info *c, struct scanned_file *file,
		  struct rb_root *file_tree, int *is_diconnected)
{
	int type;
	struct rb_node *node;
	struct scanned_file *parent_file = NULL;
	struct scanned_dent_node *dent_node;
	struct scanned_data_node *data_node;
	LIST_HEAD(drop_list);

	dbg_fsck("check validation of file %lu, in %s", file->inum, c->dev_name);

	if (!file->ino.header.exist) {
		handle_invalid_file(c, FILE_HAS_NO_INODE, file, NULL);
		return 0;
	}

	if (!file->ino.nlink) {
		handle_invalid_file(c, FILE_HAS_0_NLINK_INODE, file, NULL);
		return 0;
	}

	type = ubifs_get_dent_type(file->ino.mode);

	/* Drop dentry nodes with inconsistent type. */
	for (node = rb_first(&file->dent_nodes); node; node = rb_next(node)) {
		int is_xattr = 0;

		dent_node = rb_entry(node, struct scanned_dent_node, rb);

		if (key_type(c, &dent_node->key) == UBIFS_XENT_KEY)
			is_xattr = 1;
		if (is_xattr != file->ino.is_xattr || type != dent_node->type)
			list_add(&dent_node->list, &drop_list);
	}

	while (!list_empty(&drop_list)) {
		dent_node = list_entry(drop_list.next, struct scanned_dent_node,
				       list);

		handle_invalid_file(c, FILE_HAS_INCONSIST_TYPE, file, dent_node);
		if (FSCK(c)->mode != REBUILD_MODE) {
			int err = delete_node(c, &dent_node->key,
				dent_node->header.lnum, dent_node->header.offs);
			if (err)
				return err;
		}

		list_del(&dent_node->list);
		rb_erase(&dent_node->rb, &file->dent_nodes);
		kfree(dent_node);
	}

	if (type != UBIFS_ITYPE_DIR && !file->ino.is_xattr)
		goto check_data_nodes;

	/* Make sure that directory/xattr type files only have one dentry. */
	node = rb_first(&file->dent_nodes);
	while (node) {
		dent_node = rb_entry(node, struct scanned_dent_node, rb);
		node = rb_next(node);
		if (!node)
			break;

		handle_invalid_file(c, FILE_HAS_TOO_MANY_DENT, file, dent_node);
		if (FSCK(c)->mode != REBUILD_MODE) {
			int err = delete_node(c, &dent_node->key,
				dent_node->header.lnum, dent_node->header.offs);
			if (err)
				return err;
		}

		rb_erase(&dent_node->rb, &file->dent_nodes);
		kfree(dent_node);
	}

check_data_nodes:
	if (type == UBIFS_ITYPE_REG && !file->ino.is_xattr)
		goto check_dent_node;

	/* Make sure that non regular type files not have data/trun nodes. */
	file->trun.header.exist = 0;
	node = rb_first(&file->data_nodes);
	while (node) {
		data_node = rb_entry(node, struct scanned_data_node, rb);
		node = rb_next(node);

		handle_invalid_file(c, FILE_SHOULDNT_HAVE_DATA, file, data_node);
		if (FSCK(c)->mode != REBUILD_MODE) {
			int err = delete_node(c, &data_node->key,
				data_node->header.lnum, data_node->header.offs);
			if (err)
				return err;
		}

		rb_erase(&data_node->rb, &file->data_nodes);
		kfree(data_node);
	}

check_dent_node:
	if (rb_first(&file->dent_nodes)) {
		if (file->inum == UBIFS_ROOT_INO) {
			/* '/' has no dentries. */
			handle_invalid_file(c, FILE_ROOT_HAS_DENT, file,
					    rb_entry(rb_first(&file->dent_nodes),
						struct scanned_dent_node, rb));
			return 0;
		}

		node = rb_first(&file->dent_nodes);
		dent_node = rb_entry(node, struct scanned_dent_node, rb);
		parent_file = lookup_file(file_tree, key_inum(c, &dent_node->key));
	} else {
		/* Non-root files must have dentries. */
		if (file->inum != UBIFS_ROOT_INO) {
			if (type == UBIFS_ITYPE_REG && !file->ino.is_xattr) {
				handle_invalid_file(c, FILE_IS_DISCONNECTED,
						    file, NULL);
				if (is_diconnected)
					*is_diconnected = 1;
			} else {
				handle_invalid_file(c, FILE_HAS_NO_DENT,
						    file, NULL);
			}
			return 0;
		}
	}

	if (file->ino.is_xattr) {
		if (!parent_file) {
			/* Host inode is not found. */
			handle_invalid_file(c, XATTR_HAS_NO_HOST, file, NULL);
			return 0;
		}
		if (parent_file->ino.is_xattr) {
			/* Host cannot be a xattr file. */
			handle_invalid_file(c, XATTR_HAS_WRONG_HOST, file, parent_file);
			return 0;
		}

		insert_xattr_file(c, file, parent_file);
		if (parent_file->ino.is_encrypted) {
			int nlen = min(dent_node->nlen,
				   strlen(UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT));

			if (!strncmp(dent_node->name,
				     UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT, nlen))
				parent_file->has_encrypted_info = true;
		}
	} else {
		if (parent_file && !S_ISDIR(parent_file->ino.mode)) {
			/* Parent file should be directory. */
			if (type == UBIFS_ITYPE_REG) {
				handle_invalid_file(c, FILE_IS_DISCONNECTED,
						    file, NULL);
				if (FSCK(c)->mode != REBUILD_MODE) {
					/* Delete dentries for the disconnected file. */
					int err = delete_dent_nodes(c, file, 0);
					if (err)
						return err;
				}
				if (is_diconnected)
					*is_diconnected = 1;
			}
			return 0;
		}

		/*
		 * Since xattr files are checked in first round, so all
		 * non-xattr files's @has_encrypted_info fields have been
		 * initialized.
		 */
		if (file->ino.is_encrypted && !file->has_encrypted_info) {
			handle_invalid_file(c, FILE_HAS_NO_ENCRYPT, file, NULL);
			return 0;
		}
	}

	return 1;
}

static bool dentry_is_reachable(struct ubifs_info *c,
				struct scanned_dent_node *dent_node,
				struct list_head *path_list,
				struct rb_root *file_tree)
{
	struct scanned_file *parent_file = NULL;
	struct scanned_dent_node *dn, *parent_dent;
	struct rb_node *p;

	/* Check whether the path is cyclical. */
	list_for_each_entry(dn, path_list, list) {
		if (dn == dent_node)
			return false;
	}

	/* Quick path, dentry has already been checked as reachable. */
	if (dent_node->can_be_found)
		return true;

	dent_node->can_be_found = true;
	list_add(&dent_node->list, path_list);

	parent_file = lookup_file(file_tree, key_inum(c, &dent_node->key));
	/* Parent dentry is not found, unreachable. */
	if (!parent_file)
		return false;

	/* Parent dentry is '/', reachable. */
	if (parent_file->inum == UBIFS_ROOT_INO)
		return true;

	p = rb_first(&parent_file->dent_nodes);
	if (!p)
		return false;
	parent_dent = rb_entry(p, struct scanned_dent_node, rb);

	return dentry_is_reachable(c, parent_dent, path_list, file_tree);
}

/**
 * file_is_reachable - whether the file can be found from '/'.
 * @c: UBIFS file-system description object
 * @file: file object
 * @file_tree: tree of all scanned files
 *
 * This function iterates all directory entries in given @file and checks
 * whether each dentry is reachable. All unreachable directory entries will
 * be removed.
 */
bool file_is_reachable(struct ubifs_info *c, struct scanned_file *file,
		       struct rb_root *file_tree)
{
	struct rb_node *node;
	struct scanned_dent_node *dent_node;

	if (file->inum == UBIFS_ROOT_INO)
		goto reachable;

retry:
	for (node = rb_first(&file->dent_nodes); node; node = rb_next(node)) {
		LIST_HEAD(path_list);

		dent_node = rb_entry(node, struct scanned_dent_node, rb);

		if (dentry_is_reachable(c, dent_node, &path_list, file_tree))
			continue;

		while (!list_empty(&path_list)) {
			dent_node = list_entry(path_list.next,
					       struct scanned_dent_node, list);

			handle_invalid_file(c, DENTRY_IS_UNREACHABLE,
					    dent_node->file, dent_node);
			if (FSCK(c)->mode != REBUILD_MODE) {
				int err = delete_node(c, &dent_node->key,
						      dent_node->header.lnum,
						      dent_node->header.offs);
				if (err)
					return err;
			}
			dbg_fsck("remove unreachable dentry %s, in %s",
				 c->encrypted && !file->ino.is_xattr ?
				 "<encrypted>" : dent_node->name, c->dev_name);
			list_del(&dent_node->list);
			rb_erase(&dent_node->rb, &dent_node->file->dent_nodes);
			kfree(dent_node);
		}

		/* Since dentry node is removed from rb-tree, rescan rb-tree. */
		goto retry;
	}

	if (!rb_first(&file->dent_nodes)) {
		if (S_ISREG(file->ino.mode))
			handle_invalid_file(c, FILE_IS_DISCONNECTED, file, NULL);
		else
			handle_invalid_file(c, FILE_HAS_NO_DENT, file, NULL);
		dbg_fsck("file %lu is unreachable, in %s", file->inum, c->dev_name);
		return false;
	}

reachable:
	dbg_fsck("file %lu is reachable, in %s", file->inum, c->dev_name);
	return true;
}

/**
 * calculate_file_info - calculate the information of file
 * @c: UBIFS file-system description object
 * @file: file object
 * @file_tree: tree of all scanned files
 *
 * This function calculates file information according to dentry nodes,
 * data nodes and truncation node. The calculated informaion will be used
 * to correct inode node.
 */
static int calculate_file_info(struct ubifs_info *c, struct scanned_file *file,
			       struct rb_root *file_tree)
{
	int nlink = 0;
	bool corrupted_truncation = false;
	unsigned long long ino_sqnum, trun_size = 0, new_size = 0, trun_sqnum = 0;
	struct rb_node *node;
	struct scanned_file *parent_file, *xattr_file;
	struct scanned_dent_node *dent_node;
	struct scanned_data_node *data_node;
	LIST_HEAD(drop_list);

	for (node = rb_first(&file->xattr_files); node; node = rb_next(node)) {
		xattr_file = rb_entry(node, struct scanned_file, rb);
		dent_node = rb_entry(rb_first(&xattr_file->dent_nodes),
				     struct scanned_dent_node, rb);

		ubifs_assert(c, xattr_file->ino.is_xattr);
		ubifs_assert(c, !rb_first(&xattr_file->xattr_files));
		xattr_file->calc_nlink = 1;
		xattr_file->calc_size = xattr_file->ino.size;

		file->calc_xcnt += 1;
		file->calc_xsz += CALC_DENT_SIZE(dent_node->nlen);
		file->calc_xsz += CALC_XATTR_BYTES(xattr_file->ino.size);
		file->calc_xnms += dent_node->nlen;
	}

	if (file->inum == UBIFS_ROOT_INO) {
		file->calc_nlink += 2;
		file->calc_size += UBIFS_INO_NODE_SZ;
		return 0;
	}

	if (S_ISDIR(file->ino.mode)) {
		file->calc_nlink += 2;
		file->calc_size += UBIFS_INO_NODE_SZ;

		dent_node = rb_entry(rb_first(&file->dent_nodes),
				     struct scanned_dent_node, rb);
		parent_file = lookup_file(file_tree, key_inum(c, &dent_node->key));
		if (!parent_file) {
			ubifs_assert(c, 0);
			return 0;
		}
		parent_file->calc_nlink += 1;
		parent_file->calc_size += CALC_DENT_SIZE(dent_node->nlen);
		return 0;
	}

	for (node = rb_first(&file->dent_nodes); node; node = rb_next(node)) {
		nlink++;

		dent_node = rb_entry(node, struct scanned_dent_node, rb);

		parent_file = lookup_file(file_tree, key_inum(c, &dent_node->key));
		if (!parent_file) {
			ubifs_assert(c, 0);
			return 0;
		}
		parent_file->calc_size += CALC_DENT_SIZE(dent_node->nlen);
	}
	file->calc_nlink = nlink;

	if (!S_ISREG(file->ino.mode)) {
		/* No need to verify i_size for symlink/sock/block/char/fifo. */
		file->calc_size = file->ino.size;
		return 0;
	}

	/*
	 * Process i_size and data content, following situations should
	 * be considered:
	 * 1. Sequential writing or overwriting, i_size should be
	 *    max(i_size, data node size), pick larger sqnum one from
	 *    data nodes with same block index.
	 * 2. Mixed truncation and writing, i_size depends on the latest
	 *    truncation node or inode node or last data node, pick data
	 *    nodes which are not truncated.
	 * 3. Setting bigger i_size attr, pick inode size or biggest
	 *    i_size calculated by data nodes.
	 */
	if (file->trun.header.exist) {
		trun_size = file->trun.new_size;
		trun_sqnum = file->trun.header.sqnum;
	}
	ino_sqnum = file->ino.header.sqnum;
	for (node = rb_first(&file->data_nodes); node; node = rb_next(node)) {
		unsigned long long d_sz, d_sqnum;
		unsigned int block_no;

		data_node = rb_entry(node, struct scanned_data_node, rb);

		d_sqnum = data_node->header.sqnum;
		block_no = key_block(c, &data_node->key);
		d_sz = data_node->size + block_no * UBIFS_BLOCK_SIZE;
		if ((trun_sqnum > d_sqnum && trun_size < d_sz) ||
		    (ino_sqnum > d_sqnum && file->ino.size < d_sz)) {
			/*
			 * The truncated data nodes are not gced after
			 * truncating, just remove them.
			 */
			list_add(&data_node->list, &drop_list);
		} else {
			new_size = max_t(unsigned long long, new_size, d_sz);
		}
	}
	/*
	 * Truncation node is written successful, but inode node is not. It
	 * won't happen because inode node is written before truncation node
	 * according to ubifs_jnl_truncate(), unless only inode is corrupted.
	 * In this case, data nodes could have been removed in history mounting
	 * recovery, so i_size needs to be updated.
	 */
	if (trun_sqnum > ino_sqnum && trun_size < file->ino.size) {
		if (trun_size < new_size) {
			corrupted_truncation = true;
			/*
			 * Appendant writing after truncation and newest inode
			 * is not fell on disk.
			 */
			goto update_isize;
		}

		/*
		 * Overwriting happens after truncation and newest inode is
		 * not fell on disk.
		 */
		file->calc_size = trun_size;
		goto drop_data;
	}
update_isize:
	/*
	 * The file cannot use 'new_size' directly when the file may have ever
	 * been set i_size. For example:
	 *  1. echo 123 > file		# i_size = 4
	 *  2. truncate -s 100 file	# i_size = 100
	 * After scanning, new_size is 4. Apperantly the size of 'file' should
	 * be 100. So, the calculated new_size according to data nodes should
	 * only be used for extending i_size, like ubifs_recover_size() does.
	 */
	if (new_size > file->ino.size || corrupted_truncation)
		file->calc_size = new_size;
	else
		file->calc_size = file->ino.size;

drop_data:
	while (!list_empty(&drop_list)) {
		data_node = list_entry(drop_list.next, struct scanned_data_node,
				       list);

		if (FSCK(c)->mode != REBUILD_MODE) {
			/*
			 * Don't ask, inconsistent file correcting will be
			 * asked in function correct_file_info().
			 */
			int err = delete_node(c, &data_node->key,
				data_node->header.lnum, data_node->header.offs);
			if (err)
				return err;
		}
		list_del(&data_node->list);
		rb_erase(&data_node->rb, &file->data_nodes);
		kfree(data_node);
	}

	return 0;
}

/**
 * correct_file_info - correct the information of file
 * @c: UBIFS file-system description object
 * @file: file object
 *
 * This function corrects file information according to calculated fields,
 * eg. 'calc_nlink', 'calc_xcnt', 'calc_xsz', 'calc_xnms' and 'calc_size'.
 * Corrected inode node will be re-written.
 */
static int correct_file_info(struct ubifs_info *c, struct scanned_file *file)
{
	uint32_t crc;
	int err, lnum, len;
	struct rb_node *node;
	struct ubifs_ino_node *ino;
	struct scanned_file *xattr_file;

	for (node = rb_first(&file->xattr_files); node; node = rb_next(node)) {
		xattr_file = rb_entry(node, struct scanned_file, rb);

		err = correct_file_info(c, xattr_file);
		if (err)
			return err;
	}

	if (file->calc_nlink == file->ino.nlink &&
	    file->calc_xcnt == file->ino.xcnt &&
	    file->calc_xsz == file->ino.xsz &&
	    file->calc_xnms == file->ino.xnms &&
	    file->calc_size == file->ino.size)
		return 0;

	handle_invalid_file(c, FILE_IS_INCONSISTENT, file, NULL);
	lnum = file->ino.header.lnum;
	dbg_fsck("correct file(inum:%lu type:%s), nlink %u->%u, xattr cnt %u->%u, xattr size %u->%u, xattr names %u->%u, size %llu->%llu, at %d:%d, in %s",
		 file->inum, file->ino.is_xattr ? "xattr" :
		 ubifs_get_type_name(ubifs_get_dent_type(file->ino.mode)),
		 file->ino.nlink, file->calc_nlink,
		 file->ino.xcnt, file->calc_xcnt,
		 file->ino.xsz, file->calc_xsz,
		 file->ino.xnms, file->calc_xnms,
		 file->ino.size, file->calc_size,
		 lnum, file->ino.header.offs, c->dev_name);

	err = ubifs_leb_read(c, lnum, c->sbuf, 0, c->leb_size, 0);
	if (err && err != -EBADMSG)
		return err;

	ino = c->sbuf + file->ino.header.offs;
	ino->nlink = cpu_to_le32(file->calc_nlink);
	ino->xattr_cnt = cpu_to_le32(file->calc_xcnt);
	ino->xattr_size = cpu_to_le32(file->calc_xsz);
	ino->xattr_names = cpu_to_le32(file->calc_xnms);
	ino->size = cpu_to_le64(file->calc_size);
	len = le32_to_cpu(ino->ch.len);
	crc = crc32(UBIFS_CRC32_INIT, (void *)ino + 8, len - 8);
	ino->ch.crc = cpu_to_le32(crc);

	/* Atomically write the fixed LEB back again */
	return ubifs_leb_change(c, lnum, c->sbuf, c->leb_size);
}

/**
 * check_and_correct_files - check and correct information of files.
 * @c: UBIFS file-system description object
 *
 * This function does similar things with dbg_check_filesystem(), besides,
 * it also corrects file information if the calculated information is not
 * consistent with information from flash.
 */
int check_and_correct_files(struct ubifs_info *c)
{
	int err;
	struct rb_node *node;
	struct scanned_file *file;
	struct rb_root *tree = &FSCK(c)->scanned_files;

	for (node = rb_first(tree); node; node = rb_next(node)) {
		file = rb_entry(node, struct scanned_file, rb);

		err = calculate_file_info(c, file, tree);
		if (err)
			return err;
	}

	for (node = rb_first(tree); node; node = rb_next(node)) {
		file = rb_entry(node, struct scanned_file, rb);

		err = correct_file_info(c, file);
		if (err)
			return err;
	}

	if (list_empty(&FSCK(c)->disconnected_files))
		return 0;

	ubifs_assert(c, FSCK(c)->mode != REBUILD_MODE);
	list_for_each_entry(file, &FSCK(c)->disconnected_files, list) {
		err = calculate_file_info(c, file, tree);
		if (err)
			return err;

		/* Reset disconnected file's nlink as one. */
		file->calc_nlink = 1;
		err = correct_file_info(c, file);
		if (err)
			return err;
	}

	return 0;
}
