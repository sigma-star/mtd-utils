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
 *
 * This function builds LPT according to the calculated results by
 * @calculate_lp_cb and writes LPT into flash. Returns zero in case of success,
 * a negative error code in case of failure.
 */
int build_lpt(struct ubifs_info *c, calculate_lp_callback calculate_lp_cb)
{
	int i, err, lnum, free, dirty;
	u8 hash_lpt[UBIFS_HASH_ARR_SZ];

	memset(&c->lst, 0, sizeof(struct ubifs_lp_stats));
	/* Set gc lnum. */
	lnum = get_free_leb(c);
	if (lnum < 0)
		return lnum;
	c->gc_lnum = lnum;

	/* Update LPT. */
	for (i = 0; i < c->main_lebs; i++) {
		err = calculate_lp_cb(c, i, &free, &dirty);
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
	}

	/* Write LPT. */
	return ubifs_create_lpt(c, FSCK(c)->lpts, c->main_lebs, hash_lpt);
}
