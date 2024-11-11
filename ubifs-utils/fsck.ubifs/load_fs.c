// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Huawei Technologies Co, Ltd.
 *
 * Authors: Zhihao Cheng <chengzhihao1@huawei.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include "bitops.h"
#include "kmem.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "misc.h"
#include "fsck.ubifs.h"

int ubifs_load_filesystem(struct ubifs_info *c)
{
	int err;
	size_t sz;

	err = init_constants_early(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		return err;
	}

	err = check_volume_empty(c);
	if (err <= 0) {
		exit_code |= FSCK_ERROR;
		log_err(c, 0, "%s UBI volume!", err < 0 ? "bad" : "empty");
		return -EINVAL;
	}

	if (c->ro_media && !c->ro_mount) {
		exit_code |= FSCK_ERROR;
		log_err(c, 0, "cannot read-write on read-only media");
		return -EROFS;
	}

	err = -ENOMEM;
	c->bottom_up_buf = kmalloc_array(BOTTOM_UP_HEIGHT, sizeof(int),
					 GFP_KERNEL);
	if (!c->bottom_up_buf) {
		exit_code |= FSCK_ERROR;
		log_err(c, errno, "cannot allocate bottom_up_buf");
		goto out_free;
	}

	c->sbuf = vmalloc(c->leb_size);
	if (!c->sbuf) {
		exit_code |= FSCK_ERROR;
		log_err(c, errno, "cannot allocate sbuf");
		goto out_free;
	}

	if (!c->ro_mount) {
		c->ileb_buf = vmalloc(c->leb_size);
		if (!c->ileb_buf) {
			exit_code |= FSCK_ERROR;
			log_err(c, errno, "cannot allocate ileb_buf");
			goto out_free;
		}
	}

	c->mounting = 1;

	log_out(c, "Read superblock");
	err = ubifs_read_superblock(c);
	if (err) {
		if (test_and_clear_failure_reason_callback(c, FR_DATA_CORRUPTED))
			fix_problem(c, SB_CORRUPTED, NULL);
		exit_code |= FSCK_ERROR;
		goto out_mounting;
	}

	err = init_constants_sb(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out_mounting;
	}

	sz = ALIGN(c->max_idx_node_sz, c->min_io_size) * 2;
	c->cbuf = kmalloc(sz, GFP_NOFS);
	if (!c->cbuf) {
		err = -ENOMEM;
		exit_code |= FSCK_ERROR;
		log_err(c, errno, "cannot allocate cbuf");
		goto out_mounting;
	}

	err = alloc_wbufs(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		log_err(c, 0, "cannot allocate wbuf");
		goto out_mounting;
	}

	log_out(c, "Read master & init lpt");
	err = ubifs_read_master(c);
	if (err) {
		if (test_and_clear_failure_reason_callback(c, FR_DATA_CORRUPTED)) {
			if (fix_problem(c, MST_CORRUPTED, NULL))
				FSCK(c)->try_rebuild = true;
		} else
			exit_code |= FSCK_ERROR;
		goto out_master;
	}

	init_constants_master(c);

	if ((c->mst_node->flags & cpu_to_le32(UBIFS_MST_DIRTY)) != 0) {
		ubifs_msg(c, "recovery needed");
		c->need_recovery = 1;
	}

	if (c->need_recovery && !c->ro_mount) {
		err = ubifs_recover_inl_heads(c, c->sbuf);
		if (err) {
			exit_code |= FSCK_ERROR;
			goto out_master;
		}
	}

	err = ubifs_lpt_init(c, 1, !c->ro_mount);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out_master;
	}

	if (!c->ro_mount && c->space_fixup) {
		err = ubifs_fixup_free_space(c);
		if (err) {
			exit_code |= FSCK_ERROR;
			goto out_lpt;
		}
	}

	if (!c->ro_mount && !c->need_recovery) {
		/*
		 * Set the "dirty" flag so that if we reboot uncleanly we
		 * will notice this immediately on the next mount.
		 */
		c->mst_node->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
		err = ubifs_write_master(c);
		if (err) {
			exit_code |= FSCK_ERROR;
			goto out_lpt;
		}
	}

	if (!c->ro_mount && c->superblock_need_write) {
		err = ubifs_write_sb_node(c, c->sup_node);
		if (err) {
			exit_code |= FSCK_ERROR;
			goto out_lpt;
		}
		c->superblock_need_write = 0;
	}

	log_out(c, "Replay journal");
	err = ubifs_replay_journal(c);
	if (err) {
		handle_error(c, HAS_DATA_CORRUPTED | HAS_TNC_CORRUPTED);
		goto out_journal;
	}

	/* Calculate 'min_idx_lebs' after journal replay */
	c->bi.min_idx_lebs = ubifs_calc_min_idx_lebs(c);

	log_out(c, "Handle orphan nodes");
	err = ubifs_mount_orphans(c, c->need_recovery, c->ro_mount);
	if (err) {
		handle_error(c, HAS_TNC_CORRUPTED);
		goto out_orphans;
	}

	if (!c->ro_mount) {
		int lnum;

		/* Check for enough log space */
		lnum = c->lhead_lnum + 1;
		if (lnum >= UBIFS_LOG_LNUM + c->log_lebs)
			lnum = UBIFS_LOG_LNUM;
		if (lnum == c->ltail_lnum) {
			log_out(c, "Consolidate log");
			err = ubifs_consolidate_log(c);
			if (err) {
				handle_error(c, HAS_DATA_CORRUPTED);
				goto out_orphans;
			}
		}

		if (c->need_recovery) {
			log_out(c, "Recover isize");
			err = ubifs_recover_size(c, true);
			if (err) {
				handle_error(c, HAS_TNC_CORRUPTED);
				goto out_orphans;
			}
		}
	} else if (c->need_recovery) {
		log_out(c, "Recover isize");
		err = ubifs_recover_size(c, false);
		if (err) {
			handle_error(c, HAS_TNC_CORRUPTED);
			goto out_orphans;
		}
	}

	c->mounting = 0;

	return 0;

out_orphans:
	free_orphans(c);
out_journal:
	destroy_journal(c);
out_lpt:
	ubifs_lpt_free(c, 0);
out_master:
	c->max_sqnum = 0;
	c->highest_inum = 0;
	c->calc_idx_sz = 0;
	kfree(c->mst_node);
	kfree(c->rcvrd_mst_node);
	free_wbufs(c);
out_mounting:
	c->mounting = 0;
out_free:
	kfree(c->cbuf);
	kfree(c->ileb_buf);
	kfree(c->sbuf);
	kfree(c->bottom_up_buf);
	kfree(c->sup_node);

	return err;
}

void ubifs_destroy_filesystem(struct ubifs_info *c)
{
	destroy_journal(c);
	free_wbufs(c);
	free_orphans(c);
	ubifs_lpt_free(c, 0);

	c->max_sqnum = 0;
	c->highest_inum = 0;
	c->calc_idx_sz = 0;

	kfree(c->cbuf);
	kfree(c->rcvrd_mst_node);
	kfree(c->mst_node);
	kfree(c->ileb_buf);
	kfree(c->sbuf);
	kfree(c->bottom_up_buf);
	kfree(c->sup_node);
}
