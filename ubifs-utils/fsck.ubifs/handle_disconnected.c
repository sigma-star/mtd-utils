// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Huawei Technologies Co, Ltd.
 *
 * Authors: Huang Xiaojia <huangxiaojia2@huawei.com>
 *          Zhihao Cheng <chengzhihao1@huawei.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "linux_err.h"
#include "kmem.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "fsck.ubifs.h"

#define LOST_FOUND_DIR_NAME "lost+found"

/**
 * check_and_create_lost_found - Check and create the lost+found directory.
 * @c: UBIFS file-system description object
 *
 * This function checks whether the lost+found directory exists and creates a
 * new one if no valid lost+found existing. If there is a valid lost+found
 * directory, inode number is stored in @FSCK(c)->lost_and_found. Returns zero
 * in case of success, a negative error code in case of failure.
 */
int check_and_create_lost_found(struct ubifs_info *c)
{
	struct ubifs_inode *root_ui, *lost_found_ui;
	struct fscrypt_name nm;
	int err = 0;

	root_ui = ubifs_lookup_by_inum(c, UBIFS_ROOT_INO);
	if (IS_ERR(root_ui)) {
		err = PTR_ERR(root_ui);
		/* Previous step ensures that the root dir is valid. */
		ubifs_assert(c, err != -ENOENT);
		return err;
	}

	if (root_ui->flags & UBIFS_CRYPT_FL) {
		ubifs_msg(c, "The root dir is encrypted, skip checking lost+found");
		goto free_root;
	}

	fname_name(&nm) = LOST_FOUND_DIR_NAME;
	fname_len(&nm) = strlen(LOST_FOUND_DIR_NAME);
	lost_found_ui = ubifs_lookup(c, root_ui, &nm);
	if (IS_ERR(lost_found_ui)) {
		err = PTR_ERR(lost_found_ui);
		if (err != -ENOENT)
			goto free_root;

		/* Not found. Create a new lost+found. */
		err = ubifs_mkdir(c, root_ui, &nm, S_IRUGO | S_IWUSR | S_IXUSR);
		if (err < 0) {
			if (err == -ENOSPC) {
				ubifs_msg(c, "No free space to create lost+found");
				err = 0;
			}
			goto free_root;
		}
		lost_found_ui = ubifs_lookup(c, root_ui, &nm);
		if (IS_ERR(lost_found_ui)) {
			err = PTR_ERR(lost_found_ui);
			ubifs_assert(c, err != -ENOENT);
			goto free_root;
		}
		FSCK(c)->lost_and_found = lost_found_ui->vfs_inode.inum;
		ubifs_msg(c, "Create the lost+found");
	} else if (!(lost_found_ui->flags & UBIFS_CRYPT_FL) &&
		   S_ISDIR(lost_found_ui->vfs_inode.mode)) {
		FSCK(c)->lost_and_found = lost_found_ui->vfs_inode.inum;
	} else {
		ubifs_msg(c, "The type of lost+found is %s%s",
			  ubifs_get_type_name(ubifs_get_dent_type(lost_found_ui->vfs_inode.mode)),
			  lost_found_ui->flags & UBIFS_CRYPT_FL ? ", encrypted" : "");
	}

	kfree(lost_found_ui);
free_root:
	kfree(root_ui);
	return err;
}
