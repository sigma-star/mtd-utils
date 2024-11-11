// SPDX-License-Identifier: GPL-2.0-only
/* * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 * Copyright (C) 2006, 2007 University of Szeged, Hungary
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 *          Zoltan Sogor
 */

/*
 * This file implements directory operations.
 *
 * All FS operations in this file allocate budget before writing anything to the
 * media. If they fail to allocate it, the error is returned. The only
 * exceptions are 'ubifs_unlink()' and 'ubifs_rmdir()' which keep working even
 * if they unable to allocate the budget, because deletion %-ENOSPC failure is
 * not what users are usually ready to get. UBIFS budgeting subsystem has some
 * space reserved for these purposes.
 *
 * All operations in this file write all inodes which they change straight
 * away, instead of marking them dirty. For example, 'ubifs_link()' changes
 * @i_size of the parent inode and writes the parent inode together with the
 * target inode. This was done to simplify file-system recovery which would
 * otherwise be very difficult to do. The only exception is rename which marks
 * the re-named inode dirty (because its @i_ctime is updated) but does not
 * write it, but just marks it as dirty.
 */

#include <sys/stat.h>

#include "linux_err.h"
#include "bitops.h"
#include "kmem.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "misc.h"

/**
 * inherit_flags - inherit flags of the parent inode.
 * @c: UBIFS file-system description object
 * @dir: parent inode
 * @mode: new inode mode flags
 *
 * This is a helper function for 'ubifs_new_inode()' which inherits flag of the
 * parent directory inode @dir. UBIFS inodes inherit the following flags:
 * o %UBIFS_COMPR_FL, which is useful to switch compression on/of on
 *   sub-directory basis;
 * o %UBIFS_SYNC_FL - useful for the same reasons;
 * o %UBIFS_DIRSYNC_FL - similar, but relevant only to directories.
 *
 * This function returns the inherited flags.
 */
static int inherit_flags(struct ubifs_info *c, const struct inode *dir,
			 unsigned int mode)
{
	int flags;
	const struct ubifs_inode *ui = ubifs_inode(dir);

	ubifs_assert(c, S_ISDIR(dir->mode));

	flags = ui->flags & (UBIFS_COMPR_FL | UBIFS_SYNC_FL | UBIFS_DIRSYNC_FL);
	if (!S_ISDIR(mode))
		/* The "DIRSYNC" flag only applies to directories */
		flags &= ~UBIFS_DIRSYNC_FL;
	return flags;
}

/**
 * ubifs_new_inode - allocate new UBIFS inode object.
 * @c: UBIFS file-system description object
 * @dir: parent inode
 * @mode: inode mode flags
 *
 * This function finds an unused inode number, allocates new ubifs inode and
 * initializes it. Returns new ubifs inode in case of success and an error code
 * in case of failure.
 */
static struct ubifs_inode *ubifs_new_inode(struct ubifs_info *c,
					   const struct inode *dir,
					   unsigned int mode)
{
	int err;
	time_t now = time(NULL);
	struct ubifs_inode *ui;
	struct inode *inode;

	ui = kzalloc(sizeof(struct ubifs_inode), GFP_KERNEL);
	if (!ui)
		return ERR_PTR(-ENOMEM);

	inode = &ui->vfs_inode;
	inode->atime_sec = inode->ctime_sec = inode->mtime_sec = now;
	inode->nlink = 1;
	inode->mode = mode;
	if (dir) {
		/* Create non root dir. */
		inode->uid = dir->uid;
		inode->gid = dir->gid;
		if ((dir->mode & S_ISGID) && S_ISDIR(mode))
			inode->mode |= S_ISGID;
		ui->flags = inherit_flags(c, dir, mode);
	}
	if (S_ISDIR(mode))
		ui->ui_size = UBIFS_INO_NODE_SZ;
	if (S_ISREG(mode))
		ui->compr_type = c->default_compr;
	else
		ui->compr_type = UBIFS_COMPR_NONE;

	if (dir) {
		spin_lock(&c->cnt_lock);
		/* Inode number overflow is currently not supported */
		if (c->highest_inum >= INUM_WARN_WATERMARK) {
			if (c->highest_inum >= INUM_WATERMARK) {
				spin_unlock(&c->cnt_lock);
				ubifs_err(c, "out of inode numbers");
				err = -EINVAL;
				goto out;
			}
			ubifs_warn(c, "running out of inode numbers (current %lu, max %u)",
				   (unsigned long)c->highest_inum, INUM_WATERMARK);
		}
		inode->inum = ++c->highest_inum;
	} else {
		/* Create root dir. */
		inode->inum = UBIFS_ROOT_INO;
	}
	/*
	 * The creation sequence number remains with this inode for its
	 * lifetime. All nodes for this inode have a greater sequence number,
	 * and so it is possible to distinguish obsolete nodes belonging to a
	 * previous incarnation of the same inode number - for example, for the
	 * purpose of rebuilding the index.
	 */
	ui->creat_sqnum = ++c->max_sqnum;
	spin_unlock(&c->cnt_lock);

	return ui;

out:
	kfree(ui);
	return ERR_PTR(err);
}

/**
 * ubifs_lookup_by_inum - look up the UBIFS inode according to inode number.
 * @c: UBIFS file-system description object
 * @inum: inode number
 *
 * This function looks up the UBIFS inode according to a given inode number.
 * Returns zero in case of success and an error code in case of failure.
 */
struct ubifs_inode *ubifs_lookup_by_inum(struct ubifs_info *c, ino_t inum)
{
	int err;
	union ubifs_key key;
	struct inode *inode;
	struct ubifs_inode *ui;
	struct ubifs_ino_node *ino = NULL;

	ino = kmalloc(UBIFS_MAX_INO_NODE_SZ, GFP_NOFS);
	if (!ino)
		return ERR_PTR(-ENOMEM);

	ui = kzalloc(sizeof(struct ubifs_inode), GFP_KERNEL);
	if (!ui) {
		err = -ENOMEM;
		goto out;
	}

	inode = &ui->vfs_inode;
	ino_key_init(c, &key, inum);
	err = ubifs_tnc_lookup(c, &key, ino);
	if (err) {
		kfree(ui);
		ubifs_assert(c, !get_failure_reason_callback(c));
		goto out;
	}

	inode = &ui->vfs_inode;
	inode->inum = inum;
	inode->uid = le32_to_cpu(ino->uid);
	inode->gid = le32_to_cpu(ino->gid);
	inode->mode = le32_to_cpu(ino->mode);
	inode->nlink = le32_to_cpu(ino->nlink);
	inode->atime_sec = le64_to_cpu(ino->atime_sec);
	inode->ctime_sec = le64_to_cpu(ino->ctime_sec);
	inode->mtime_sec = le64_to_cpu(ino->mtime_sec);
	inode->atime_nsec = le32_to_cpu(ino->atime_nsec);
	inode->ctime_nsec = le32_to_cpu(ino->ctime_nsec);
	inode->mtime_nsec = le32_to_cpu(ino->mtime_nsec);
	ui->creat_sqnum = le64_to_cpu(ino->creat_sqnum);
	ui->xattr_size = le32_to_cpu(ino->xattr_size);
	ui->xattr_cnt = le32_to_cpu(ino->xattr_cnt);
	ui->xattr_names = le32_to_cpu(ino->xattr_names);
	ui->compr_type = le16_to_cpu(ino->compr_type);
	ui->ui_size = le64_to_cpu(ino->size);
	ui->flags = le32_to_cpu(ino->flags);
	ui->data_len = le32_to_cpu(ino->data_len);

out:
	kfree(ino);
	return err ? ERR_PTR(err) : ui;
}

struct ubifs_inode *ubifs_lookup(struct ubifs_info *c,
				 struct ubifs_inode *dir_ui,
				 const struct fscrypt_name *nm)
{
	int err;
	ino_t inum;
	union ubifs_key key;
	struct ubifs_dent_node *dent;

	if (fname_len(nm) > UBIFS_MAX_NLEN)
		return ERR_PTR(-ENAMETOOLONG);

	dent = kmalloc(UBIFS_MAX_DENT_NODE_SZ, GFP_NOFS);
	if (!dent)
		return ERR_PTR(-ENOMEM);

	dent_key_init(c, &key, dir_ui->vfs_inode.inum, nm);
	err = ubifs_tnc_lookup_nm(c, &key, dent, nm);
	if (err) {
		kfree(dent);
		ubifs_assert(c, !get_failure_reason_callback(c));
		return ERR_PTR(err);
	}
	inum = le64_to_cpu(dent->inum);
	kfree(dent);

	return ubifs_lookup_by_inum(c, inum);
}

int ubifs_mkdir(struct ubifs_info *c, struct ubifs_inode *dir_ui,
		const struct fscrypt_name *nm, unsigned int mode)
{
	struct ubifs_inode *ui;
	struct inode *inode, *dir = &dir_ui->vfs_inode;
	int err, sz_change;
	struct ubifs_budget_req req = { .new_ino = 1, .new_dent = 1,
					.dirtied_ino = 1};
	/*
	 * Budget request settings: new inode, new direntry and changing parent
	 * directory inode.
	 */
	dbg_gen("dent '%s', mode %#hx in dir ino %lu",
		fname_name(nm), mode, dir->inum);

	/* New dir is not allowed to be created under an encrypted directory. */
	ubifs_assert(c, !(dir_ui->flags & UBIFS_CRYPT_FL));

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	sz_change = CALC_DENT_SIZE(fname_len(nm));

	ui = ubifs_new_inode(c, dir, S_IFDIR | mode);
	if (IS_ERR(ui)) {
		err = PTR_ERR(ui);
		goto out_budg;
	}

	inode = &ui->vfs_inode;
	inode->nlink++;
	dir->nlink++;
	dir_ui->ui_size += sz_change;
	dir->ctime_sec = dir->mtime_sec = inode->ctime_sec;
	err = ubifs_jnl_update_file(c, dir_ui, nm, ui);
	if (err) {
		ubifs_err(c, "cannot create directory, error %d", err);
		goto out_cancel;
	}

	kfree(ui);
	ubifs_release_budget(c, &req);
	return 0;

out_cancel:
	dir_ui->ui_size -= sz_change;
	dir->nlink--;
	kfree(ui);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

/**
 * ubifs_link_recovery - link a disconnected file into the target directory.
 * @c: UBIFS file-system description object
 * @dir_ui: target directory
 * @ui: the UBIFS inode of disconnected file
 * @nm: directory entry name
 *
 * This function links the inode of disconnected file to a directory entry name
 * under the target directory. Returns zero in case of success and an error code
 * in case of failure.
 */
int ubifs_link_recovery(struct ubifs_info *c, struct ubifs_inode *dir_ui,
			struct ubifs_inode *ui, const struct fscrypt_name *nm)
{
	struct inode *inode = &ui->vfs_inode, *dir = &dir_ui->vfs_inode;
	int err, sz_change;
	struct ubifs_budget_req req = { .new_dent = 1, .dirtied_ino = 2,
				.dirtied_ino_d = ALIGN(ui->data_len, 8) };
	time_t now = time(NULL);

	/*
	 * Budget request settings: new direntry, changing the target inode,
	 * changing the parent inode.
	 */
	dbg_gen("dent '%s' to ino %lu (nlink %d) in dir ino %lu",
		fname_name(nm), inode->inum, inode->nlink, dir->inum);

	/* New dir is not allowed to be created under an encrypted directory. */
	ubifs_assert(c, !(dir_ui->flags & UBIFS_CRYPT_FL));

	sz_change = CALC_DENT_SIZE(fname_len(nm));

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	inode->ctime_sec = now;
	dir_ui->ui_size += sz_change;
	dir->ctime_sec = dir->mtime_sec = now;
	err = ubifs_jnl_update_file(c, dir_ui, nm, ui);
	if (err)
		goto out_cancel;

	ubifs_release_budget(c, &req);
	return 0;

out_cancel:
	dir_ui->ui_size -= sz_change;
	ubifs_release_budget(c, &req);
	return err;
}

/**
 * ubifs_create_root - create the root inode.
 * @c: UBIFS file-system description object
 *
 * This function creates a new inode for the root directory. Returns zero in
 * case of success and an error code in case of failure.
 */
int ubifs_create_root(struct ubifs_info *c)
{
	int err;
	struct inode *inode;
	struct ubifs_budget_req req = { .new_ino = 1 };
	struct ubifs_inode *ui;

	/* Budget request settings: new inode. */
	dbg_gen("create root dir");

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	ui = ubifs_new_inode(c, NULL, S_IFDIR | S_IRUGO | S_IWUSR | S_IXUGO);
	if (IS_ERR(ui)) {
		err = PTR_ERR(ui);
		goto out_budg;
	}

	inode = &ui->vfs_inode;
	inode->nlink = 2;
	ui->ui_size = UBIFS_INO_NODE_SZ;
	ui->flags = UBIFS_COMPR_FL;
	err = ubifs_jnl_update_file(c, NULL, NULL, ui);
	if (err)
		goto out_ui;

	kfree(ui);
	ubifs_release_budget(c, &req);
	return 0;

out_ui:
	kfree(ui);
out_budg:
	ubifs_release_budget(c, &req);
	ubifs_err(c, "cannot create root dir, error %d", err);
	return err;
}
