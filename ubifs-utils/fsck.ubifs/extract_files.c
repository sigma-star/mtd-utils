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
		dbg_fsck("bad inode node(bad inum %lu) at %d:%d, in %s",
			 inum, lnum, offs, c->dev_name);
		goto out;
	}

	if (ch->node_type != key_type(c, key)) {
		dbg_fsck("bad inode node %lu(inconsistent node type %d vs key_type %d) at %d:%d, in %s",
			 inum, ch->node_type, key_type(c, key),
			 lnum, offs, c->dev_name);
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
		dbg_fsck("bad inode node %lu(root inode is not dir, tyoe %u) at %d:%d, in %s",
			 inum, ino_node->mode & S_IFMT, lnum, offs, c->dev_name);
		goto out;
	}

	if (ino_node->size > c->max_inode_sz) {
		dbg_fsck("bad inode node %lu(size %llu is too large) at %d:%d, in %s",
			 inum, ino_node->size, lnum, offs, c->dev_name);
		goto out;
	}

	if (le16_to_cpu(ino->compr_type) >= UBIFS_COMPR_TYPES_CNT) {
		dbg_fsck("bad inode node %lu(unknown compression type %d) at %d:%d, in %s",
			 inum, le16_to_cpu(ino->compr_type), lnum, offs,
			 c->dev_name);
		goto out;
	}

	if (ino_node->xnms + ino_node->xcnt > XATTR_LIST_MAX) {
		dbg_fsck("bad inode node %lu(too big xnames %u xcount %u) at %d:%d, in %s",
			 inum, ino_node->xnms, ino_node->xcnt,
			 lnum, offs, c->dev_name);
		goto out;
	}

	if (data_len < 0 || data_len > UBIFS_MAX_INO_DATA) {
		dbg_fsck("bad inode node %lu(invalid data len %d) at %d:%d, in %s",
			 inum, data_len, lnum, offs, c->dev_name);
		goto out;
	}

	if (UBIFS_INO_NODE_SZ + data_len != node_len) {
		dbg_fsck("bad inode node %lu(inconsistent data len %d vs node len %d) at %d:%d, in %s",
			 inum, data_len, node_len, lnum, offs, c->dev_name);
		goto out;
	}

	if (ino_node->is_xattr) {
		if (!S_ISREG(ino_node->mode)) {
			dbg_fsck("bad inode node %lu(bad type %u for xattr) at %d:%d, in %s",
				 inum, ino_node->mode & S_IFMT,
				 lnum, offs, c->dev_name);
			goto out;
		}
		if (data_len != ino_node->size) {
			dbg_fsck("bad inode node %lu(inconsistent data_len %d vs size %llu for xattr) at %d:%d, in %s",
				 inum, data_len, ino_node->size,
				 lnum, offs, c->dev_name);
			goto out;
		}
		if (ino_node->xcnt || ino_node->xsz || ino_node->xnms) {
			dbg_fsck("bad inode node %lu(non zero xattr count %u xattr size %u xattr names %u for xattr) at %d:%d, in %s",
				 inum, ino_node->xcnt, ino_node->xsz,
				 ino_node->xnms, lnum, offs, c->dev_name);
			goto out;
		}
	}

	switch (ino_node->mode & S_IFMT) {
	case S_IFREG:
		if (!ino_node->is_xattr && data_len != 0) {
			dbg_fsck("bad inode node %lu(bad data len %d for reg file) at %d:%d, in %s",
				 inum, data_len, lnum, offs, c->dev_name);
			goto out;
		}
		break;
	case S_IFDIR:
		if (data_len != 0) {
			dbg_fsck("bad inode node %lu(bad data len %d for dir file) at %d:%d, in %s",
				 inum, data_len, lnum, offs, c->dev_name);
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
			dbg_fsck("bad symlink inode node %lu(bad data len %d) at %d:%d, in %s",
				 inum, data_len, lnum, offs, c->dev_name);
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
			dbg_fsck("bad inode node %lu(bad data len %d for char/block file, expect %d or %d) at %d:%d, in %s",
				 inum, data_len, sz_new, sz_huge, lnum,
				 offs, c->dev_name);
			goto out;
		}
		break;
	}
	case S_IFSOCK:
		fallthrough;
	case S_IFIFO:
		if (data_len != 0) {
			dbg_fsck("bad inode node %lu(bad data len %d for fifo/sock file) at %d:%d, in %s",
				 inum, data_len, lnum, offs, c->dev_name);
			goto out;
		}
		break;
	default:
		/* invalid file type. */
		dbg_fsck("bad inode node %lu(unknown type %u) at %d:%d, in %s",
			 inum, ino_node->mode & S_IFMT, lnum, offs, c->dev_name);
		goto out;
	}

	if (ino_node->is_encrypted && !inode_can_be_encrypted(c, ino_node)) {
		dbg_fsck("bad inode node %lu(encrypted but cannot be encrypted, type %u, is_xattr %d, fs_encrypted %d) at %d:%d, in %s",
			 inum, ino_node->mode & S_IFMT, ino_node->is_xattr,
			 c->encrypted, lnum, offs, c->dev_name);
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
		dbg_fsck("bad %s node(len %d nlen %d type %d inum %lu key_type %d node_type %d) at %d:%d, in %s",
			 ch->node_type == UBIFS_XENT_NODE ? "xattr entry" : "directory entry",
			 node_len, nlen, dent->type, inum, key_type,
			 ch->node_type, lnum, offs, c->dev_name);
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
		dbg_fsck("bad data node(inconsistent node type %d vs key_type %d) at %d:%d, in %s",
			 ch->node_type, key_type(c, key),
			 lnum, offs, c->dev_name);
		goto out;
	}

	if (!inum || inum > INUM_WATERMARK) {
		dbg_fsck("bad data node(bad inum %lu) at %d:%d, in %s",
			 inum, lnum, offs, c->dev_name);
		goto out;
	}

	node_len = le32_to_cpu(ch->len);
	sqnum = le64_to_cpu(ch->sqnum);
	key_copy(c, key, &data_node->key);
	data_node->size = le32_to_cpu(dn->size);

	if (!data_node->size || data_node->size > UBIFS_BLOCK_SIZE) {
		dbg_fsck("bad data node(invalid size %u) at %d:%d, in %s",
			 data_node->size, lnum, offs, c->dev_name);
		goto out;
	}

	if (le16_to_cpu(dn->compr_type) >= UBIFS_COMPR_TYPES_CNT) {
		dbg_fsck("bad data node(invalid compression type %d) at %d:%d, in %s",
			 le16_to_cpu(dn->compr_type), lnum, offs, c->dev_name);
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
