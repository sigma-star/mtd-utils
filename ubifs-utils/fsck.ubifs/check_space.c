// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Huawei Technologies Co, Ltd.
 *
 * Authors: Zhihao Cheng <chengzhihao1@huawei.com>
 */

#include <stdio.h>
#include <stdlib.h>

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
 * get_free_leb - get a free LEB according to @FSCK(c)->used_lebs.
 * @c: UBIFS file-system description object
 *
 * This function tries to find a free LEB, lnum is returned if found, otherwise
 * %-ENOSPC is returned.
 */
int get_free_leb(struct ubifs_info *c)
{
	int lnum;

	lnum = find_next_zero_bit(FSCK(c)->used_lebs, c->main_lebs, 0);
	if (lnum >= c->main_lebs) {
		ubifs_err(c, "No space left.");
		return -ENOSPC;
	}
	set_bit(lnum, FSCK(c)->used_lebs);
	lnum += c->main_first;

	return lnum;
}

/**
 * build_lpt - construct LPT and write it into flash.
 * @c: UBIFS file-system description object
 * @calculate_lp_cb: callback function to calculate the properties for given LEB
 * @free_ltab: %true means to release c->ltab after creating lpt
 *
 * This function builds LPT according to the calculated results by
 * @calculate_lp_cb and writes LPT into flash. Returns zero in case of success,
 * a negative error code in case of failure.
 */
int build_lpt(struct ubifs_info *c, calculate_lp_callback calculate_lp_cb,
	      bool free_ltab)
{
	int i, err, lnum, free, dirty;
	u8 hash_lpt[UBIFS_HASH_ARR_SZ];

	memset(&c->lst, 0, sizeof(struct ubifs_lp_stats));
	/* Set gc lnum, equivalent to ubifs_rcvry_gc_commit/take_gc_lnum. */
	lnum = get_free_leb(c);
	if (lnum < 0)
		return lnum;
	c->gc_lnum = lnum;

	/* Update LPT. */
	for (i = 0; i < c->main_lebs; i++) {
		err = calculate_lp_cb(c, i, &free, &dirty, NULL);
		if (err)
			return err;

		FSCK(c)->lpts[i].free = free;
		FSCK(c)->lpts[i].dirty = dirty;
		c->lst.total_free += free;
		c->lst.total_dirty += dirty;

		if (free == c->leb_size)
			c->lst.empty_lebs++;

		if (FSCK(c)->lpts[i].flags & LPROPS_INDEX) {
			c->lst.idx_lebs += 1;
		} else {
			int spc;

			spc = free + dirty;
			if (spc < c->dead_wm)
				c->lst.total_dead += spc;
			else
				c->lst.total_dark += ubifs_calc_dark(c, spc);
			c->lst.total_used += c->leb_size - spc;
		}

		dbg_fsck("build properties for LEB %d, free %d dirty %d is_idx %d, in %s",
			 i + c->main_first, free, dirty,
			 FSCK(c)->lpts[i].flags & LPROPS_INDEX ? 1 : 0,
			 c->dev_name);
	}

	/* Write LPT. */
	return ubifs_create_lpt(c, FSCK(c)->lpts, c->main_lebs, hash_lpt, free_ltab);
}

static int scan_get_lp(struct ubifs_info *c, int index, int *free, int *dirty,
		       int *is_idx)
{
	struct ubifs_scan_leb *sleb;
	struct ubifs_scan_node *snod;
	int used, idx_leb, lnum = index + c->main_first, err = 0;
	bool is_build_lpt = FSCK(c)->lpt_status & FR_LPT_CORRUPTED;

	if (is_build_lpt) {
		if (!test_bit(index, FSCK(c)->used_lebs) || c->gc_lnum == lnum) {
			*free = c->leb_size;
			*dirty = 0;
			return 0;
		}
	} else {
		if (!test_bit(index, FSCK(c)->used_lebs)) {
			*free = c->leb_size;
			*dirty = 0;
			return 0;
		}
	}

	sleb = ubifs_scan(c, lnum, 0, c->sbuf, 0);
	if (IS_ERR(sleb)) {
		/* All TNC LEBs have passed ubifs_scan in previous steps. */
		ubifs_assert(c, !get_failure_reason_callback(c));
		return PTR_ERR(sleb);
	}

	idx_leb = -1;
	used = 0;
	list_for_each_entry(snod, &sleb->nodes, list) {
		int found, level = 0;

		if (idx_leb == -1)
			idx_leb = (snod->type == UBIFS_IDX_NODE) ? 1 : 0;

		if (idx_leb)
			/*
			 * Previous steps have ensured that every TNC LEB
			 * contains only index nodes or non-index nodes.
			 */
			ubifs_assert(c, snod->type == UBIFS_IDX_NODE);

		if (snod->type == UBIFS_IDX_NODE) {
			struct ubifs_idx_node *idx = snod->node;

			key_read(c, ubifs_idx_key(c, idx), &snod->key);
			level = le16_to_cpu(idx->level);
		}

		found = ubifs_tnc_has_node(c, &snod->key, level, lnum,
					   snod->offs, idx_leb);
		if (found) {
			if (found < 0) {
				err = found;
				/*
				 * TNC traversing is finished in previous steps,
				 * any TNC path is accessible.
				 */
				ubifs_assert(c, !get_failure_reason_callback(c));
				goto out;
			}
			used += ALIGN(snod->len, 8);
		}
	}

	if (is_build_lpt && !used) {
		*free = c->leb_size;
		*dirty = 0;
	} else {
		*free = c->leb_size - sleb->endpt;
		*dirty = sleb->endpt - used;
		if (idx_leb == 1) {
			if (is_build_lpt)
				FSCK(c)->lpts[index].flags = LPROPS_INDEX;
			else
				*is_idx = 1;
		}
	}

out:
	ubifs_scan_destroy(sleb);
	return err;
}

static void clear_buds(struct ubifs_info *c)
{
	int i;

	/*
	 * Since lpt is invalid, space statistics cannot be trusted, the buds
	 * were used to trace taken LEBs(LPT related), and fsck makes sure that
	 * there will be no new journal writings(no space allocations) before
	 * committing, so we should clear buds to prevent wrong lpt updating in
	 * committing stage(eg. ubifs_return_leb operation for @c->old_buds).
	 */
	free_buds(c, true);
	for (i = 0; i < c->jhead_cnt; i++) {
		c->jheads[i].wbuf.lnum = -1;
		c->jheads[i].wbuf.offs = -1;
	}
}

static void clear_lp_lists_and_heaps(struct ubifs_info *c)
{
	int i;

	/*
	 * Since lpt is invalid, clear in-memory fast accessing paths (lp
	 * lists & heaps).
	 */
	c->freeable_cnt = 0;
	c->in_a_category_cnt = 0;
	for (i = 0; i < LPROPS_HEAP_CNT; i++) {
		memset(c->lpt_heap[i].arr, 0, LPT_HEAP_SZ * sizeof(void *));
		c->lpt_heap[i].cnt = 0;
		c->lpt_heap[i].max_cnt = LPT_HEAP_SZ;
	}
	memset(c->dirty_idx.arr, 0, LPT_HEAP_SZ * sizeof(void *));
	c->dirty_idx.cnt = 0;
	c->dirty_idx.max_cnt = LPT_HEAP_SZ;
	INIT_LIST_HEAD(&c->uncat_list);
	INIT_LIST_HEAD(&c->empty_list);
	INIT_LIST_HEAD(&c->freeable_list);
	INIT_LIST_HEAD(&c->frdi_idx_list);
}

static int retake_ihead(struct ubifs_info *c)
{
	int err = take_ihead(c);

	if (err < 0) {
		/* All LPT nodes must be accessible. */
		ubifs_assert(c, !get_failure_reason_callback(c));
		ubifs_assert(c, FSCK(c)->lpt_status == 0);
	} else
		err = 0;

	return err;
}

static int rebuild_lpt(struct ubifs_info *c)
{
	int err;

	/* Clear buds. */
	clear_buds(c);
	/* Clear stale in-memory lpt data. */
	c->lpt_drty_flgs = 0;
	c->dirty_nn_cnt = 0;
	c->dirty_pn_cnt = 0;
	clear_lp_lists_and_heaps(c);
	ubifs_free_lpt_nodes(c);
	kfree(c->ltab);
	c->ltab = NULL;

	FSCK(c)->lpts = kzalloc(sizeof(struct ubifs_lprops) * c->main_lebs,
				GFP_KERNEL);
	if (!FSCK(c)->lpts) {
		log_err(c, errno, "can not allocate lpts");
		return -ENOMEM;
	}

	err = build_lpt(c, scan_get_lp, false);
	if (err)
		goto out;

	err = retake_ihead(c);
	if (err)
		goto out;

	FSCK(c)->lpt_status = 0;

out:
	kfree(FSCK(c)->lpts);
	return err;
}

static void check_and_correct_nnode(struct ubifs_info *c,
				    struct ubifs_nnode *nnode,
				    struct ubifs_nnode *parent_nnode,
				    int row, int col, int *corrected)
{
	int num = ubifs_calc_nnode_num(row, col);

	if (nnode->num != num) {
		struct nnode_problem nnp = {
			.nnode = nnode,
			.parent_nnode = parent_nnode,
			.num = num,
		};

		/*
		 * The nnode number is read from disk in big lpt mode, which
		 * could lead to the wrong nnode number, otherwise, ther nnode
		 * number cannot be wrong.
		 */
		ubifs_assert(c, c->big_lpt);
		FSCK(c)->lpt_status |= FR_LPT_INCORRECT;
		if (fix_problem(c, NNODE_INCORRECT, &nnp)) {
			nnode->num = num;
			*corrected = 1;
		}
	}
}

static int check_and_correct_pnode(struct ubifs_info *c,
				   struct ubifs_pnode *pnode, int col,
				   struct ubifs_lp_stats *lst,
				   int *freeable_cnt, int *corrected)
{
	int i, index, lnum;
	const int lp_cnt = UBIFS_LPT_FANOUT;

	if (pnode->num != col) {
		struct pnode_problem pnp = {
			.pnode = pnode,
			.num = col,
		};

		/*
		 * The pnode number is read from disk in big lpt mode, which
		 * could lead to the wrong pnode number, otherwise, ther pnode
		 * number cannot be wrong.
		 */
		ubifs_assert(c, c->big_lpt);
		FSCK(c)->lpt_status |= FR_LPT_INCORRECT;
		if (fix_problem(c, PNODE_INCORRECT, &pnp)) {
			pnode->num = col;
			*corrected = 1;
		}
	}

	index = pnode->num << UBIFS_LPT_FANOUT_SHIFT;
	lnum = index + c->main_first;
	for (i = 0; i < lp_cnt && lnum < c->leb_cnt; i++, index++, lnum++) {
		int err, cat, free, dirty, is_idx = 0;
		struct ubifs_lprops *lp = &pnode->lprops[i];

		err = scan_get_lp(c, index, &free, &dirty, &is_idx);
		if (err)
			return err;

		dbg_fsck("calculate properties for LEB %d, free %d dirty %d is_idx %d, in %s",
			 lnum, free, dirty, is_idx, c->dev_name);

		if (!FSCK(c)->lpt_status && lp->free + lp->dirty == c->leb_size
		    && !test_bit(index, FSCK(c)->used_lebs)) {
			/*
			 * Some LEBs may become freeable in the following cases:
			 *  a. LEBs become freeable after replaying the journal.
			 *  b. Unclean reboot while doing gc for a freeable
			 *     non-index LEB
			 *  c. Freeable index LEBs in an uncompleted commit due
			 *     to an unclean unmount.
			 * , which makes that these LEBs won't be accounted into
			 * the FSCK(c)->used_lebs, but they actually have
			 * free/dirty space statistics. So we should skip
			 * checking space for these LEBs.
			 */
			free = lp->free;
			dirty = lp->dirty;
			is_idx = (lp->flags & LPROPS_INDEX) ? 1 : 0;
		}
		if (lnum != lp->lnum ||
		    free != lp->free || dirty != lp->dirty ||
		    (is_idx && !(lp->flags & LPROPS_INDEX)) ||
		    (!is_idx && (lp->flags & LPROPS_INDEX))) {
			struct lp_problem lpp = {
				.lnum = lnum,
				.lp = lp,
				.free = free,
				.dirty = dirty,
				.is_idx = is_idx,
			};

			FSCK(c)->lpt_status |= FR_LPT_INCORRECT;
			if (fix_problem(c, LP_INCORRECT, &lpp)) {
				lp->lnum = lnum;
				lp->free = free;
				lp->dirty = dirty;
				lp->flags = is_idx ? LPROPS_INDEX : 0;
				*corrected = 1;
			}
		}

		cat = ubifs_categorize_lprops(c, lp);
		if (cat != (lp->flags & LPROPS_CAT_MASK)) {
			if (FSCK(c)->lpt_status & FR_LPT_INCORRECT) {
				lp->flags &= ~LPROPS_CAT_MASK;
				lp->flags |= cat;
			} else {
				/* lp could be in the heap or un-categorized(add heap failed). */
				ubifs_assert(c, (lp->flags & LPROPS_CAT_MASK) == LPROPS_UNCAT);
			}
		}
		if (cat == LPROPS_FREEABLE)
			*freeable_cnt = *freeable_cnt + 1;
		if ((lp->flags & LPROPS_TAKEN) && free == c->leb_size)
			lst->taken_empty_lebs += 1;

		lst->total_free += free;
		lst->total_dirty += dirty;

		if (free == c->leb_size)
			lst->empty_lebs++;

		if (is_idx) {
			lst->idx_lebs += 1;
		} else {
			int spc;

			spc = free + dirty;
			if (spc < c->dead_wm)
				lst->total_dead += spc;
			else
				lst->total_dark += ubifs_calc_dark(c, spc);
			lst->total_used += c->leb_size - spc;
		}
	}

	return 0;
}

static int check_and_correct_lpt(struct ubifs_info *c, int *lpt_corrected)
{
	int err, i, cnt, iip, row, col, corrected, lnum, max_num, freeable_cnt;
	struct ubifs_cnode *cn, *cnode;
	struct ubifs_nnode *nnode, *nn;
	struct ubifs_pnode *pnode;
	struct ubifs_lp_stats lst;

	max_num = 0;
	freeable_cnt = 0;
	memset(&lst, 0, sizeof(struct ubifs_lp_stats));

	/* Load the entire LPT tree, check whether there are corrupted nodes. */
	cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);
	for (i = 0; i < cnt; i++) {
		pnode = ubifs_pnode_lookup(c, i);
		if (IS_ERR(pnode))
			return PTR_ERR(pnode);
		if (pnode->num > max_num)
			max_num = pnode->num;
	}

	/* Check whether there are pnodes exceeding the 'c->main_lebs'. */
	pnode = ubifs_pnode_lookup(c, 0);
	if (IS_ERR(pnode))
		return PTR_ERR(pnode);
	while (pnode) {
		if (pnode->num > max_num) {
			ubifs_err(c, "pnode(%d) exceeds max number(%d)",
				  pnode->num, max_num);
			set_failure_reason_callback(c, FR_LPT_CORRUPTED);
			return -EINVAL;
		}
		pnode = ubifs_find_next_pnode(c, pnode);
		if (IS_ERR(pnode))
			return PTR_ERR(pnode);
	}

	/* Check & correct nnodes and pnodes(including LEB properties). */
	row = col = iip = 0;
	cnode = (struct ubifs_cnode *)c->nroot;
	while (cnode) {
		ubifs_assert(c, row >= 0);
		nnode = cnode->parent;
		if (cnode->level) {
			corrected = 0;
			/* cnode is a nnode */
			nn = (struct ubifs_nnode *)cnode;
			check_and_correct_nnode(c, nn, nnode, row, col,
						&corrected);
			if (corrected)
				ubifs_make_nnode_dirty(c, nn);
			while (iip < UBIFS_LPT_FANOUT) {
				cn = nn->nbranch[iip].cnode;
				if (cn) {
					/* Go down */
					row += 1;
					col <<= UBIFS_LPT_FANOUT_SHIFT;
					col += iip;
					iip = 0;
					cnode = cn;
					break;
				}
				/* Go right */
				iip += 1;
			}
			if (iip < UBIFS_LPT_FANOUT)
				continue;
		} else {
			corrected = 0;
			/* cnode is a pnode */
			pnode = (struct ubifs_pnode *)cnode;
			err = check_and_correct_pnode(c, pnode, col, &lst,
						      &freeable_cnt, &corrected);
			if (err)
				return err;
			if (corrected)
				ubifs_make_pnode_dirty(c, pnode);
		}
		/* Go up and to the right */
		row -= 1;
		col >>= UBIFS_LPT_FANOUT_SHIFT;
		iip = cnode->iip + 1;
		cnode = (struct ubifs_cnode *)nnode;
	}

	dbg_fsck("empty_lebs %d, idx_lebs %d, total_free %lld, total_dirty %lld,"
		 " total_used %lld, total_dead %lld, total_dark %lld,"
		 " taken_empty_lebs %d, freeable_cnt %d, in %s",
		 lst.empty_lebs, lst.idx_lebs, lst.total_free, lst.total_dirty,
		 lst.total_used, lst.total_dead, lst.total_dark,
		 lst.taken_empty_lebs, freeable_cnt, c->dev_name);

	/* Check & correct the global space statistics. */
	if (lst.empty_lebs != c->lst.empty_lebs ||
	    lst.idx_lebs != c->lst.idx_lebs ||
	    lst.total_free != c->lst.total_free ||
	    lst.total_dirty != c->lst.total_dirty ||
	    lst.total_used != c->lst.total_used ||
	    lst.total_dead != c->lst.total_dead ||
	    lst.total_dark != c->lst.total_dark) {
		struct space_stat_problem ssp = {
			.lst = &c->lst,
			.calc_lst = &lst,
		};

		FSCK(c)->lpt_status |= FR_LPT_INCORRECT;
		if (fix_problem(c, SPACE_STAT_INCORRECT, &ssp)) {
			c->lst.empty_lebs = lst.empty_lebs;
			c->lst.idx_lebs = lst.idx_lebs;
			c->lst.total_free = lst.total_free;
			c->lst.total_dirty = lst.total_dirty;
			c->lst.total_used = lst.total_used;
			c->lst.total_dead = lst.total_dead;
			c->lst.total_dark = lst.total_dark;
		}
	}

	/* Check & correct the lprops table information. */
	for (lnum = c->lpt_first; lnum <= c->lpt_last; lnum++) {
		err = dbg_check_ltab_lnum(c, lnum);
		if (err)
			return err;
	}

	if (FSCK(c)->lpt_status & FR_LPT_INCORRECT) {
		/* Reset the taken_empty_lebs. */
		c->lst.taken_empty_lebs = 0;
		/* Clear buds. */
		clear_buds(c);
		/* Clear lp lists & heaps. */
		clear_lp_lists_and_heaps(c);
		/*
		 * Build lp lists & heaps, subsequent steps could recover
		 * disconnected files by allocating free space.
		 */
		for (lnum = c->main_first; lnum < c->leb_cnt; lnum++) {
			int cat;
			struct ubifs_lprops *lp = ubifs_lpt_lookup(c, lnum);
			if (IS_ERR(lp))
				return PTR_ERR(lp);

			/* Clear %LPROPS_TAKEN flag for all LEBs. */
			lp->flags &= ~LPROPS_TAKEN;
			cat = lp->flags & LPROPS_CAT_MASK;
			ubifs_add_to_cat(c, lp, cat);
		}
		/*
		 * The %LPROPS_TAKEN flag is cleared in LEB properties, just
		 * remark it for c->ihead_lnum LEB.
		 */
		err = retake_ihead(c);
		if (err)
			return err;

		*lpt_corrected = 1;
		FSCK(c)->lpt_status &= ~FR_LPT_INCORRECT;
	} else {
		ubifs_assert(c, c->freeable_cnt == freeable_cnt);
		ubifs_assert(c, c->lst.taken_empty_lebs == lst.taken_empty_lebs);
		ubifs_assert(c, c->in_a_category_cnt == c->main_lebs);
	}

	return 0;
}

/**
 * check_and_correct_space - check & correct the space statistics.
 * @c: UBIFS file-system description object
 *
 * This function does following things:
 * 1. Check fsck mode, exit program if current mode is check mode.
 * 2. Check space statistics by comparing lpt records with scanning results
 *    for all main LEBs. There could be following problems:
 *    a) comparison result is inconsistent: correct the lpt records by LEB
 *       scanning results.
 *    b) lpt is corrupted: rebuild lpt.
 * 3. Set the gc lnum.
 * Returns zero in case of success, a negative error code in case of failure.
 */
int check_and_correct_space(struct ubifs_info *c)
{
	int err, lpt_corrected = 0;

	if (FSCK(c)->mode == CHECK_MODE) {
		/*
		 * The check mode will exit, because unclean LEBs are not
		 * rewritten for readonly mode in previous steps.
		 */
		if (FSCK(c)->lpt_status)
			exit_code |= FSCK_UNCORRECTED;
		dbg_fsck("skip checking & correcting space%s, in %s",
			 mode_name(c), c->dev_name);
		exit(exit_code);
	}

	log_out(c, "Check and correct the space statistics");

	if (FSCK(c)->lpt_status & FR_LPT_CORRUPTED) {
rebuild:
		if (fix_problem(c, LPT_CORRUPTED, NULL))
			return rebuild_lpt(c);
	}

	err = check_and_correct_lpt(c, &lpt_corrected);
	if (err) {
		if (test_and_clear_failure_reason_callback(c, FR_LPT_CORRUPTED))
			goto rebuild;
		return err;
	}

	/* Set gc lnum. */
	if (c->need_recovery || lpt_corrected) {
		err = ubifs_rcvry_gc_commit(c);
		if (err) {
			/* All LPT nodes must be accessible. */
			ubifs_assert(c, !get_failure_reason_callback(c));
			ubifs_assert(c, FSCK(c)->lpt_status == 0);
			return err;
		}
	} else {
		err = take_gc_lnum(c);
		if (err) {
			/* All LPT nodes must be accessible. */
			ubifs_assert(c, !get_failure_reason_callback(c));
			ubifs_assert(c, FSCK(c)->lpt_status == 0);
			return err;
		}
		err = ubifs_leb_unmap(c, c->gc_lnum);
		if (err)
			return err;
	}

	return err;
}

/**
 * check_and_correct_index_size - check & correct the index size.
 * @c: UBIFS file-system description object
 *
 * This function checks and corrects the index size by traversing TNC: Returns
 * zero in case of success, a negative error code in case of failure.
 */
int check_and_correct_index_size(struct ubifs_info *c)
{
	int err;
	unsigned long long index_size = 0;

	ubifs_assert(c, c->bi.old_idx_sz == c->calc_idx_sz);
	err = dbg_walk_index(c, NULL, add_size, &index_size);
	if (err) {
		/* All TNC nodes must be accessible. */
		ubifs_assert(c, !get_failure_reason_callback(c));
		return err;
	}

	dbg_fsck("total index size %llu, in %s", index_size, c->dev_name);
	if (index_size != c->calc_idx_sz &&
	    fix_problem(c, INCORRECT_IDX_SZ, &index_size))
		c->bi.old_idx_sz = c->calc_idx_sz = index_size;

	return 0;
}
