// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements functions that manage the running of the commit process.
 * Each affected module has its own functions to accomplish their part in the
 * commit and those functions are called here.
 *
 * The commit is the process whereby all updates to the index and LEB properties
 * are written out together and the journal becomes empty. This keeps the
 * file system consistent - at all times the state can be recreated by reading
 * the index and LEB properties and then replaying the journal.
 *
 * The commit is split into two parts named "commit start" and "commit end".
 * During commit start, the commit process has exclusive access to the journal
 * by holding the commit semaphore down for writing. As few I/O operations as
 * possible are performed during commit start, instead the nodes that are to be
 * written are merely identified. During commit end, the commit semaphore is no
 * longer held and the journal is again in operation, allowing users to continue
 * to use the file system while the bulk of the commit I/O is performed. The
 * purpose of this two-step approach is to prevent the commit from causing any
 * latency blips. Note that in any case, the commit does not prevent lookups
 * (as permitted by the TNC mutex), or access to VFS data structures e.g. page
 * cache.
 */

#include "linux_err.h"
#include "bitops.h"
#include "kmem.h"
#include "ubifs.h"
#include "debug.h"
#include "defs.h"
#include "misc.h"

/*
 * nothing_to_commit - check if there is nothing to commit.
 * @c: UBIFS file-system description object
 *
 * This is a helper function which checks if there is anything to commit. It is
 * used as an optimization to avoid starting the commit if it is not really
 * necessary. Indeed, the commit operation always assumes flash I/O (e.g.,
 * writing the commit start node to the log), and it is better to avoid doing
 * this unnecessarily. E.g., 'ubifs_sync_fs()' runs the commit, but if there is
 * nothing to commit, it is more optimal to avoid any flash I/O.
 *
 * This function has to be called with @c->commit_sem locked for writing -
 * this function does not take LPT/TNC locks because the @c->commit_sem
 * guarantees that we have exclusive access to the TNC and LPT data structures.
 *
 * This function returns %1 if there is nothing to commit and %0 otherwise.
 */
static int nothing_to_commit(struct ubifs_info *c)
{
	/*
	 * During mounting or remounting from R/O mode to R/W mode we may
	 * commit for various recovery-related reasons.
	 */
	if (c->mounting || c->remounting_rw)
		return 0;

	/*
	 * If the root TNC node is dirty, we definitely have something to
	 * commit.
	 */
	if (c->zroot.znode && ubifs_zn_dirty(c->zroot.znode))
		return 0;

	/*
	 * Increasing @c->dirty_pn_cnt/@c->dirty_nn_cnt and marking
	 * nnodes/pnodes as dirty in run_gc() could race with following
	 * checking, which leads inconsistent states between @c->nroot
	 * and @c->dirty_pn_cnt/@c->dirty_nn_cnt, holding @c->lp_mutex
	 * to avoid that.
	 */
	mutex_lock(&c->lp_mutex);
	/*
	 * Even though the TNC is clean, the LPT tree may have dirty nodes. For
	 * example, this may happen if the budgeting subsystem invoked GC to
	 * make some free space, and the GC found an LEB with only dirty and
	 * free space. In this case GC would just change the lprops of this
	 * LEB (by turning all space into free space) and unmap it.
	 */
	if (c->nroot && test_bit(DIRTY_CNODE, &c->nroot->flags)) {
		mutex_unlock(&c->lp_mutex);
		return 0;
	}

	ubifs_assert(c, atomic_long_read(&c->dirty_zn_cnt) == 0);
	ubifs_assert(c, c->dirty_pn_cnt == 0);
	ubifs_assert(c, c->dirty_nn_cnt == 0);
	mutex_unlock(&c->lp_mutex);

	return 1;
}

/**
 * do_commit - commit the journal.
 * @c: UBIFS file-system description object
 *
 * This function implements UBIFS commit. It has to be called with commit lock
 * locked. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int do_commit(struct ubifs_info *c)
{
	int err, new_ltail_lnum, old_ltail_lnum, i;
	struct ubifs_zbranch zroot;
	struct ubifs_lp_stats lst;

	dbg_cmt("start");
	ubifs_assert(c, !c->ro_media && !c->ro_mount);

	if (c->ro_error) {
		err = -EROFS;
		goto out_up;
	}

	if (nothing_to_commit(c)) {
		up_write(&c->commit_sem);
		err = 0;
		goto out_cancel;
	}

	/* Sync all write buffers (necessary for recovery) */
	for (i = 0; i < c->jhead_cnt; i++) {
		err = ubifs_wbuf_sync(&c->jheads[i].wbuf);
		if (err)
			goto out_up;
	}

	c->cmt_no += 1;
	err = ubifs_gc_start_commit(c);
	if (err)
		goto out_up;
	err = dbg_check_lprops(c);
	if (err)
		goto out_up;
	err = ubifs_log_start_commit(c, &new_ltail_lnum);
	if (err)
		goto out_up;
	err = ubifs_tnc_start_commit(c, &zroot);
	if (err)
		goto out_up;
	err = ubifs_lpt_start_commit(c);
	if (err)
		goto out_up;
	err = ubifs_orphan_start_commit(c);
	if (err)
		goto out_up;

	ubifs_get_lp_stats(c, &lst);

	up_write(&c->commit_sem);

	err = ubifs_tnc_end_commit(c);
	if (err)
		goto out;
	err = ubifs_lpt_end_commit(c);
	if (err)
		goto out;
	err = ubifs_orphan_end_commit(c);
	if (err)
		goto out;
	err = dbg_check_old_index(c, &zroot);
	if (err)
		goto out;

	c->mst_node->cmt_no      = cpu_to_le64(c->cmt_no);
	c->mst_node->log_lnum    = cpu_to_le32(new_ltail_lnum);
	c->mst_node->root_lnum   = cpu_to_le32(zroot.lnum);
	c->mst_node->root_offs   = cpu_to_le32(zroot.offs);
	c->mst_node->root_len    = cpu_to_le32(zroot.len);
	c->mst_node->ihead_lnum  = cpu_to_le32(c->ihead_lnum);
	c->mst_node->ihead_offs  = cpu_to_le32(c->ihead_offs);
	c->mst_node->index_size  = cpu_to_le64(c->bi.old_idx_sz);
	c->mst_node->lpt_lnum    = cpu_to_le32(c->lpt_lnum);
	c->mst_node->lpt_offs    = cpu_to_le32(c->lpt_offs);
	c->mst_node->nhead_lnum  = cpu_to_le32(c->nhead_lnum);
	c->mst_node->nhead_offs  = cpu_to_le32(c->nhead_offs);
	c->mst_node->ltab_lnum   = cpu_to_le32(c->ltab_lnum);
	c->mst_node->ltab_offs   = cpu_to_le32(c->ltab_offs);
	c->mst_node->lsave_lnum  = cpu_to_le32(c->lsave_lnum);
	c->mst_node->lsave_offs  = cpu_to_le32(c->lsave_offs);
	c->mst_node->lscan_lnum  = cpu_to_le32(c->lscan_lnum);
	c->mst_node->empty_lebs  = cpu_to_le32(lst.empty_lebs);
	c->mst_node->idx_lebs    = cpu_to_le32(lst.idx_lebs);
	c->mst_node->total_free  = cpu_to_le64(lst.total_free);
	c->mst_node->total_dirty = cpu_to_le64(lst.total_dirty);
	c->mst_node->total_used  = cpu_to_le64(lst.total_used);
	c->mst_node->total_dead  = cpu_to_le64(lst.total_dead);
	c->mst_node->total_dark  = cpu_to_le64(lst.total_dark);
	if (c->no_orphs)
		c->mst_node->flags |= cpu_to_le32(UBIFS_MST_NO_ORPHS);
	else
		c->mst_node->flags &= ~cpu_to_le32(UBIFS_MST_NO_ORPHS);

	old_ltail_lnum = c->ltail_lnum;
	err = ubifs_log_end_commit(c, new_ltail_lnum);
	if (err)
		goto out;

	err = ubifs_log_post_commit(c, old_ltail_lnum);
	if (err)
		goto out;
	err = ubifs_gc_end_commit(c);
	if (err)
		goto out;
	err = ubifs_lpt_post_commit(c);
	if (err)
		goto out;

out_cancel:
	spin_lock(&c->cs_lock);
	c->cmt_state = COMMIT_RESTING;
	dbg_cmt("commit end");
	spin_unlock(&c->cs_lock);
	return 0;

out_up:
	up_write(&c->commit_sem);
out:
	ubifs_err(c, "commit failed, error %d", err);
	spin_lock(&c->cs_lock);
	c->cmt_state = COMMIT_BROKEN;
	spin_unlock(&c->cs_lock);
	ubifs_ro_mode(c, err);
	return err;
}

/**
 * ubifs_commit_required - set commit state to "required".
 * @c: UBIFS file-system description object
 *
 * This function is called if a commit is required but cannot be done from the
 * calling function, so it is just flagged instead.
 */
void ubifs_commit_required(struct ubifs_info *c)
{
	spin_lock(&c->cs_lock);
	switch (c->cmt_state) {
	case COMMIT_RESTING:
	case COMMIT_BACKGROUND:
		dbg_cmt("old: %s, new: %s", dbg_cstate(c->cmt_state),
			dbg_cstate(COMMIT_REQUIRED));
		c->cmt_state = COMMIT_REQUIRED;
		break;
	case COMMIT_RUNNING_BACKGROUND:
		dbg_cmt("old: %s, new: %s", dbg_cstate(c->cmt_state),
			dbg_cstate(COMMIT_RUNNING_REQUIRED));
		c->cmt_state = COMMIT_RUNNING_REQUIRED;
		break;
	case COMMIT_REQUIRED:
	case COMMIT_RUNNING_REQUIRED:
	case COMMIT_BROKEN:
		break;
	}
	spin_unlock(&c->cs_lock);
}

/**
 * ubifs_request_bg_commit - notify the background thread to do a commit.
 * @c: UBIFS file-system description object
 *
 * This function is called if the journal is full enough to make a commit
 * worthwhile, so background thread is kicked to start it.
 */
void ubifs_request_bg_commit(__unused struct ubifs_info *c)
{
}

/**
 * wait_for_commit - wait for commit.
 * @c: UBIFS file-system description object
 *
 * This function sleeps until the commit operation is no longer running.
 */
static int wait_for_commit(struct ubifs_info *c)
{
	/*
	 * All commit operations are executed in synchronization context,
	 * so it is impossible that more than one threads doing commit.
	 */
	ubifs_assert(c, 0);
	return 0;
}

/**
 * ubifs_run_commit - run or wait for commit.
 * @c: UBIFS file-system description object
 *
 * This function runs commit and returns zero in case of success and a negative
 * error code in case of failure.
 */
int ubifs_run_commit(struct ubifs_info *c)
{
	int err = 0;

	spin_lock(&c->cs_lock);
	if (c->cmt_state == COMMIT_BROKEN) {
		err = -EROFS;
		goto out;
	}

	if (c->cmt_state == COMMIT_RUNNING_BACKGROUND)
		/*
		 * We set the commit state to 'running required' to indicate
		 * that we want it to complete as quickly as possible.
		 */
		c->cmt_state = COMMIT_RUNNING_REQUIRED;

	if (c->cmt_state == COMMIT_RUNNING_REQUIRED) {
		spin_unlock(&c->cs_lock);
		return wait_for_commit(c);
	}
	spin_unlock(&c->cs_lock);

	/* Ok, the commit is indeed needed */

	down_write(&c->commit_sem);
	spin_lock(&c->cs_lock);
	/*
	 * Since we unlocked 'c->cs_lock', the state may have changed, so
	 * re-check it.
	 */
	if (c->cmt_state == COMMIT_BROKEN) {
		err = -EROFS;
		goto out_cmt_unlock;
	}

	if (c->cmt_state == COMMIT_RUNNING_BACKGROUND)
		c->cmt_state = COMMIT_RUNNING_REQUIRED;

	if (c->cmt_state == COMMIT_RUNNING_REQUIRED) {
		up_write(&c->commit_sem);
		spin_unlock(&c->cs_lock);
		return wait_for_commit(c);
	}
	c->cmt_state = COMMIT_RUNNING_REQUIRED;
	spin_unlock(&c->cs_lock);

	err = do_commit(c);
	return err;

out_cmt_unlock:
	up_write(&c->commit_sem);
out:
	spin_unlock(&c->cs_lock);
	return err;
}

/**
 * ubifs_gc_should_commit - determine if it is time for GC to run commit.
 * @c: UBIFS file-system description object
 *
 * This function is called by garbage collection to determine if commit should
 * be run. If commit state is @COMMIT_BACKGROUND, which means that the journal
 * is full enough to start commit, this function returns true. It is not
 * absolutely necessary to commit yet, but it feels like this should be better
 * then to keep doing GC. This function returns %1 if GC has to initiate commit
 * and %0 if not.
 */
int ubifs_gc_should_commit(struct ubifs_info *c)
{
	int ret = 0;

	spin_lock(&c->cs_lock);
	if (c->cmt_state == COMMIT_BACKGROUND) {
		dbg_cmt("commit required now");
		c->cmt_state = COMMIT_REQUIRED;
	} else
		dbg_cmt("commit not requested");
	if (c->cmt_state == COMMIT_REQUIRED)
		ret = 1;
	spin_unlock(&c->cs_lock);
	return ret;
}
