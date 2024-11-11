// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Huawei Technologies Co, Ltd.
 *
 * Authors: Zhihao Cheng <chengzhihao1@huawei.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "linux_err.h"
#include "bitops.h"
#include "kmem.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "fsck.ubifs.h"

struct invalid_node {
	union ubifs_key key;
	int lnum;
	int offs;
	struct list_head list;
};

struct iteration_info {
	struct list_head invalid_nodes;
	unsigned long *corrupted_lebs;
};

static int add_invalid_node(struct ubifs_info *c, union ubifs_key *key,
			    int lnum, int offs, struct iteration_info *iter)
{
	struct invalid_node *in;

	in = kmalloc(sizeof(struct invalid_node), GFP_KERNEL);
	if (!in) {
		log_err(c, errno, "can not allocate invalid node");
		return -ENOMEM;
	}

	key_copy(c, key, &in->key);
	in->lnum = lnum;
	in->offs = offs;
	list_add(&in->list, &iter->invalid_nodes);

	return 0;
}

static int construct_file(struct ubifs_info *c, union ubifs_key *key,
			  int lnum, int offs, void *node,
			  struct iteration_info *iter)
{
	ino_t inum = 0;
	struct rb_root *tree = &FSCK(c)->scanned_files;
	struct scanned_node *sn = NULL;
	struct ubifs_ch *ch = (struct ubifs_ch *)node;

	switch (ch->node_type) {
	case UBIFS_INO_NODE:
	{
		struct scanned_ino_node ino_node;

		if (!parse_ino_node(c, lnum, offs, node, key, &ino_node)) {
			if (fix_problem(c, INVALID_INO_NODE, NULL))
				return add_invalid_node(c, key, lnum, offs, iter);
		}
		inum = key_inum(c, key);
		sn = (struct scanned_node *)&ino_node;
		break;
	}
	case UBIFS_DENT_NODE:
	case UBIFS_XENT_NODE:
	{
		struct scanned_dent_node dent_node;

		if (!parse_dent_node(c, lnum, offs, node, key, &dent_node)) {
			if (fix_problem(c, INVALID_DENT_NODE, NULL))
				return add_invalid_node(c, key, lnum, offs, iter);
		}
		inum = dent_node.inum;
		sn = (struct scanned_node *)&dent_node;
		break;
	}
	case UBIFS_DATA_NODE:
	{
		struct scanned_data_node data_node;

		if (!parse_data_node(c, lnum, offs, node, key, &data_node)) {
			if (fix_problem(c, INVALID_DATA_NODE, NULL))
				return add_invalid_node(c, key, lnum, offs, iter);
		}
		inum = key_inum(c, key);
		sn = (struct scanned_node *)&data_node;
		break;
	}
	default:
		ubifs_assert(c, 0);
	}

	dbg_fsck("construct file(%lu) for %s node, TNC location %d:%d, in %s",
		 inum, ubifs_get_key_name(key_type(c, key)), sn->lnum, sn->offs,
		 c->dev_name);
	return insert_or_update_file(c, tree, sn, key_type(c, key), inum);
}

static int scan_check_leb(struct ubifs_info *c, int lnum, bool is_idx)
{
	int err = 0;
	struct ubifs_scan_leb *sleb;
	struct ubifs_scan_node *snod;

	if (FSCK(c)->mode == CHECK_MODE)
		/* Skip check mode. */
		return 0;

	ubifs_assert(c, lnum >= c->main_first);
	if (test_bit(lnum - c->main_first, FSCK(c)->used_lebs))
		return 0;

	sleb = ubifs_scan(c, lnum, 0, c->sbuf, 0);
	if (IS_ERR(sleb)) {
		err = PTR_ERR(sleb);
		if (test_and_clear_failure_reason_callback(c, FR_DATA_CORRUPTED))
			err = 1;
		return err;
	}

	list_for_each_entry(snod, &sleb->nodes, list) {
		if (is_idx) {
			if (snod->type != UBIFS_IDX_NODE) {
				err = 1;
				goto out;
			}
		} else {
			if (snod->type == UBIFS_IDX_NODE) {
				err = 1;
				goto out;
			}
		}
	}

	set_bit(lnum - c->main_first, FSCK(c)->used_lebs);

out:
	ubifs_scan_destroy(sleb);
	return err;
}

static int check_leaf(struct ubifs_info *c, struct ubifs_zbranch *zbr,
		      void *priv)
{
	void *node;
	struct iteration_info *iter = (struct iteration_info *)priv;
	union ubifs_key *key = &zbr->key;
	int lnum = zbr->lnum, offs = zbr->offs, len = zbr->len, err = 0;

	if (len < UBIFS_CH_SZ) {
		ubifs_err(c, "bad leaf length %d (LEB %d:%d)",
			  len, lnum, offs);
		set_failure_reason_callback(c, FR_TNC_CORRUPTED);
		return -EINVAL;
	}
	if (key_type(c, key) != UBIFS_INO_KEY &&
	    key_type(c, key) != UBIFS_DATA_KEY &&
	    key_type(c, key) != UBIFS_DENT_KEY &&
	    key_type(c, key) != UBIFS_XENT_KEY) {
		ubifs_err(c, "bad key type %d (LEB %d:%d)",
			  key_type(c, key), lnum, offs);
		set_failure_reason_callback(c, FR_TNC_CORRUPTED);
		return -EINVAL;
	}

	if (test_bit(lnum - c->main_first, iter->corrupted_lebs)) {
		if (fix_problem(c, SCAN_CORRUPTED, zbr))
			/* All nodes in corrupted LEB should be removed. */
			return add_invalid_node(c, key, lnum, offs, iter);
		return 0;
	}

	err = scan_check_leb(c, lnum, false);
	if (err < 0) {
		return err;
	} else if (err) {
		set_bit(lnum - c->main_first, iter->corrupted_lebs);
		if (fix_problem(c, SCAN_CORRUPTED, zbr))
			return add_invalid_node(c, key, lnum, offs, iter);
		return 0;
	}

	node = kmalloc(len, GFP_NOFS);
	if (!node)
		return -ENOMEM;

	err = ubifs_tnc_read_node(c, zbr, node);
	if (err) {
		if (test_and_clear_failure_reason_callback(c, FR_DATA_CORRUPTED)) {
			if (fix_problem(c, TNC_DATA_CORRUPTED, NULL))
				err = add_invalid_node(c, key, lnum, offs, iter);
		}
		goto out;
	}

	err = construct_file(c, key, lnum, offs, node, iter);

out:
	kfree(node);
	return err;
}

static int check_znode(struct ubifs_info *c, struct ubifs_znode *znode,
		       __unused void *priv)
{
	int err;
	const struct ubifs_zbranch *zbr;

	if (znode->parent)
		zbr = &znode->parent->zbranch[znode->iip];
	else
		zbr = &c->zroot;

	if (zbr->lnum == 0) {
		/* The znode has been split up. */
		ubifs_assert(c, zbr->offs == 0 && zbr->len == 0);
		return 0;
	}

	err = scan_check_leb(c, zbr->lnum, true);
	if (err < 0) {
		return err;
	} else if (err) {
		set_failure_reason_callback(c, FR_TNC_CORRUPTED);
		return -EINVAL;
	}

	return 0;
}

static int remove_invalid_nodes(struct ubifs_info *c,
				struct list_head *invalid_nodes, int error)
{
	int ret = 0;;
	struct invalid_node *in;

	while (!list_empty(invalid_nodes)) {
		in = list_entry(invalid_nodes->next, struct invalid_node, list);

		if (!error) {
			error = ubifs_tnc_remove_node(c, &in->key, in->lnum, in->offs);
			if (error) {
				/* TNC traversing is finished, any TNC path is accessible */
				ubifs_assert(c, !get_failure_reason_callback(c));
				ret = error;
			}
		}

		list_del(&in->list);
		kfree(in);
	}

	return ret;
}

/**
 * traverse_tnc_and_construct_files - traverse TNC and construct all files.
 * @c: UBIFS file-system description object
 *
 * This function does two things by traversing TNC:
 * 1. Check all index nodes and non-index nodes, then construct file according
 *    to scanned non-index nodes and insert file into file tree.
 * 2. Make sure that LEB(contains any nodes from TNC) can be scanned by
 *    ubifs_scan, and the LEB only contains index nodes or non-index nodes.
 * Returns zero in case of success, a negative error code in case of failure.
 */
int traverse_tnc_and_construct_files(struct ubifs_info *c)
{
	int err, ret;
	struct iteration_info iter;

	FSCK(c)->scanned_files = RB_ROOT;
	FSCK(c)->used_lebs = kcalloc(BITS_TO_LONGS(c->main_lebs),
				     sizeof(unsigned long), GFP_KERNEL);
	if (!FSCK(c)->used_lebs) {
		err = -ENOMEM;
		log_err(c, errno, "can not allocate bitmap of used lebs");
		return err;
	}
	INIT_LIST_HEAD(&iter.invalid_nodes);
	iter.corrupted_lebs = kcalloc(BITS_TO_LONGS(c->main_lebs),
				      sizeof(unsigned long), GFP_KERNEL);
	if (!iter.corrupted_lebs) {
		err = -ENOMEM;
		log_err(c, errno, "can not allocate bitmap of corrupted lebs");
		goto out;
	}

	err = dbg_walk_index(c, check_leaf, check_znode, &iter);

	ret = remove_invalid_nodes(c, &iter.invalid_nodes, err);
	if (!err)
		err = ret;

	kfree(iter.corrupted_lebs);
out:
	if (err) {
		kfree(FSCK(c)->used_lebs);
		destroy_file_tree(c, &FSCK(c)->scanned_files);
	}
	return err;
}

/**
 * update_files_size - Update files' size.
 * @c: UBIFS file-system description object
 *
 * This function updates files' size according to @c->size_tree for check mode.
 */
void update_files_size(struct ubifs_info *c)
{
	struct rb_node *this;

	if (FSCK(c)->mode != CHECK_MODE) {
		/* Other modes(rw) have updated inode size in place. */
		dbg_fsck("skip updating files' size%s, in %s",
			 mode_name(c), c->dev_name);
		return;
	}

	log_out(c, "Update files' size");

	this = rb_first(&c->size_tree);
	while (this) {
		struct size_entry *e;

		e = rb_entry(this, struct size_entry, rb);
		this = rb_next(this);

		if (e->exists && e->i_size < e->d_size) {
			struct scanned_file *file;

			file = lookup_file(&FSCK(c)->scanned_files, e->inum);
			if (file && file->ino.header.exist &&
			    file->ino.size < e->d_size) {
				dbg_fsck("update file(%lu) size %llu->%llu, in %s",
					 e->inum, file->ino.size,
					 (unsigned long long)e->d_size,
					 c->dev_name);
				file->ino.size = e->d_size;
			}
		}

		rb_erase(&e->rb, &c->size_tree);
		kfree(e);
	}
}

/**
 * handle_invalid_files - Handle invalid files.
 * @c: UBIFS file-system description object
 *
 * This function checks and handles invalid files, there are three situations:
 * 1. Move unattached(file has no dentries, or file's parent file has invalid
 *    type) regular file into disconnected list, let subsequent steps to handle
 *    them with lost+found.
 * 2. Make file type be consistent between inode, detries and data nodes by
 *    deleting dentries or data blocks.
 * 3. Delete file for other invalid cases(eg. file has no inode).
 *
 * Returns zero in case of success, a negative error code in case of failure.
 */
int handle_invalid_files(struct ubifs_info *c)
{
	int err;
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
		err = file_is_valid(c, file, tree, NULL);
		if (err < 0) {
			destroy_file_content(c, file);
			kfree(file);
			return err;
		} else if (!err) {
			err = delete_file(c, file);
			kfree(file);
			if (err)
				return err;
		}
	}

	/* Round 2: Traverse non-xattr files. */
	for (node = rb_first(tree); node; node = rb_next(node)) {
		int is_diconnected = 0;

		file = rb_entry(node, struct scanned_file, rb);
		err = file_is_valid(c, file, tree, &is_diconnected);
		if (err < 0) {
			return err;
		} else if (!err) {
			if (is_diconnected)
				list_add(&file->list, &FSCK(c)->disconnected_files);
			else
				list_add(&file->list, &tmp_list);
		}
	}

	/* Delete & remove invalid files. */
	while (!list_empty(&tmp_list)) {
		file = list_entry(tmp_list.next, struct scanned_file, list);

		list_del(&file->list);
		err = delete_file(c, file);
		if (err)
			return err;
		rb_erase(&file->rb, tree);
		kfree(file);
	}

	/* Remove disconnected file from the file tree. */
	list_for_each_entry(file, &FSCK(c)->disconnected_files, list) {
		rb_erase(&file->rb, tree);
	}

	return 0;
}

/**
 * handle_dentry_tree - Handle unreachable dentries and files.
 * @c: UBIFS file-system description object
 *
 * This function iterates all directory entries and remove those unreachable
 * ones. If file has no directory entries, it becomes unreachable:
 * 1. If the unreachable file has non-regular type, delete it;
 * 2. If the unreachable file has regular type, move it into the
 *    @FSCK(c)->disconnected_files.
 * 'Unreachable' means that a directory entry can not be searched from '/'.
 *
 * Returns zero in case of success, a negative error code in case of failure.
 */
int handle_dentry_tree(struct ubifs_info *c)
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

	while (!list_empty(&unreachable)) {
		file = list_entry(unreachable.next, struct scanned_file, list);

		list_del(&file->list);
		if (S_ISREG(file->ino.mode)) {
			/*
			 * Move regular type unreachable file into the
			 * @FSCK(c)->disconnected_files.
			 */
			list_add(&file->list, &FSCK(c)->disconnected_files);
			rb_erase(&file->rb, tree);
		} else {
			/* Delete non-regular type unreachable file. */
			int err = delete_file(c, file);
			if (err)
				return err;
			rb_erase(&file->rb, tree);
			kfree(file);
		}
	}

	return 0;
}

/**
 * tnc_is_empty - Check whether the TNC is empty.
 * @c: UBIFS file-system description object
 *
 * Returns %true if the TNC is empty, otherwise %false is returned.
 */
bool tnc_is_empty(struct ubifs_info *c)
{
	/*
	 * Check whether the TNC is empty, turn to rebuild_fs if it is empty.
	 * Can we recreate a new root dir to avoid empty TNC? The answer is no,
	 * lpt fixing should be done before creating new entry, but lpt fixing
	 * needs a committing before new dirty data generated to ensure that
	 * bud data won't be overwritten(bud LEB could become freeable after
	 * replaying journal, corrected lpt may treat it as a free one to hold
	 * new data, see details in space checking & correcting step). Then we
	 * have to create the new root dir after fixing lpt and a committing,
	 * znode without children(empty TNC) maybe written on disk at the
	 * moment of committing, which corrupts the UBIFS image. So we choose
	 * to rebuild the filesystem if the TNC is empty, this case is
	 * equivalent to corrupted TNC.
	 */
	return c->zroot.znode->child_cnt == 0;
}

/**
 * check_and_create_root - Check and create root dir.
 * @c: UBIFS file-system description object
 *
 * This function checks whether the root dir is existed, create a new root
 * dir if it doesn't exist. Returns zero in case of success, a negative error
 * code in case of failure.
 */
int check_and_create_root(struct ubifs_info *c)
{
	int err;
	struct ubifs_inode *ui = ubifs_lookup_by_inum(c, UBIFS_ROOT_INO);

	if (!IS_ERR(ui)) {
		/* The root dir is found. */
		dbg_fsck("root dir is found, in %s", c->dev_name);
		kfree(ui);
		return 0;
	}

	err = PTR_ERR(ui);
	if (err != -ENOENT)
		return err;

	fix_problem(c, ROOT_DIR_NOT_FOUND, NULL);
	dbg_fsck("root dir is lost, create a new one, in %s", c->dev_name);
	return ubifs_create_root(c);
}
