// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS superblock. The superblock is stored at the first
 * LEB of the volume and is never changed by UBIFS. Only user-space tools may
 * change it. The superblock node mostly contains geometry information.
 */

#include "linux_err.h"
#include "bitops.h"
#include "kmem.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "misc.h"

/* Default number of LEB numbers in LPT's save table */
#define DEFAULT_LSAVE_CNT 256

/**
 * validate_sb - validate superblock node.
 * @c: UBIFS file-system description object
 * @sup: superblock node
 *
 * This function validates superblock node @sup. Since most of data was read
 * from the superblock and stored in @c, the function validates fields in @c
 * instead. Returns zero in case of success and %-EINVAL in case of validation
 * failure.
 */
static int validate_sb(struct ubifs_info *c, struct ubifs_sb_node *sup)
{
	long long max_bytes;
	int err = 1, min_leb_cnt;

	if (!c->key_hash) {
		err = 2;
		goto failed;
	}

	if (sup->key_fmt != UBIFS_SIMPLE_KEY_FMT) {
		err = 3;
		goto failed;
	}

	if (le32_to_cpu(sup->min_io_size) != c->min_io_size) {
		ubifs_err(c, "min. I/O unit mismatch: %d in superblock, %d real",
			  le32_to_cpu(sup->min_io_size), c->min_io_size);
		goto failed;
	}

	if (le32_to_cpu(sup->leb_size) != c->leb_size) {
		ubifs_err(c, "LEB size mismatch: %d in superblock, %d real",
			  le32_to_cpu(sup->leb_size), c->leb_size);
		goto failed;
	}

	if (c->log_lebs < UBIFS_MIN_LOG_LEBS ||
	    c->lpt_lebs < UBIFS_MIN_LPT_LEBS ||
	    c->orph_lebs < UBIFS_MIN_ORPH_LEBS ||
	    c->main_lebs < UBIFS_MIN_MAIN_LEBS) {
		err = 4;
		goto failed;
	}

	/*
	 * Calculate minimum allowed amount of main area LEBs. This is very
	 * similar to %UBIFS_MIN_LEB_CNT, but we take into account real what we
	 * have just read from the superblock.
	 */
	min_leb_cnt = UBIFS_SB_LEBS + UBIFS_MST_LEBS + c->log_lebs;
	min_leb_cnt += c->lpt_lebs + c->orph_lebs + c->jhead_cnt + 6;

	if (c->leb_cnt < min_leb_cnt || c->leb_cnt > c->vi.rsvd_lebs) {
		ubifs_err(c, "bad LEB count: %d in superblock, %d on UBI volume, %d minimum required",
			  c->leb_cnt, c->vi.rsvd_lebs, min_leb_cnt);
		goto failed;
	}

	if (c->max_leb_cnt < c->leb_cnt) {
		ubifs_err(c, "max. LEB count %d less than LEB count %d",
			  c->max_leb_cnt, c->leb_cnt);
		goto failed;
	}

	if (c->main_lebs < UBIFS_MIN_MAIN_LEBS) {
		ubifs_err(c, "too few main LEBs count %d, must be at least %d",
			  c->main_lebs, UBIFS_MIN_MAIN_LEBS);
		goto failed;
	}

	max_bytes = (long long)c->leb_size * UBIFS_MIN_BUD_LEBS;
	if (c->max_bud_bytes < max_bytes) {
		ubifs_err(c, "too small journal (%lld bytes), must be at least %lld bytes",
			  c->max_bud_bytes, max_bytes);
		goto failed;
	}

	max_bytes = (long long)c->leb_size * c->main_lebs;
	if (c->max_bud_bytes > max_bytes) {
		ubifs_err(c, "too large journal size (%lld bytes), only %lld bytes available in the main area",
			  c->max_bud_bytes, max_bytes);
		goto failed;
	}

	if (c->jhead_cnt < NONDATA_JHEADS_CNT + 1 ||
	    c->jhead_cnt > NONDATA_JHEADS_CNT + UBIFS_MAX_JHEADS) {
		err = 9;
		goto failed;
	}

	if (c->fanout < UBIFS_MIN_FANOUT ||
	    ubifs_idx_node_sz(c, c->fanout) > c->leb_size) {
		err = 10;
		goto failed;
	}

	if (c->lsave_cnt < 0 || (c->lsave_cnt > DEFAULT_LSAVE_CNT &&
	    c->lsave_cnt > c->max_leb_cnt - UBIFS_SB_LEBS - UBIFS_MST_LEBS -
	    c->log_lebs - c->lpt_lebs - c->orph_lebs)) {
		err = 11;
		goto failed;
	}

	if (UBIFS_SB_LEBS + UBIFS_MST_LEBS + c->log_lebs + c->lpt_lebs +
	    c->orph_lebs + c->main_lebs != c->leb_cnt) {
		err = 12;
		goto failed;
	}

	if (c->default_compr >= UBIFS_COMPR_TYPES_CNT) {
		err = 13;
		goto failed;
	}

	if (c->rp_size < 0 || max_bytes < c->rp_size) {
		err = 14;
		goto failed;
	}

	if (le32_to_cpu(sup->time_gran) > 1000000000 ||
	    le32_to_cpu(sup->time_gran) < 1) {
		err = 15;
		goto failed;
	}

	if (!c->double_hash && c->fmt_version >= 5) {
		err = 16;
		goto failed;
	}

	if (c->encrypted && c->fmt_version < 5) {
		err = 17;
		goto failed;
	}

	return 0;

failed:
	ubifs_err(c, "bad superblock, error %d", err);
	ubifs_dump_node(c, sup, ALIGN(UBIFS_SB_NODE_SZ, c->min_io_size));
	return -EINVAL;
}

/**
 * ubifs_read_sb_node - read superblock node.
 * @c: UBIFS file-system description object
 *
 * This function returns a pointer to the superblock node or a negative error
 * code. Note, the user of this function is responsible of kfree()'ing the
 * returned superblock buffer.
 */
static struct ubifs_sb_node *ubifs_read_sb_node(struct ubifs_info *c)
{
	struct ubifs_sb_node *sup;
	int err;

	sup = kmalloc(ALIGN(UBIFS_SB_NODE_SZ, c->min_io_size), GFP_NOFS);
	if (!sup)
		return ERR_PTR(-ENOMEM);

	err = ubifs_read_node(c, sup, UBIFS_SB_NODE, UBIFS_SB_NODE_SZ,
			      UBIFS_SB_LNUM, 0);
	if (err) {
		kfree(sup);
		return ERR_PTR(err);
	}

	return sup;
}

static int authenticate_sb_node(__unused struct ubifs_info *c,
				const struct ubifs_sb_node *sup)
{
	unsigned int sup_flags = le32_to_cpu(sup->flags);
	int authenticated = !!(sup_flags & UBIFS_FLG_AUTHENTICATION);

	if (authenticated) {
		// To be implemented
		ubifs_err(c, "not support authentication");
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * ubifs_write_sb_node - write superblock node.
 * @c: UBIFS file-system description object
 * @sup: superblock node read with 'ubifs_read_sb_node()'
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_write_sb_node(struct ubifs_info *c, struct ubifs_sb_node *sup)
{
	int len = ALIGN(UBIFS_SB_NODE_SZ, c->min_io_size);
	int err;

	err = ubifs_prepare_node_hmac(c, sup, UBIFS_SB_NODE_SZ,
				      offsetof(struct ubifs_sb_node, hmac), 1);
	if (err)
		return err;

	return ubifs_leb_change(c, UBIFS_SB_LNUM, sup, len);
}

/**
 * ubifs_read_superblock - read superblock.
 * @c: UBIFS file-system description object
 *
 * This function finds, reads and checks the superblock. If an empty UBI volume
 * is being mounted, this function creates default superblock. Returns zero in
 * case of success, and a negative error code in case of failure.
 */
int ubifs_read_superblock(struct ubifs_info *c)
{
	int err, sup_flags;
	struct ubifs_sb_node *sup;

	sup = ubifs_read_sb_node(c);
	if (IS_ERR(sup))
		return PTR_ERR(sup);

	c->sup_node = sup;

	c->fmt_version = le32_to_cpu(sup->fmt_version);
	c->ro_compat_version = le32_to_cpu(sup->ro_compat_version);

	/*
	 * The software supports all previous versions but not future versions,
	 * due to the unavailability of time-travelling equipment.
	 */
	if (c->fmt_version > UBIFS_FORMAT_VERSION) {
		ubifs_assert(c, !c->ro_media || c->ro_mount);
		if (!c->ro_mount ||
		    c->ro_compat_version > UBIFS_RO_COMPAT_VERSION) {
			ubifs_err(c, "on-flash format version is w%d/r%d, but software only supports up to version w%d/r%d",
				  c->fmt_version, c->ro_compat_version,
				  UBIFS_FORMAT_VERSION,
				  UBIFS_RO_COMPAT_VERSION);
			if (c->ro_compat_version <= UBIFS_RO_COMPAT_VERSION) {
				ubifs_msg(c, "only R/O mounting is possible");
				err = -EROFS;
			} else
				err = -EINVAL;
			goto out;
		}
	}

	if (c->fmt_version < 3) {
		ubifs_err(c, "on-flash format version %d is not supported",
			  c->fmt_version);
		err = -EINVAL;
		goto out;
	}

	switch (sup->key_hash) {
	case UBIFS_KEY_HASH_R5:
		c->key_hash = key_r5_hash;
		c->key_hash_type = UBIFS_KEY_HASH_R5;
		break;

	case UBIFS_KEY_HASH_TEST:
		c->key_hash = key_test_hash;
		c->key_hash_type = UBIFS_KEY_HASH_TEST;
		break;
	}

	c->key_fmt = sup->key_fmt;

	switch (c->key_fmt) {
	case UBIFS_SIMPLE_KEY_FMT:
		c->key_len = UBIFS_SK_LEN;
		break;
	default:
		ubifs_err(c, "unsupported key format");
		err = -EINVAL;
		goto out;
	}

	c->leb_cnt       = le32_to_cpu(sup->leb_cnt);
	c->max_leb_cnt   = le32_to_cpu(sup->max_leb_cnt);
	c->max_bud_bytes = le64_to_cpu(sup->max_bud_bytes);
	c->log_lebs      = le32_to_cpu(sup->log_lebs);
	c->lpt_lebs      = le32_to_cpu(sup->lpt_lebs);
	c->orph_lebs     = le32_to_cpu(sup->orph_lebs);
	c->jhead_cnt     = le32_to_cpu(sup->jhead_cnt) + NONDATA_JHEADS_CNT;
	c->fanout        = le32_to_cpu(sup->fanout);
	c->lsave_cnt     = le32_to_cpu(sup->lsave_cnt);
	c->rp_size       = le64_to_cpu(sup->rp_size);
	sup_flags        = le32_to_cpu(sup->flags);
	c->default_compr = le16_to_cpu(sup->default_compr);

	c->big_lpt = !!(sup_flags & UBIFS_FLG_BIGLPT);
	c->space_fixup = !!(sup_flags & UBIFS_FLG_SPACE_FIXUP);
	c->double_hash = !!(sup_flags & UBIFS_FLG_DOUBLE_HASH);
	c->encrypted = !!(sup_flags & UBIFS_FLG_ENCRYPTION);

	err = authenticate_sb_node(c, sup);
	if (err)
		goto out;

	if ((sup_flags & ~UBIFS_FLG_MASK) != 0) {
		ubifs_err(c, "Unknown feature flags found: %#x",
			  sup_flags & ~UBIFS_FLG_MASK);
		err = -EINVAL;
		goto out;
	}

	/* Automatically increase file system size to the maximum size */
	if (c->leb_cnt < c->vi.rsvd_lebs && c->leb_cnt < c->max_leb_cnt) {
		int old_leb_cnt = c->leb_cnt;

		c->leb_cnt = min_t(int, c->max_leb_cnt, c->vi.rsvd_lebs);
		sup->leb_cnt = cpu_to_le32(c->leb_cnt);

		c->superblock_need_write = 1;

		dbg_mnt("Auto resizing from %d LEBs to %d LEBs",
			old_leb_cnt, c->leb_cnt);
	}

	c->log_bytes = (long long)c->log_lebs * c->leb_size;
	c->log_last = UBIFS_LOG_LNUM + c->log_lebs - 1;
	c->lpt_first = UBIFS_LOG_LNUM + c->log_lebs;
	c->lpt_last = c->lpt_first + c->lpt_lebs - 1;
	c->orph_first = c->lpt_last + 1;
	c->orph_last = c->orph_first + c->orph_lebs - 1;
	c->main_lebs = c->leb_cnt - UBIFS_SB_LEBS - UBIFS_MST_LEBS;
	c->main_lebs -= c->log_lebs + c->lpt_lebs + c->orph_lebs;
	c->main_first = c->leb_cnt - c->main_lebs;

	err = validate_sb(c, sup);
out:
	if (err)
		set_failure_reason_callback(c, FR_DATA_CORRUPTED);
	return err;
}

/**
 * fixup_leb - fixup/unmap an LEB containing free space.
 * @c: UBIFS file-system description object
 * @lnum: the LEB number to fix up
 * @len: number of used bytes in LEB (starting at offset 0)
 *
 * This function reads the contents of the given LEB number @lnum, then fixes
 * it up, so that empty min. I/O units in the end of LEB are actually erased on
 * flash (rather than being just all-0xff real data). If the LEB is completely
 * empty, it is simply unmapped.
 */
static int fixup_leb(struct ubifs_info *c, int lnum, int len)
{
	int err;

	ubifs_assert(c, len >= 0);
	ubifs_assert(c, len % c->min_io_size == 0);
	ubifs_assert(c, len < c->leb_size);

	if (len == 0) {
		dbg_mnt("unmap empty LEB %d", lnum);
		return ubifs_leb_unmap(c, lnum);
	}

	dbg_mnt("fixup LEB %d, data len %d", lnum, len);
	err = ubifs_leb_read(c, lnum, c->sbuf, 0, len, 1);
	if (err && err != -EBADMSG)
		return err;

	return ubifs_leb_change(c, lnum, c->sbuf, len);
}

/**
 * fixup_free_space - find & remap all LEBs containing free space.
 * @c: UBIFS file-system description object
 *
 * This function walks through all LEBs in the filesystem and fiexes up those
 * containing free/empty space.
 */
static int fixup_free_space(struct ubifs_info *c)
{
	int lnum, err = 0;
	struct ubifs_lprops *lprops;

	ubifs_get_lprops(c);

	/* Fixup LEBs in the master area */
	for (lnum = UBIFS_MST_LNUM; lnum < UBIFS_LOG_LNUM; lnum++) {
		err = fixup_leb(c, lnum, c->mst_offs + c->mst_node_alsz);
		if (err)
			goto out;
	}

	/* Unmap unused log LEBs */
	lnum = ubifs_next_log_lnum(c, c->lhead_lnum);
	while (lnum != c->ltail_lnum) {
		err = fixup_leb(c, lnum, 0);
		if (err)
			goto out;
		lnum = ubifs_next_log_lnum(c, lnum);
	}

	/*
	 * Fixup the log head which contains the only a CS node at the
	 * beginning.
	 */
	err = fixup_leb(c, c->lhead_lnum,
			ALIGN(UBIFS_CS_NODE_SZ, c->min_io_size));
	if (err)
		goto out;

	/* Fixup LEBs in the LPT area */
	for (lnum = c->lpt_first; lnum <= c->lpt_last; lnum++) {
		int free = c->ltab[lnum - c->lpt_first].free;

		if (free > 0) {
			err = fixup_leb(c, lnum, c->leb_size - free);
			if (err)
				goto out;
		}
	}

	/* Unmap LEBs in the orphans area */
	for (lnum = c->orph_first; lnum <= c->orph_last; lnum++) {
		err = fixup_leb(c, lnum, 0);
		if (err)
			goto out;
	}

	/* Fixup LEBs in the main area */
	for (lnum = c->main_first; lnum < c->leb_cnt; lnum++) {
		lprops = ubifs_lpt_lookup(c, lnum);
		if (IS_ERR(lprops)) {
			err = PTR_ERR(lprops);
			goto out;
		}

		if (lprops->free > 0) {
			err = fixup_leb(c, lnum, c->leb_size - lprops->free);
			if (err)
				goto out;
		}
	}

out:
	ubifs_release_lprops(c);
	return err;
}

/**
 * ubifs_fixup_free_space - find & fix all LEBs with free space.
 * @c: UBIFS file-system description object
 *
 * This function fixes up LEBs containing free space on first mount, if the
 * appropriate flag was set when the FS was created. Each LEB with one or more
 * empty min. I/O unit (i.e. free-space-count > 0) is re-written, to make sure
 * the free space is actually erased. E.g., this is necessary for some NAND
 * chips, since the free space may have been programmed like real "0xff" data
 * (generating a non-0xff ECC), causing future writes to the not-really-erased
 * NAND pages to behave badly. After the space is fixed up, the superblock flag
 * is cleared, so that this is skipped for all future mounts.
 */
int ubifs_fixup_free_space(struct ubifs_info *c)
{
	int err;
	struct ubifs_sb_node *sup = c->sup_node;

	ubifs_assert(c, c->space_fixup);
	ubifs_assert(c, !c->ro_mount);

	ubifs_msg(c, "start fixing up free space");

	err = fixup_free_space(c);
	if (err)
		return err;

	/* Free-space fixup is no longer required */
	c->space_fixup = 0;
	sup->flags &= cpu_to_le32(~UBIFS_FLG_SPACE_FIXUP);

	c->superblock_need_write = 1;

	ubifs_msg(c, "free space fixup complete");
	return err;
}
