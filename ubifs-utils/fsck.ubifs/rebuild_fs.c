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

/**
 * scanned_info - nodes and files information from scanning.
 * @valid_inos: the tree of scanned inode nodes with 'nlink > 0'
 * @del_inos: the tree of scanned inode nodes with 'nlink = 0'
 * @valid_dents: the tree of scanned dentry nodes with 'inum > 0'
 * @del_dents: the tree of scanned dentry nodes with 'inum = 0'
 */
struct scanned_info {
	struct rb_root valid_inos;
	struct rb_root del_inos;
	struct rb_root valid_dents;
	struct rb_root del_dents;
};

/**
 * struct idx_entry - index entry.
 * @list: link in the list index entries for building index tree
 * @key: key
 * @name: directory entry name used for sorting colliding keys by name
 * @lnum: LEB number
 * @offs: offset
 * @len: length
 *
 * The index is recorded as a linked list which is sorted and used to create
 * the bottom level of the on-flash index tree. The remaining levels of the
 * index tree are each built from the level below.
 */
struct idx_entry {
	struct list_head list;
	union ubifs_key key;
	char *name;
	int name_len;
	int lnum;
	int offs;
	int len;
};

static int init_rebuild_info(struct ubifs_info *c)
{
	int err;

	c->sbuf = vmalloc(c->leb_size);
	if (!c->sbuf) {
		log_err(c, errno, "can not allocate sbuf");
		return -ENOMEM;
	}
	FSCK(c)->rebuild = kzalloc(sizeof(struct ubifs_rebuild_info),
				   GFP_KERNEL);
	if (!FSCK(c)->rebuild) {
		err = -ENOMEM;
		log_err(c, errno, "can not allocate rebuild info");
		goto free_sbuf;
	}
	FSCK(c)->scanned_files = RB_ROOT;
	FSCK(c)->used_lebs = kcalloc(BITS_TO_LONGS(c->main_lebs),
				     sizeof(unsigned long), GFP_KERNEL);
	if (!FSCK(c)->used_lebs) {
		err = -ENOMEM;
		log_err(c, errno, "can not allocate bitmap of used lebs");
		goto free_rebuild;
	}
	FSCK(c)->lpts = kzalloc(sizeof(struct ubifs_lprops) * c->main_lebs,
				GFP_KERNEL);
	if (!FSCK(c)->lpts) {
		err = -ENOMEM;
		log_err(c, errno, "can not allocate lpts");
		goto free_used_lebs;
	}
	FSCK(c)->rebuild->write_buf = vmalloc(c->leb_size);
	if (!FSCK(c)->rebuild->write_buf) {
		err = -ENOMEM;
		goto free_lpts;
	}
	FSCK(c)->rebuild->head_lnum = -1;

	return 0;

free_lpts:
	kfree(FSCK(c)->lpts);
free_used_lebs:
	kfree(FSCK(c)->used_lebs);
free_rebuild:
	kfree(FSCK(c)->rebuild);
free_sbuf:
	vfree(c->sbuf);
	return err;
}

static void destroy_rebuild_info(struct ubifs_info *c)
{
	vfree(FSCK(c)->rebuild->write_buf);
	kfree(FSCK(c)->lpts);
	kfree(FSCK(c)->used_lebs);
	kfree(FSCK(c)->rebuild);
	vfree(c->sbuf);
}

/**
 * insert_or_update_ino_node - insert or update inode node.
 * @c: UBIFS file-system description object
 * @new_ino: new inode node
 * @tree: a tree to record valid/deleted inode node info
 *
 * This function inserts @new_ino into the @tree, or updates inode node
 * if it already exists in the tree. Returns zero in case of success, a
 * negative error code in case of failure.
 */
static int insert_or_update_ino_node(struct ubifs_info *c,
				     struct scanned_ino_node *new_ino,
				     struct rb_root *tree)
{
	int cmp;
	struct scanned_ino_node *ino_node, *old_ino_node = NULL;
	struct rb_node **p, *parent = NULL;

	p = &tree->rb_node;
	while (*p) {
		parent = *p;
		ino_node = rb_entry(parent, struct scanned_ino_node, rb);
		cmp = keys_cmp(c, &new_ino->key, &ino_node->key);
		if (cmp < 0) {
			p = &(*p)->rb_left;
		} else if (cmp > 0) {
			p = &(*p)->rb_right;
		} else {
			old_ino_node = ino_node;
			break;
		}
	}
	if (old_ino_node) {
		if (old_ino_node->header.sqnum < new_ino->header.sqnum) {
			size_t len = offsetof(struct scanned_ino_node, rb);

			memcpy(old_ino_node, new_ino, len);
		}
		return 0;
	}

	ino_node = kmalloc(sizeof(struct scanned_ino_node), GFP_KERNEL);
	if (!ino_node)
		return -ENOMEM;

	*ino_node = *new_ino;
	rb_link_node(&ino_node->rb, parent, p);
	rb_insert_color(&ino_node->rb, tree);

	return 0;
}

static int namecmp(const char *a, int la, const char *b, int lb)
{
	int cmp, len = min(la, lb);

	cmp = memcmp(a, b, len);
	if (cmp)
		return cmp;

	return la - lb;
}

/**
 * insert_or_update_dent_node - insert or update dentry node.
 * @c: UBIFS file-system description object
 * @new_dent: new dentry node
 * @tree: a tree to record valid/deleted dentry node info
 *
 * This function inserts @new_dent into the @tree, or updates dent node
 * if it already exists in the tree. Returns zero in case of success, a
 * negative error code in case of failure.
 */
static int insert_or_update_dent_node(struct ubifs_info *c,
				      struct scanned_dent_node *new_dent,
				      struct rb_root *tree)
{
	int cmp;
	struct scanned_dent_node *dent_node, *old_dent_node = NULL;
	struct rb_node **p, *parent = NULL;

	p = &tree->rb_node;
	while (*p) {
		parent = *p;
		dent_node = rb_entry(parent, struct scanned_dent_node, rb);
		cmp = keys_cmp(c, &new_dent->key, &dent_node->key);
		if (cmp < 0) {
			p = &(*p)->rb_left;
		} else if (cmp > 0) {
			p = &(*p)->rb_right;
		} else {
			cmp = namecmp(new_dent->name, new_dent->nlen,
				      dent_node->name, dent_node->nlen);
			if (cmp < 0) {
				p = &(*p)->rb_left;
			} else if (cmp > 0) {
				p = &(*p)->rb_right;
			} else {
				old_dent_node = dent_node;
				break;
			}
		}
	}
	if (old_dent_node) {
		if (old_dent_node->header.sqnum < new_dent->header.sqnum) {
			size_t len = offsetof(struct scanned_dent_node, rb);

			memcpy(old_dent_node, new_dent, len);
		}
		return 0;
	}

	dent_node = kmalloc(sizeof(struct scanned_dent_node), GFP_KERNEL);
	if (!dent_node)
		return -ENOMEM;

	*dent_node = *new_dent;
	rb_link_node(&dent_node->rb, parent, p);
	rb_insert_color(&dent_node->rb, tree);

	return 0;
}

/**
 * process_scanned_node - process scanned node.
 * @c: UBIFS file-system description object
 * @lnum: logical eraseblock number
 * @snod: scanned node
 * @si: records nodes and files information during scanning
 *
 * This function parses, checks and records scanned node information.
 * Returns zero in case of success, 1% if the scanned LEB doesn't hold file
 * data and should be ignored(eg. index LEB), a negative error code in case
 * of failure.
 */
static int process_scanned_node(struct ubifs_info *c, int lnum,
				struct ubifs_scan_node *snod,
				struct scanned_info *si)
{
	ino_t inum;
	int offs = snod->offs;
	void *node = snod->node;
	union ubifs_key *key = &snod->key;
	struct rb_root *tree;
	struct scanned_node *sn;
	struct scanned_ino_node ino_node;
	struct scanned_dent_node dent_node;
	struct scanned_data_node data_node;
	struct scanned_trun_node trun_node;

	switch (snod->type) {
	case UBIFS_INO_NODE:
	{
		if (!parse_ino_node(c, lnum, offs, node, key, &ino_node))
			return 0;

		tree = &si->del_inos;
		if (ino_node.nlink)
			tree = &si->valid_inos;
		return insert_or_update_ino_node(c, &ino_node, tree);
	}
	case UBIFS_DENT_NODE:
	case UBIFS_XENT_NODE:
	{
		if (!parse_dent_node(c, lnum, offs, node, key, &dent_node))
			return 0;

		tree = &si->del_dents;
		if (dent_node.inum)
			tree = &si->valid_dents;
		return insert_or_update_dent_node(c, &dent_node, tree);
	}
	case UBIFS_DATA_NODE:
	{
		if (!parse_data_node(c, lnum, offs, node, key, &data_node))
			return 0;

		inum = key_inum(c, key);
		sn = (struct scanned_node *)&data_node;
		break;
	}
	case UBIFS_TRUN_NODE:
	{
		if (!parse_trun_node(c, lnum, offs, node, key, &trun_node))
			return 0;

		inum = le32_to_cpu(((struct ubifs_trun_node *)node)->inum);
		sn = (struct scanned_node *)&trun_node;
		break;
	}
	default:
		dbg_fsck("skip node type %d, at %d:%d, in %s",
			 snod->type, lnum, offs, c->dev_name);
		return 1;
	}

	tree = &FSCK(c)->scanned_files;
	return insert_or_update_file(c, tree, sn, key_type(c, key), inum);
}

/**
 * destroy_scanned_info - destroy scanned nodes.
 * @c: UBIFS file-system description object
 * @si: records nodes and files information during scanning
 *
 * Destroy scanned files and all data/dentry nodes attached to file, destroy
 * valid/deleted inode/dentry info.
 */
static void destroy_scanned_info(struct ubifs_info *c, struct scanned_info *si)
{
	struct scanned_ino_node *ino_node;
	struct scanned_dent_node *dent_node;
	struct rb_node *this;

	destroy_file_tree(c, &FSCK(c)->scanned_files);

	this = rb_first(&si->valid_inos);
	while (this) {
		ino_node = rb_entry(this, struct scanned_ino_node, rb);
		this = rb_next(this);

		rb_erase(&ino_node->rb, &si->valid_inos);
		kfree(ino_node);
	}

	this = rb_first(&si->del_inos);
	while (this) {
		ino_node = rb_entry(this, struct scanned_ino_node, rb);
		this = rb_next(this);

		rb_erase(&ino_node->rb, &si->del_inos);
		kfree(ino_node);
	}

	this = rb_first(&si->valid_dents);
	while (this) {
		dent_node = rb_entry(this, struct scanned_dent_node, rb);
		this = rb_next(this);

		rb_erase(&dent_node->rb, &si->valid_dents);
		kfree(dent_node);
	}

	this = rb_first(&si->del_dents);
	while (this) {
		dent_node = rb_entry(this, struct scanned_dent_node, rb);
		this = rb_next(this);

		rb_erase(&dent_node->rb, &si->del_dents);
		kfree(dent_node);
	}
}

/**
 * scan_nodes - scan node information from flash.
 * @c: UBIFS file-system description object
 * @si: records nodes and files information during scanning
 *
 * This function scans nodes from flash, all ino/dent nodes are split
 * into valid tree and deleted tree, all trun/data nodes are collected
 * into file, the file is inserted into @FSCK(c)->scanned_files.
 */
static int scan_nodes(struct ubifs_info *c, struct scanned_info *si)
{
	int lnum, err = 0;
	struct ubifs_scan_leb *sleb;
	struct ubifs_scan_node *snod;

	for (lnum = c->main_first; lnum < c->leb_cnt; ++lnum) {
		dbg_fsck("scan nodes at LEB %d, in %s", lnum, c->dev_name);

		sleb = ubifs_scan(c, lnum, 0, c->sbuf, 1);
		if (IS_ERR(sleb)) {
			if (PTR_ERR(sleb) != -EUCLEAN)
				return PTR_ERR(sleb);

			sleb = ubifs_recover_leb(c, lnum, 0, c->sbuf, -1);
			if (IS_ERR(sleb)) {
				if (PTR_ERR(sleb) != -EUCLEAN)
					return PTR_ERR(sleb);

				/* This LEB holds corrupted data, abandon it. */
				continue;
			}
		}

		list_for_each_entry(snod, &sleb->nodes, list) {
			if (snod->sqnum > c->max_sqnum)
				c->max_sqnum = snod->sqnum;

			err = process_scanned_node(c, lnum, snod, si);
			if (err < 0) {
				log_err(c, 0, "process node failed at LEB %d, err %d",
					lnum, err);
				ubifs_scan_destroy(sleb);
				goto out;
			} else if (err == 1) {
				err = 0;
				break;
			}
		}

		ubifs_scan_destroy(sleb);
	}

out:
	return err;
}

static struct scanned_ino_node *
lookup_valid_ino_node(struct ubifs_info *c, struct scanned_info *si,
		      struct scanned_ino_node *target)
{
	int cmp;
	struct scanned_ino_node *ino_node;
	struct rb_node *p;

	p = si->valid_inos.rb_node;
	while (p) {
		ino_node = rb_entry(p, struct scanned_ino_node, rb);
		cmp = keys_cmp(c, &target->key, &ino_node->key);
		if (cmp < 0) {
			p = p->rb_left;
		} else if (cmp > 0) {
			p = p->rb_right;
		} else {
			if (target->header.sqnum > ino_node->header.sqnum)
				return ino_node;
			else
				return NULL;
		}
	}

	return NULL;
}

static struct scanned_dent_node *
lookup_valid_dent_node(struct ubifs_info *c, struct scanned_info *si,
		       struct scanned_dent_node *target)
{
	int cmp;
	struct scanned_dent_node *dent_node;
	struct rb_node *p;

	p = si->valid_dents.rb_node;
	while (p) {
		dent_node = rb_entry(p, struct scanned_dent_node, rb);
		cmp = keys_cmp(c, &target->key, &dent_node->key);
		if (cmp < 0) {
			p = p->rb_left;
		} else if (cmp > 0) {
			p = p->rb_right;
		} else {
			cmp = namecmp(target->name, target->nlen,
				      dent_node->name, dent_node->nlen);
			if (cmp < 0) {
				p = p->rb_left;
			} else if (cmp > 0) {
				p = p->rb_right;
			} else {
				if (target->header.sqnum >
				    dent_node->header.sqnum)
					return dent_node;
				else
					return NULL;
			}
		}
	}

	return NULL;
}

static void update_lpt(struct ubifs_info *c, struct scanned_node *sn,
		       bool deleted)
{
	int index = sn->lnum - c->main_first;
	int pos = sn->offs + ALIGN(sn->len, 8);

	set_bit(index, FSCK(c)->used_lebs);
	FSCK(c)->lpts[index].end = max_t(int, FSCK(c)->lpts[index].end, pos);

	if (deleted)
		return;

	FSCK(c)->lpts[index].used += ALIGN(sn->len, 8);
}

/**
 * remove_del_nodes - remove deleted nodes from valid node tree.
 * @c: UBIFS file-system description object
 * @si: records nodes and files information during scanning
 *
 * This function compares sqnum between deleted node and corresponding valid
 * node, removes valid node from tree if the sqnum of deleted node is bigger.
 * Deleted ino/dent nodes will be removed from @si->del_inos/@si->del_dents
 * after this function finished.
 */
static void remove_del_nodes(struct ubifs_info *c, struct scanned_info *si)
{
	struct scanned_ino_node *del_ino_node, *valid_ino_node;
	struct scanned_dent_node *del_dent_node, *valid_dent_node;
	struct rb_node *this;

	this = rb_first(&si->del_inos);
	while (this) {
		del_ino_node = rb_entry(this, struct scanned_ino_node, rb);
		this = rb_next(this);

		valid_ino_node = lookup_valid_ino_node(c, si, del_ino_node);
		if (valid_ino_node) {
			update_lpt(c, &del_ino_node->header, true);
			rb_erase(&valid_ino_node->rb, &si->valid_inos);
			kfree(valid_ino_node);
		}

		rb_erase(&del_ino_node->rb, &si->del_inos);
		kfree(del_ino_node);
	}

	this = rb_first(&si->del_dents);
	while (this) {
		del_dent_node = rb_entry(this, struct scanned_dent_node, rb);
		this = rb_next(this);

		valid_dent_node = lookup_valid_dent_node(c, si, del_dent_node);
		if (valid_dent_node) {
			update_lpt(c, &del_dent_node->header, true);
			rb_erase(&valid_dent_node->rb, &si->valid_dents);
			kfree(valid_dent_node);
		}

		rb_erase(&del_dent_node->rb, &si->del_dents);
		kfree(del_dent_node);
	}
}

/**
 * add_valid_nodes_into_file - add valid nodes into file.
 * @c: UBIFS file-system description object
 * @si: records nodes and files information during scanning
 *
 * This function adds valid nodes into corresponding file, all valid ino/dent
 * nodes will be removed from @si->valid_inos/@si->valid_dents if the function
 * is executed successfully.
 */
static int add_valid_nodes_into_file(struct ubifs_info *c,
				     struct scanned_info *si)
{
	int err, type;
	ino_t inum;
	struct scanned_node *sn;
	struct scanned_ino_node *ino_node;
	struct scanned_dent_node *dent_node;
	struct rb_node *this;
	struct rb_root *tree = &FSCK(c)->scanned_files;

	this = rb_first(&si->valid_inos);
	while (this) {
		ino_node = rb_entry(this, struct scanned_ino_node, rb);
		this = rb_next(this);

		sn = (struct scanned_node *)ino_node;
		type = key_type(c, &ino_node->key);
		inum = key_inum(c, &ino_node->key);
		err = insert_or_update_file(c, tree, sn, type, inum);
		if (err)
			return err;

		rb_erase(&ino_node->rb, &si->valid_inos);
		kfree(ino_node);
	}

	this = rb_first(&si->valid_dents);
	while (this) {
		dent_node = rb_entry(this, struct scanned_dent_node, rb);
		this = rb_next(this);

		sn = (struct scanned_node *)dent_node;
		inum = dent_node->inum;
		type = key_type(c, &dent_node->key);
		err = insert_or_update_file(c, tree, sn, type, inum);
		if (err)
			return err;

		rb_erase(&dent_node->rb, &si->valid_dents);
		kfree(dent_node);
	}

	return 0;
}

/**
 * filter_invalid_files - filter out invalid files.
 * @c: UBIFS file-system description object
 *
 * This function filters out invalid files(eg. inconsistent types between
 * inode and dentry node, or missing inode/dentry node, or encrypted inode
 * has no encryption related xattrs, etc.).
 */
static void filter_invalid_files(struct ubifs_info *c)
{
	struct rb_node *node;
	struct scanned_file *file;
	struct rb_root *tree = &FSCK(c)->scanned_files;
	LIST_HEAD(tmp_list);

	/* Add all xattr files into a list. */
	for (node = rb_first(tree); node; node = rb_next(node)) {
		file = rb_entry(node, struct scanned_file, rb);

		if (file->ino.is_xattr)
			list_add(&file->list, &tmp_list);
	}

	/*
	 * Round 1: Traverse xattr files, check whether the xattr file is
	 * valid, move valid xattr file into corresponding host file's subtree.
	 */
	while (!list_empty(&tmp_list)) {
		file = list_entry(tmp_list.next, struct scanned_file, list);

		list_del(&file->list);
		rb_erase(&file->rb, tree);
		if (!file_is_valid(c, file, tree, NULL)) {
			destroy_file_content(c, file);
			kfree(file);
		}
	}

	/* Round 2: Traverse non-xattr files. */
	for (node = rb_first(tree); node; node = rb_next(node)) {
		file = rb_entry(node, struct scanned_file, rb);

		if (!file_is_valid(c, file, tree, NULL))
			list_add(&file->list, &tmp_list);
	}

	/* Remove invalid files. */
	while (!list_empty(&tmp_list)) {
		file = list_entry(tmp_list.next, struct scanned_file, list);

		list_del(&file->list);
		destroy_file_content(c, file);
		rb_erase(&file->rb, tree);
		kfree(file);
	}
}

/**
 * extract_dentry_tree - extract reachable directory entries.
 * @c: UBIFS file-system description object
 *
 * This function iterates all directory entries and remove those
 * unreachable ones. 'Unreachable' means that a directory entry can
 * not be searched from '/'.
 */
static void extract_dentry_tree(struct ubifs_info *c)
{
	struct rb_node *node;
	struct scanned_file *file;
	struct rb_root *tree = &FSCK(c)->scanned_files;
	LIST_HEAD(unreachable);

	for (node = rb_first(tree); node; node = rb_next(node)) {
		file = rb_entry(node, struct scanned_file, rb);

		/*
		 * Since all xattr files are already attached to corresponding
		 * host file, there are only non-xattr files in the file tree.
		 */
		ubifs_assert(c, !file->ino.is_xattr);
		if (!file_is_reachable(c, file, tree))
			list_add(&file->list, &unreachable);
	}

	/* Remove unreachable files. */
	while (!list_empty(&unreachable)) {
		file = list_entry(unreachable.next, struct scanned_file, list);

		dbg_fsck("remove unreachable file %lu, in %s",
			 file->inum, c->dev_name);
		list_del(&file->list);
		destroy_file_content(c, file);
		rb_erase(&file->rb, tree);
		kfree(file);
	}
}

static void init_root_ino(struct ubifs_info *c, struct ubifs_ino_node *ino)
{
	__le64 tmp_le64;

	/* Create default root inode */
	ino_key_init_flash(c, &ino->key, UBIFS_ROOT_INO);
	ino->ch.node_type = UBIFS_INO_NODE;
	ino->creat_sqnum = cpu_to_le64(++c->max_sqnum);
	ino->nlink = cpu_to_le32(2);
	tmp_le64 = cpu_to_le64(time(NULL));
	ino->atime_sec   = tmp_le64;
	ino->ctime_sec   = tmp_le64;
	ino->mtime_sec   = tmp_le64;
	ino->atime_nsec  = 0;
	ino->ctime_nsec  = 0;
	ino->mtime_nsec  = 0;
	ino->mode = cpu_to_le32(S_IFDIR | S_IRUGO | S_IWUSR | S_IXUGO);
	ino->size = cpu_to_le64(UBIFS_INO_NODE_SZ);
	/* Set compression enabled by default */
	ino->flags = cpu_to_le32(UBIFS_COMPR_FL);
}

/**
 * flush_write_buf - flush write buffer.
 * @c: UBIFS file-system description object
 *
 * This function flush write buffer to LEB @FSCK(c)->rebuild->head_lnum, then
 * set @FSCK(c)->rebuild->head_lnum to '-1'.
 */
static int flush_write_buf(struct ubifs_info *c)
{
	int len, pad, err;

	if (!FSCK(c)->rebuild->head_offs)
		return 0;

	len = ALIGN(FSCK(c)->rebuild->head_offs, c->min_io_size);
	pad = len - FSCK(c)->rebuild->head_offs;
	if (pad)
		ubifs_pad(c, FSCK(c)->rebuild->write_buf +
			  FSCK(c)->rebuild->head_offs, pad);

	err = ubifs_leb_write(c, FSCK(c)->rebuild->head_lnum,
			      FSCK(c)->rebuild->write_buf, 0, len);
	if (err)
		return err;

	if (FSCK(c)->rebuild->need_update_lpt) {
		int index = FSCK(c)->rebuild->head_lnum - c->main_first;

		FSCK(c)->lpts[index].free = c->leb_size - len;
		FSCK(c)->lpts[index].dirty = pad;
		FSCK(c)->lpts[index].flags = LPROPS_INDEX;
	}

	FSCK(c)->rebuild->head_lnum = -1;

	return 0;
}

/**
 * reserve_space - reserve enough space to write data.
 * @c: UBIFS file-system description object
 * @len: the length of written data
 * @lnum: the write LEB number is returned here
 * @offs: the write pos in LEB is returned here
 *
 * This function finds target position <@lnum, @offs> to write data with
 * length of @len.
 */
static int reserve_space(struct ubifs_info *c, int len, int *lnum, int *offs)
{
	int err, new_lnum;

	if (FSCK(c)->rebuild->head_lnum == -1) {
get_new:
		new_lnum = get_free_leb(c);
		if (new_lnum < 0)
			return new_lnum;

		err = ubifs_leb_unmap(c, new_lnum);
		if (err)
			return err;

		FSCK(c)->rebuild->head_lnum = new_lnum;
		FSCK(c)->rebuild->head_offs = 0;
	}

	if (len > c->leb_size - FSCK(c)->rebuild->head_offs) {
		err = flush_write_buf(c);
		if (err)
			return err;

		goto get_new;
	}

	*lnum = FSCK(c)->rebuild->head_lnum;
	*offs = FSCK(c)->rebuild->head_offs;
	FSCK(c)->rebuild->head_offs += ALIGN(len, 8);

	return 0;
}

static void copy_node_data(struct ubifs_info *c, void *node, int offs, int len)
{
	memcpy(FSCK(c)->rebuild->write_buf + offs, node, len);
	memset(FSCK(c)->rebuild->write_buf + offs + len, 0xff, ALIGN(len, 8) - len);
}

/**
 * create_root - create root dir.
 * @c: UBIFS file-system description object
 *
 * This function creates root dir.
 */
static int create_root(struct ubifs_info *c)
{
	int err, lnum, offs;
	struct ubifs_ino_node *ino;
	struct scanned_file *file;

	ino = kzalloc(ALIGN(UBIFS_INO_NODE_SZ, c->min_io_size), GFP_KERNEL);
	if (!ino)
		return -ENOMEM;

	c->max_sqnum = 0;
	c->highest_inum = UBIFS_FIRST_INO;
	init_root_ino(c, ino);
	err = ubifs_prepare_node_hmac(c, ino, UBIFS_INO_NODE_SZ, -1, 1);
	if (err)
		goto out;

	err = reserve_space(c, UBIFS_INO_NODE_SZ, &lnum, &offs);
	if (err)
		goto out;

	copy_node_data(c, ino, offs, UBIFS_INO_NODE_SZ);

	err = flush_write_buf(c);
	if (err)
		goto out;

	file = kzalloc(sizeof(struct scanned_file), GFP_KERNEL);
	if (!file) {
		err = -ENOMEM;
		goto out;
	}

	file->inum = UBIFS_ROOT_INO;
	file->dent_nodes = RB_ROOT;
	file->data_nodes = RB_ROOT;
	INIT_LIST_HEAD(&file->list);

	file->ino.header.exist = true;
	file->ino.header.lnum = lnum;
	file->ino.header.offs = offs;
	file->ino.header.len = UBIFS_INO_NODE_SZ;
	file->ino.header.sqnum = le64_to_cpu(ino->ch.sqnum);
	ino_key_init(c, &file->ino.key, UBIFS_ROOT_INO);
	file->ino.is_xattr = le32_to_cpu(ino->flags) & UBIFS_XATTR_FL;
	file->ino.mode = le32_to_cpu(ino->mode);
	file->calc_nlink = file->ino.nlink = le32_to_cpu(ino->nlink);
	file->calc_xcnt = file->ino.xcnt = le32_to_cpu(ino->xattr_cnt);
	file->calc_xsz = file->ino.xsz = le32_to_cpu(ino->xattr_size);
	file->calc_xnms = file->ino.xnms = le32_to_cpu(ino->xattr_names);
	file->calc_size = file->ino.size = le64_to_cpu(ino->size);

	rb_link_node(&file->rb, NULL, &FSCK(c)->scanned_files.rb_node);
	rb_insert_color(&file->rb, &FSCK(c)->scanned_files);

out:
	kfree(ino);
	return err;
}

static const char *get_file_name(struct ubifs_info *c, struct scanned_file *file)
{
	static char name[UBIFS_MAX_NLEN + 1];
	struct rb_node *node;
	struct scanned_dent_node *dent_node;

	node = rb_first(&file->dent_nodes);
	if (!node) {
		ubifs_assert(c, file->inum == UBIFS_ROOT_INO);
		return "/";
	}

	if (c->encrypted && !file->ino.is_xattr)
		/* Encrypted file name. */
		return "<encrypted>";

	/* Get name from any one dentry. */
	dent_node = rb_entry(node, struct scanned_dent_node, rb);
	memcpy(name, dent_node->name, dent_node->nlen);
	/* @dent->name could be non '\0' terminated. */
	name[dent_node->nlen] = '\0';
	return name;
}

static int parse_node_info(struct ubifs_info *c, struct scanned_node *sn,
			   union ubifs_key *key, char *name, int name_len,
			   struct list_head *idx_list, int *idx_cnt)
{
	struct idx_entry *e;

	update_lpt(c, sn, idx_cnt == NULL);

	if (idx_cnt == NULL)
		/* Skip truncation node. */
		return 0;

	e = kmalloc(sizeof(struct idx_entry), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	key_copy(c, key, &e->key);
	e->name = name;
	e->name_len = name_len;
	e->lnum = sn->lnum;
	e->offs = sn->offs;
	e->len = sn->len;
	list_add_tail(&e->list, idx_list);
	*idx_cnt = *idx_cnt + 1;

	return 0;
}

static int add_idx_node(struct ubifs_info *c, struct ubifs_idx_node *idx,
			union ubifs_key *key, int child_cnt,
			struct idx_entry *e)
{
	int err, lnum, offs, len;

	len = ubifs_idx_node_sz(c, child_cnt);
	ubifs_prepare_node(c, idx, len, 0);

	err = reserve_space(c, len, &lnum, &offs);
	if (err)
		return err;

	copy_node_data(c, idx, offs, len);

	c->calc_idx_sz += ALIGN(len, 8);

	/* The last index node written will be the root */
	c->zroot.lnum = lnum;
	c->zroot.offs = offs;
	c->zroot.len = len;

	key_copy(c, key, &e->key);
	e->lnum = lnum;
	e->offs = offs;
	e->len = len;

	return err;
}

static int cmp_idx(void *priv, const struct list_head *a,
		   const struct list_head *b)
{
	int cmp;
	struct ubifs_info *c = priv;
	struct idx_entry *ia, *ib;

	if (a == b)
		return 0;

	ia = list_entry(a, struct idx_entry, list);
	ib = list_entry(b, struct idx_entry, list);

	cmp = keys_cmp(c, &ia->key, &ib->key);
	if (cmp)
		return cmp;

	return namecmp(ia->name, ia->name_len, ib->name, ib->name_len);
}

/**
 * build_tnc - construct TNC and write it into flash.
 * @c: UBIFS file-system description object
 * @lower_idxs: leaf entries of TNC
 * @lower_cnt: the count of @lower_idxs
 *
 * This function builds TNC according to @lower_idxs and writes all index
 * nodes into flash.
 */
static int build_tnc(struct ubifs_info *c, struct list_head *lower_idxs,
		     int lower_cnt)
{
	int i, j, err, upper_cnt, child_cnt, idx_sz, level = 0;
	struct idx_entry *pe, *tmp_e, *e = NULL;
	struct ubifs_idx_node *idx;
	struct ubifs_branch *br;
	union ubifs_key key;
	LIST_HEAD(upper_idxs);

	idx_sz = ubifs_idx_node_sz(c, c->fanout);
	idx = kmalloc(idx_sz, GFP_KERNEL);
	if (!idx)
		return -ENOMEM;

	list_sort(c, lower_idxs, cmp_idx);
	FSCK(c)->rebuild->need_update_lpt = true;

	ubifs_assert(c, lower_cnt != 0);

	do {
		upper_cnt = lower_cnt / c->fanout;
		if (lower_cnt % c->fanout)
			upper_cnt += 1;
		e = list_first_entry(lower_idxs, struct idx_entry, list);

		for (i = 0; i < upper_cnt; i++) {
			if (i == upper_cnt - 1) {
				child_cnt = lower_cnt % c->fanout;
				if (child_cnt == 0)
					child_cnt = c->fanout;
			} else
				child_cnt = c->fanout;

			key_copy(c, &e->key, &key);
			memset(idx, 0, idx_sz);
			idx->ch.node_type = UBIFS_IDX_NODE;
			idx->child_cnt = cpu_to_le16(child_cnt);
			idx->level = cpu_to_le16(level);
			for (j = 0; j < child_cnt; j++) {
				ubifs_assert(c,
				    !list_entry_is_head(e, lower_idxs, list));
				br = ubifs_idx_branch(c, idx, j);
				key_write_idx(c, &e->key, &br->key);
				br->lnum = cpu_to_le32(e->lnum);
				br->offs = cpu_to_le32(e->offs);
				br->len = cpu_to_le32(e->len);
				e = list_next_entry(e, list);
			}

			pe = kmalloc(sizeof(struct idx_entry), GFP_KERNEL);
			if (!pe) {
				err = -ENOMEM;
				goto out;
			}

			err = add_idx_node(c, idx, &key, child_cnt, pe);
			if (err) {
				kfree(pe);
				goto out;
			}

			list_add_tail(&pe->list, &upper_idxs);
		}

		level++;
		list_for_each_entry_safe(e, tmp_e, lower_idxs, list) {
			list_del(&e->list);
			kfree(e);
		}
		list_splice_init(&upper_idxs, lower_idxs);
		lower_cnt = upper_cnt;
	} while (lower_cnt > 1);

	/* Set the index head */
	c->ihead_lnum = FSCK(c)->rebuild->head_lnum;
	c->ihead_offs = ALIGN(FSCK(c)->rebuild->head_offs, c->min_io_size);

	/* Flush the last index LEB */
	err = flush_write_buf(c);
	FSCK(c)->rebuild->need_update_lpt = false;

out:
	list_for_each_entry_safe(e, tmp_e, lower_idxs, list) {
		list_del(&e->list);
		kfree(e);
	}
	list_for_each_entry_safe(e, tmp_e, &upper_idxs, list) {
		list_del(&e->list);
		kfree(e);
	}
	kfree(idx);
	return err;
}

static int record_file_used_lebs(struct ubifs_info *c,
				 struct scanned_file *file,
				 struct list_head *idx_list, int *idx_cnt)
{
	int err;
	struct rb_node *node;
	struct scanned_file *xattr_file;
	struct scanned_dent_node *dent_node;
	struct scanned_data_node *data_node;

	dbg_fsck("recovered file(inum:%lu name:%s type:%s), in %s",
		 file->inum, get_file_name(c, file),
		 file->ino.is_xattr ? "xattr" :
		 ubifs_get_type_name(ubifs_get_dent_type(file->ino.mode)),
		 c->dev_name);
	c->highest_inum = max_t(ino_t, c->highest_inum, file->inum);

	err = parse_node_info(c, &file->ino.header, &file->ino.key,
			      NULL, 0, idx_list, idx_cnt);
	if (err)
		return err;

	if (file->trun.header.exist) {
		err = parse_node_info(c, &file->trun.header, NULL, NULL,
				      0, idx_list, NULL);
		if (err)
			return err;
	}

	for (node = rb_first(&file->data_nodes); node; node = rb_next(node)) {
		data_node = rb_entry(node, struct scanned_data_node, rb);

		err = parse_node_info(c, &data_node->header, &data_node->key,
				      NULL, 0, idx_list, idx_cnt);
		if (err)
			return err;
	}

	for (node = rb_first(&file->dent_nodes); node; node = rb_next(node)) {
		dent_node = rb_entry(node, struct scanned_dent_node, rb);

		err = parse_node_info(c, &dent_node->header, &dent_node->key,
				      dent_node->name, dent_node->nlen,
				      idx_list, idx_cnt);
		if (err)
			return err;
	}

	for (node = rb_first(&file->xattr_files); node; node = rb_next(node)) {
		xattr_file = rb_entry(node, struct scanned_file, rb);

		err = record_file_used_lebs(c, xattr_file, idx_list, idx_cnt);
		if (err)
			return err;
	}

	return err;
}

/**
 * traverse_files_and_nodes - traverse all nodes from valid files.
 * @c: UBIFS file-system description object
 *
 * This function traverses all nodes from valid files and does following
 * things:
 * 1. If there are no scanned files, create default empty filesystem.
 * 2. Record all used LEBs which may hold useful nodes, then left unused
 *    LEBs could be taken for storing new index tree.
 * 3. Re-write data to prevent failed gc scanning in the subsequent mounting
 *    process caused by corrupted data.
 * 4. Build TNC.
 */
static int traverse_files_and_nodes(struct ubifs_info *c)
{
	int i, err = 0, idx_cnt = 0;
	struct rb_node *node;
	struct scanned_file *file;
	struct rb_root *tree = &FSCK(c)->scanned_files;
	struct idx_entry *ie, *tmp_ie;
	LIST_HEAD(idx_list);

	if (rb_first(tree) == NULL) {
		/* No scanned files. Create root dir. */
		log_out(c, "No files found, create empty filesystem");
		err = create_root(c);
		if (err)
			return err;
	}

	log_out(c, "Record used LEBs");
	for (node = rb_first(tree); node; node = rb_next(node)) {
		file = rb_entry(node, struct scanned_file, rb);

		err = record_file_used_lebs(c, file, &idx_list, &idx_cnt);
		if (err)
			goto out_idx_list;
	}

	/* Re-write data. */
	log_out(c, "Re-write data");
	for (i = 0; i < c->main_lebs; ++i) {
		int lnum, len, end;

		if (!test_bit(i, FSCK(c)->used_lebs))
			continue;

		lnum = i + c->main_first;
		dbg_fsck("re-write LEB %d, in %s", lnum, c->dev_name);

		end = FSCK(c)->lpts[i].end;
		len = ALIGN(end, c->min_io_size);

		err = ubifs_leb_read(c, lnum, c->sbuf, 0, len, 0);
		if (err && err != -EBADMSG)
			goto out_idx_list;

		if (len > end)
			ubifs_pad(c, c->sbuf + end, len - end);

		err = ubifs_leb_change(c, lnum, c->sbuf, len);
		if (err)
			goto out_idx_list;
	}

	/* Build TNC. */
	log_out(c, "Build TNC");
	err = build_tnc(c, &idx_list, idx_cnt);

out_idx_list:
	list_for_each_entry_safe(ie, tmp_ie, &idx_list, list) {
		list_del(&ie->list);
		kfree(ie);
	}
	return err;
}

static int calculate_lp(struct ubifs_info *c, int index, int *free, int *dirty,
			__unused int *is_idx)
{
	if (!test_bit(index, FSCK(c)->used_lebs) ||
	    c->gc_lnum == index + c->main_first) {
		*free = c->leb_size;
		*dirty = 0;
	} else if (FSCK(c)->lpts[index].flags & LPROPS_INDEX) {
		*free = FSCK(c)->lpts[index].free;
		*dirty = FSCK(c)->lpts[index].dirty;
	} else {
		int len = ALIGN(FSCK(c)->lpts[index].end, c->min_io_size);

		*free = c->leb_size - len;
		*dirty = len - FSCK(c)->lpts[index].used;

		if (*dirty == c->leb_size) {
			*free = c->leb_size;
			*dirty = 0;
		}
	}

	return 0;
}

/**
 * clean_log - clean up log area.
 * @c: UBIFS file-system description object
 *
 * This function cleans up log area, since there is no need to do recovery
 * in next mounting.
 */
static int clean_log(struct ubifs_info *c)
{
	int lnum, err;
	struct ubifs_cs_node *cs;

	for (lnum = UBIFS_LOG_LNUM; lnum <= c->log_last; lnum++) {
		err = ubifs_leb_unmap(c, lnum);
		if (err)
			return err;
	}

	cs = kzalloc(ALIGN(UBIFS_CS_NODE_SZ, c->min_io_size), GFP_KERNEL);
	if (!cs)
		return -ENOMEM;

	cs->ch.node_type = UBIFS_CS_NODE;
	cs->cmt_no = cpu_to_le64(0);

	err = ubifs_write_node(c, cs, UBIFS_CS_NODE_SZ, UBIFS_LOG_LNUM, 0);
	kfree(cs);
	if (err)
		return err;

	return 0;
}

/**
 * write_master - write master nodes.
 * @c: UBIFS file-system description object
 *
 * This function updates meta information into master node and writes master
 * node into master area.
 */
static int write_master(struct ubifs_info *c)
{
	int err, lnum;
	struct ubifs_mst_node *mst;

	mst = kzalloc(c->mst_node_alsz, GFP_KERNEL);
	if (!mst)
		return -ENOMEM;

	mst->ch.node_type = UBIFS_MST_NODE;
	mst->log_lnum     = cpu_to_le32(UBIFS_LOG_LNUM);
	mst->highest_inum = cpu_to_le64(c->highest_inum);
	mst->cmt_no       = 0;
	mst->root_lnum    = cpu_to_le32(c->zroot.lnum);
	mst->root_offs    = cpu_to_le32(c->zroot.offs);
	mst->root_len     = cpu_to_le32(c->zroot.len);
	mst->gc_lnum      = cpu_to_le32(c->gc_lnum);
	mst->ihead_lnum   = cpu_to_le32(c->ihead_lnum);
	mst->ihead_offs   = cpu_to_le32(c->ihead_offs);
	mst->index_size   = cpu_to_le64(c->calc_idx_sz);
	mst->lpt_lnum     = cpu_to_le32(c->lpt_lnum);
	mst->lpt_offs     = cpu_to_le32(c->lpt_offs);
	mst->nhead_lnum   = cpu_to_le32(c->nhead_lnum);
	mst->nhead_offs   = cpu_to_le32(c->nhead_offs);
	mst->ltab_lnum    = cpu_to_le32(c->ltab_lnum);
	mst->ltab_offs    = cpu_to_le32(c->ltab_offs);
	mst->lsave_lnum   = cpu_to_le32(c->lsave_lnum);
	mst->lsave_offs   = cpu_to_le32(c->lsave_offs);
	mst->lscan_lnum   = cpu_to_le32(c->main_first);
	mst->empty_lebs   = cpu_to_le32(c->lst.empty_lebs);
	mst->idx_lebs     = cpu_to_le32(c->lst.idx_lebs);
	mst->leb_cnt      = cpu_to_le32(c->leb_cnt);
	mst->total_free   = cpu_to_le64(c->lst.total_free);
	mst->total_dirty  = cpu_to_le64(c->lst.total_dirty);
	mst->total_used   = cpu_to_le64(c->lst.total_used);
	mst->total_dead   = cpu_to_le64(c->lst.total_dead);
	mst->total_dark   = cpu_to_le64(c->lst.total_dark);
	mst->flags	  |= cpu_to_le32(UBIFS_MST_NO_ORPHS);

	lnum = UBIFS_MST_LNUM;
	err = ubifs_leb_unmap(c, lnum);
	if (err)
		goto out;
	err = ubifs_write_node_hmac(c, mst, UBIFS_MST_NODE_SZ, lnum, 0,
				    offsetof(struct ubifs_mst_node, hmac));
	if (err)
		goto out;
	lnum++;
	err = ubifs_leb_unmap(c, lnum);
	if (err)
		goto out;
	err = ubifs_write_node_hmac(c, mst, UBIFS_MST_NODE_SZ, lnum, 0,
				    offsetof(struct ubifs_mst_node, hmac));
	if (err)
		goto out;

out:
	kfree(mst);

	return err;
}

/**
 * ubifs_rebuild_filesystem - Rebuild filesystem.
 * @c: UBIFS file-system description object
 *
 * Scanning nodes from UBI volume and rebuild filesystem. Any inconsistent
 * problems or corrupted data will be fixed.
 */
int ubifs_rebuild_filesystem(struct ubifs_info *c)
{
	int err = 0;
	struct scanned_info si;

	si.valid_inos = si.del_inos = si.valid_dents = si.del_dents = RB_ROOT;
	log_out(c, "Start rebuilding filesystem (Notice: dropping data/recovering deleted data can't be awared)");
	FSCK(c)->mode = REBUILD_MODE;

	err = init_rebuild_info(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		return err;
	}

	/* Step 1: Scan valid/deleted nodes from volume. */
	log_out(c, "Scan nodes");
	err = scan_nodes(c, &si);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out;
	}

	/* Step 2: Remove deleted nodes from valid node tree. */
	log_out(c, "Remove deleted nodes");
	remove_del_nodes(c, &si);

	/* Step 3: Add valid nodes into file. */
	log_out(c, "Add valid nodes into file");
	err = add_valid_nodes_into_file(c, &si);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out;
	}

	/* Step 4: Drop invalid files. */
	log_out(c, "Filter invalid files");
	filter_invalid_files(c);

	/* Step 5: Extract reachable directory entries. */
	log_out(c, "Extract reachable files");
	extract_dentry_tree(c);

	/* Step 6: Check & correct files' information. */
	log_out(c, "Check & correct file information");
	err = check_and_correct_files(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out;
	}

	/*
	 * Step 7: Record used LEBs.
	 * Step 8: Re-write data to clean corrupted data.
	 * Step 9: Build TNC.
	 */
	err = traverse_files_and_nodes(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out;
	}

	/* Step 10. Build LPT. */
	log_out(c, "Build LPT");
	err = build_lpt(c, calculate_lp, true);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out;
	}

	/* Step 11. Clean up log & orphan. */
	log_out(c, "Clean up log & orphan");
	err = clean_log(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out;
	}
	err = ubifs_clear_orphans(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out;
	}

	/* Step 12. Write master node. */
	log_out(c, "Write master");
	err = write_master(c);
	if (err)
		exit_code |= FSCK_ERROR;

out:
	destroy_scanned_info(c, &si);
	destroy_rebuild_info(c);

	return err;
}
