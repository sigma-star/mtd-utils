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
 * This file implements UBIFS initialization and VFS superblock operations. Some
 * initialization stuff which is rather large and complex is placed at
 * corresponding subsystems, but most of it is here.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "linux_err.h"
#include "bitops.h"
#include "kmem.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "misc.h"

atomic_long_t ubifs_clean_zn_cnt;
static const int default_debug_level = WARN_LEVEL;

/**
 * open_ubi - open the libubi.
 * @c: the UBIFS file-system description object
 * @node: name of the UBI volume character device to fetch information about
 *
 * This function opens libubi, and initialize device & volume information
 * according to @node. Returns %0 in case of success and %-1 in case of failure.
 */
int open_ubi(struct ubifs_info *c, const char *node)
{
	struct stat st;

	if (stat(node, &st))
		return -1;

	if (!S_ISCHR(st.st_mode)) {
		errno = ENODEV;
		return -1;
	}

	c->libubi = libubi_open();
	if (!c->libubi)
		return -1;
	if (ubi_get_vol_info(c->libubi, node, &c->vi))
		goto out_err;
	if (ubi_get_dev_info1(c->libubi, c->vi.dev_num, &c->di))
		goto out_err;

	return 0;

out_err:
	close_ubi(c);
	return -1;
}

void close_ubi(struct ubifs_info *c)
{
	if (c->libubi) {
		libubi_close(c->libubi);
		c->libubi = NULL;
	}
}

/**
 * open_target - open the output target.
 * @c: the UBIFS file-system description object
 *
 * Open the output target. The target can be an UBI volume
 * or a file.
 *
 * Returns %0 in case of success and a negative error code in case of failure.
 */
int open_target(struct ubifs_info *c)
{
	if (c->libubi) {
		c->dev_fd = open(c->dev_name, O_RDWR | O_EXCL);

		if (c->dev_fd == -1) {
			ubifs_err(c, "cannot open the UBI volume. %s",
				  strerror(errno));
			return -errno;
		}
		if (ubi_set_property(c->dev_fd, UBI_VOL_PROP_DIRECT_WRITE, 1)) {
			close(c->dev_fd);
			ubifs_err(c, "ubi_set_property(set direct_write) failed. %s",
				  strerror(errno));
			return -errno;
		}
	} else {
		c->dev_fd = open(c->dev_name, O_CREAT | O_RDWR | O_TRUNC,
			      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
		if (c->dev_fd == -1) {
			ubifs_err(c, "cannot create output file. %s",
				  strerror(errno));
			return -errno;
		}
	}
	return 0;
}

/**
 * close_target - close the output target.
 * @c: the UBIFS file-system description object
 *
 * Close the output target. If the target was an UBI
 * volume, also close libubi.
 *
 * Returns %0 in case of success and a negative error code in case of failure.
 */
int close_target(struct ubifs_info *c)
{
	if (c->dev_fd >= 0) {
		if (c->libubi && ubi_set_property(c->dev_fd, UBI_VOL_PROP_DIRECT_WRITE, 0)) {
			ubifs_err(c, "ubi_set_property(clear direct_write) failed. %s",
				  strerror(errno));
			return -errno;
		}
		if (close(c->dev_fd) == -1) {
			ubifs_err(c, "cannot close the target. %s",
				  strerror(errno));
			return -errno;
		}
	}
	return 0;
}

/**
 * ubifs_open_volume - open UBI volume.
 * @c: the UBIFS file-system description object
 * @volume_name: the UBI volume name
 *
 * Open ubi volume. This function is implemented by open_ubi + open_target.
 *
 * Returns %0 in case of success and a negative error code in case of failure.
 */
int ubifs_open_volume(struct ubifs_info *c, const char *volume_name)
{
	int err;

	err = open_ubi(c, volume_name);
	if (err) {
		ubifs_err(c, "cannot open libubi. %s", strerror(errno));
		return err;
	}

	err = open_target(c);
	if (err)
		close_ubi(c);

	return err;
}

/**
 * ubifs_close_volume - close UBI volume.
 * @c: the UBIFS file-system description object
 *
 * Close ubi volume. This function is implemented by close_target + close_ubi.
 *
 * Returns %0 in case of success and a negative error code in case of failure.
 */
int ubifs_close_volume(struct ubifs_info *c)
{
	int err;

	err = close_target(c);
	if (err)
		return err;

	close_ubi(c);

	return 0;
}

/**
 * check_volume_empty - check if the UBI volume is empty.
 * @c: the UBIFS file-system description object
 *
 * This function checks if the UBI volume is empty by looking if its LEBs are
 * mapped or not.
 *
 * Returns %0 in case of success, %1 is the volume is not empty,
 * and a negative error code in case of failure.
 */
int check_volume_empty(struct ubifs_info *c)
{
	int lnum, err;

	for (lnum = 0; lnum < c->vi.rsvd_lebs; lnum++) {
		err = ubi_is_mapped(c->dev_fd, lnum);
		if (err < 0)
			return err;
		if (err == 1)
			return 1;
	}
	return 0;
}

void init_ubifs_info(struct ubifs_info *c, int program_type)
{
	spin_lock_init(&c->cnt_lock);
	spin_lock_init(&c->cs_lock);
	spin_lock_init(&c->buds_lock);
	spin_lock_init(&c->space_lock);
	spin_lock_init(&c->orphan_lock);
	init_rwsem(&c->commit_sem);
	mutex_init(&c->lp_mutex);
	mutex_init(&c->tnc_mutex);
	mutex_init(&c->log_mutex);
	c->buds = RB_ROOT;
	c->old_idx = RB_ROOT;
	c->size_tree = RB_ROOT;
	c->orph_tree = RB_ROOT;
	INIT_LIST_HEAD(&c->idx_gc);
	INIT_LIST_HEAD(&c->replay_list);
	INIT_LIST_HEAD(&c->replay_buds);
	INIT_LIST_HEAD(&c->uncat_list);
	INIT_LIST_HEAD(&c->empty_list);
	INIT_LIST_HEAD(&c->freeable_list);
	INIT_LIST_HEAD(&c->frdi_idx_list);
	INIT_LIST_HEAD(&c->unclean_leb_list);
	INIT_LIST_HEAD(&c->old_buds);
	INIT_LIST_HEAD(&c->orph_list);
	INIT_LIST_HEAD(&c->orph_new);
	c->no_chk_data_crc = 1;

	c->highest_inum = UBIFS_FIRST_INO;
	c->lhead_lnum = c->ltail_lnum = UBIFS_LOG_LNUM;

	c->program_type = program_type;
	switch (c->program_type) {
	case MKFS_PROGRAM_TYPE:
		c->program_name = MKFS_PROGRAM_NAME;
		break;
	case FSCK_PROGRAM_TYPE:
		c->program_name = FSCK_PROGRAM_NAME;
		/* Always check crc for data node. */
		c->no_chk_data_crc = 0;
		break;
	default:
		assert(0);
		break;
	}
	c->dev_fd = -1;
	c->debug_level = default_debug_level;
}

/**
 * init_constants_early - initialize UBIFS constants.
 * @c: UBIFS file-system description object
 *
 * This function initialize UBIFS constants which do not need the superblock to
 * be read. It also checks that the UBI volume satisfies basic UBIFS
 * requirements. Returns zero in case of success and a negative error code in
 * case of failure.
 */
int init_constants_early(struct ubifs_info *c)
{
#define NOR_MAX_WRITESZ 64
	if (c->vi.corrupted) {
		ubifs_warn(c, "UBI volume is corrupted - read-only mode");
		c->ro_media = 1;
	}

	if (c->vi.type == UBI_STATIC_VOLUME) {
		ubifs_msg(c, "static UBI volume - read-only mode");
		c->ro_media = 1;
	}

	c->max_inode_sz = key_max_inode_size(c);
	c->leb_cnt = c->vi.rsvd_lebs;
	c->leb_size = c->vi.leb_size;
	c->half_leb_size = c->leb_size / 2;
	c->min_io_size = c->di.min_io_size;
	c->min_io_shift = fls(c->min_io_size) - 1;
	if (c->min_io_size == 1)
		/*
		 * Different from linux kernel, the max write size of nor flash
		 * is not exposed in sysfs, just reset @c->max_write_size.
		 */
		c->max_write_size = NOR_MAX_WRITESZ;
	else
		c->max_write_size = c->di.min_io_size;
	c->max_write_shift = fls(c->max_write_size) - 1;

	if (c->leb_size < UBIFS_MIN_LEB_SZ) {
		ubifs_err(c, "too small LEBs (%d bytes), min. is %d bytes",
			  c->leb_size, UBIFS_MIN_LEB_SZ);
		return -EINVAL;
	}

	if (c->leb_cnt < UBIFS_MIN_LEB_CNT) {
		ubifs_err(c, "too few LEBs (%d), min. is %d",
			  c->leb_cnt, UBIFS_MIN_LEB_CNT);
		return -EINVAL;
	}

	if (!is_power_of_2(c->min_io_size)) {
		ubifs_err(c, "bad min. I/O size %d", c->min_io_size);
		return -EINVAL;
	}

	/*
	 * Maximum write size has to be greater or equivalent to min. I/O
	 * size, and be multiple of min. I/O size.
	 */
	if (c->max_write_size < c->min_io_size ||
	    c->max_write_size % c->min_io_size ||
	    !is_power_of_2(c->max_write_size)) {
		ubifs_err(c, "bad write buffer size %d for %d min. I/O unit",
			  c->max_write_size, c->min_io_size);
		return -EINVAL;
	}

	/*
	 * UBIFS aligns all node to 8-byte boundary, so to make function in
	 * io.c simpler, assume minimum I/O unit size to be 8 bytes if it is
	 * less than 8.
	 */
	if (c->min_io_size < 8) {
		c->min_io_size = 8;
		c->min_io_shift = 3;
		if (c->max_write_size < c->min_io_size) {
			c->max_write_size = c->min_io_size;
			c->max_write_shift = c->min_io_shift;
		}
	}

	c->ref_node_alsz = ALIGN(UBIFS_REF_NODE_SZ, c->min_io_size);
	c->mst_node_alsz = ALIGN(UBIFS_MST_NODE_SZ, c->min_io_size);

	/*
	 * Initialize node length ranges which are mostly needed for node
	 * length validation.
	 */
	c->ranges[UBIFS_PAD_NODE].len  = UBIFS_PAD_NODE_SZ;
	c->ranges[UBIFS_SB_NODE].len   = UBIFS_SB_NODE_SZ;
	c->ranges[UBIFS_MST_NODE].len  = UBIFS_MST_NODE_SZ;
	c->ranges[UBIFS_REF_NODE].len  = UBIFS_REF_NODE_SZ;
	c->ranges[UBIFS_TRUN_NODE].len = UBIFS_TRUN_NODE_SZ;
	c->ranges[UBIFS_CS_NODE].len   = UBIFS_CS_NODE_SZ;
	c->ranges[UBIFS_AUTH_NODE].min_len = UBIFS_AUTH_NODE_SZ;
	c->ranges[UBIFS_AUTH_NODE].max_len = UBIFS_AUTH_NODE_SZ +
				UBIFS_MAX_HMAC_LEN;
	c->ranges[UBIFS_SIG_NODE].min_len = UBIFS_SIG_NODE_SZ;
	c->ranges[UBIFS_SIG_NODE].max_len = c->leb_size - UBIFS_SB_NODE_SZ;

	c->ranges[UBIFS_INO_NODE].min_len  = UBIFS_INO_NODE_SZ;
	c->ranges[UBIFS_INO_NODE].max_len  = UBIFS_MAX_INO_NODE_SZ;
	c->ranges[UBIFS_ORPH_NODE].min_len =
				UBIFS_ORPH_NODE_SZ + sizeof(__le64);
	c->ranges[UBIFS_ORPH_NODE].max_len = c->leb_size;
	c->ranges[UBIFS_DENT_NODE].min_len = UBIFS_DENT_NODE_SZ;
	c->ranges[UBIFS_DENT_NODE].max_len = UBIFS_MAX_DENT_NODE_SZ;
	c->ranges[UBIFS_XENT_NODE].min_len = UBIFS_XENT_NODE_SZ;
	c->ranges[UBIFS_XENT_NODE].max_len = UBIFS_MAX_XENT_NODE_SZ;
	c->ranges[UBIFS_DATA_NODE].min_len = UBIFS_DATA_NODE_SZ;
	c->ranges[UBIFS_DATA_NODE].max_len = UBIFS_MAX_DATA_NODE_SZ;
	/*
	 * Minimum indexing node size is amended later when superblock is
	 * read and the key length is known.
	 */
	c->ranges[UBIFS_IDX_NODE].min_len = UBIFS_IDX_NODE_SZ + UBIFS_BRANCH_SZ;
	/*
	 * Maximum indexing node size is amended later when superblock is
	 * read and the fanout is known.
	 */
	c->ranges[UBIFS_IDX_NODE].max_len = INT_MAX;

	/*
	 * Initialize dead and dark LEB space watermarks. See gc.c for comments
	 * about these values.
	 */
	c->dead_wm = ALIGN(MIN_WRITE_SZ, c->min_io_size);
	c->dark_wm = ALIGN(UBIFS_MAX_NODE_SZ, c->min_io_size);

	/*
	 * Calculate how many bytes would be wasted at the end of LEB if it was
	 * fully filled with data nodes of maximum size. This is used in
	 * calculations when reporting free space.
	 */
	c->leb_overhead = c->leb_size % UBIFS_MAX_DATA_NODE_SZ;

	/* Log is ready, preserve one LEB for commits. */
	c->min_log_bytes = c->leb_size;

	return 0;
}

/**
 * bud_wbuf_callback - bud LEB write-buffer synchronization call-back.
 * @c: UBIFS file-system description object
 * @lnum: LEB the write-buffer was synchronized to
 * @free: how many free bytes left in this LEB
 * @pad: how many bytes were padded
 *
 * This is a callback function which is called by the I/O unit when the
 * write-buffer is synchronized. We need this to correctly maintain space
 * accounting in bud logical eraseblocks. This function returns zero in case of
 * success and a negative error code in case of failure.
 *
 * This function actually belongs to the journal, but we keep it here because
 * we want to keep it static.
 */
static int bud_wbuf_callback(struct ubifs_info *c, int lnum, int free, int pad)
{
	return ubifs_update_one_lp(c, lnum, free, pad, 0, 0);
}

/*
 * init_constants_sb - initialize UBIFS constants.
 * @c: UBIFS file-system description object
 *
 * This is a helper function which initializes various UBIFS constants after
 * the superblock has been read. It also checks various UBIFS parameters and
 * makes sure they are all right. Returns zero in case of success and a
 * negative error code in case of failure.
 */
int init_constants_sb(struct ubifs_info *c)
{
	int tmp, err;
	long long tmp64;

	c->main_bytes = (long long)c->main_lebs * c->leb_size;
	c->max_znode_sz = sizeof(struct ubifs_znode) +
				c->fanout * sizeof(struct ubifs_zbranch);

	tmp = ubifs_idx_node_sz(c, 1);
	c->ranges[UBIFS_IDX_NODE].min_len = tmp;
	c->min_idx_node_sz = ALIGN(tmp, 8);

	tmp = ubifs_idx_node_sz(c, c->fanout);
	c->ranges[UBIFS_IDX_NODE].max_len = tmp;
	c->max_idx_node_sz = ALIGN(tmp, 8);

	/* Make sure LEB size is large enough to fit full commit */
	tmp = UBIFS_CS_NODE_SZ + UBIFS_REF_NODE_SZ * c->jhead_cnt;
	tmp = ALIGN(tmp, c->min_io_size);
	if (tmp > c->leb_size) {
		ubifs_err(c, "too small LEB size %d, at least %d needed",
			  c->leb_size, tmp);
		return -EINVAL;
	}

	/*
	 * Make sure that the log is large enough to fit reference nodes for
	 * all buds plus one reserved LEB.
	 */
	tmp64 = c->max_bud_bytes + c->leb_size - 1;
	c->max_bud_cnt = div_u64(tmp64, c->leb_size);
	tmp = (c->ref_node_alsz * c->max_bud_cnt + c->leb_size - 1);
	tmp /= c->leb_size;
	tmp += 1;
	if (c->log_lebs < tmp) {
		ubifs_err(c, "too small log %d LEBs, required min. %d LEBs",
			  c->log_lebs, tmp);
		return -EINVAL;
	}

	/*
	 * When budgeting we assume worst-case scenarios when the pages are not
	 * be compressed and direntries are of the maximum size.
	 *
	 * Note, data, which may be stored in inodes is budgeted separately, so
	 * it is not included into 'c->bi.inode_budget'.
	 */
	c->bi.page_budget = UBIFS_MAX_DATA_NODE_SZ * UBIFS_BLOCKS_PER_PAGE;
	c->bi.inode_budget = UBIFS_INO_NODE_SZ;
	c->bi.dent_budget = UBIFS_MAX_DENT_NODE_SZ;

	/*
	 * When the amount of flash space used by buds becomes
	 * 'c->max_bud_bytes', UBIFS just blocks all writers and starts commit.
	 * The writers are unblocked when the commit is finished. To avoid
	 * writers to be blocked UBIFS initiates background commit in advance,
	 * when number of bud bytes becomes above the limit defined below.
	 */
	c->bg_bud_bytes = (c->max_bud_bytes * 13) >> 4;

	/*
	 * Ensure minimum journal size. All the bytes in the journal heads are
	 * considered to be used, when calculating the current journal usage.
	 * Consequently, if the journal is too small, UBIFS will treat it as
	 * always full.
	 */
	tmp64 = (long long)(c->jhead_cnt + 1) * c->leb_size + 1;
	if (c->bg_bud_bytes < tmp64)
		c->bg_bud_bytes = tmp64;
	if (c->max_bud_bytes < tmp64 + c->leb_size)
		c->max_bud_bytes = tmp64 + c->leb_size;

	err = ubifs_calc_lpt_geom(c);
	if (err)
		return err;

	/* Initialize effective LEB size used in budgeting calculations */
	c->idx_leb_size = c->leb_size - c->max_idx_node_sz;
	return 0;
}

/*
 * init_constants_master - initialize UBIFS constants.
 * @c: UBIFS file-system description object
 *
 * This is a helper function which initializes various UBIFS constants after
 * the master node has been read. It also checks various UBIFS parameters and
 * makes sure they are all right.
 */
void init_constants_master(struct ubifs_info *c)
{
	c->bi.min_idx_lebs = ubifs_calc_min_idx_lebs(c);
}

/**
 * take_gc_lnum - reserve GC LEB.
 * @c: UBIFS file-system description object
 *
 * This function ensures that the LEB reserved for garbage collection is marked
 * as "taken" in lprops. We also have to set free space to LEB size and dirty
 * space to zero, because lprops may contain out-of-date information if the
 * file-system was un-mounted before it has been committed. This function
 * returns zero in case of success and a negative error code in case of
 * failure.
 */
int take_gc_lnum(struct ubifs_info *c)
{
	int err;

	if (c->gc_lnum == -1) {
		ubifs_err(c, "no LEB for GC");
		return -EINVAL;
	}

	/* And we have to tell lprops that this LEB is taken */
	err = ubifs_change_one_lp(c, c->gc_lnum, c->leb_size, 0,
				  LPROPS_TAKEN, 0, 0);
	return err;
}

/**
 * alloc_wbufs - allocate write-buffers.
 * @c: UBIFS file-system description object
 *
 * This helper function allocates and initializes UBIFS write-buffers. Returns
 * zero in case of success and %-ENOMEM in case of failure.
 */
int alloc_wbufs(struct ubifs_info *c)
{
	int i, err;

	c->jheads = kcalloc(c->jhead_cnt, sizeof(struct ubifs_jhead),
			    GFP_KERNEL);
	if (!c->jheads)
		return -ENOMEM;

	/* Initialize journal heads */
	for (i = 0; i < c->jhead_cnt; i++) {
		INIT_LIST_HEAD(&c->jheads[i].buds_list);
		err = ubifs_wbuf_init(c, &c->jheads[i].wbuf);
		if (err)
			goto out_wbuf;

		c->jheads[i].wbuf.sync_callback = &bud_wbuf_callback;
		c->jheads[i].wbuf.jhead = i;
		c->jheads[i].grouped = 1;
		c->jheads[i].log_hash = ubifs_hash_get_desc(c);
		if (IS_ERR(c->jheads[i].log_hash)) {
			err = PTR_ERR(c->jheads[i].log_hash);
			goto out_log_hash;
		}
	}

	/*
	 * Garbage Collector head does not need to be synchronized by timer.
	 * Also GC head nodes are not grouped.
	 */
	c->jheads[GCHD].grouped = 0;

	return 0;

out_log_hash:
	kfree(c->jheads[i].wbuf.buf);
	kfree(c->jheads[i].wbuf.inodes);

out_wbuf:
	while (i--) {
		kfree(c->jheads[i].wbuf.buf);
		kfree(c->jheads[i].wbuf.inodes);
		kfree(c->jheads[i].log_hash);
	}
	kfree(c->jheads);
	c->jheads = NULL;

	return err;
}

/**
 * free_wbufs - free write-buffers.
 * @c: UBIFS file-system description object
 */
void free_wbufs(struct ubifs_info *c)
{
	int i;

	if (c->jheads) {
		for (i = 0; i < c->jhead_cnt; i++) {
			kfree(c->jheads[i].wbuf.buf);
			kfree(c->jheads[i].wbuf.inodes);
			kfree(c->jheads[i].log_hash);
		}
		kfree(c->jheads);
		c->jheads = NULL;
	}
}

/**
 * free_orphans - free orphans.
 * @c: UBIFS file-system description object
 */
void free_orphans(struct ubifs_info *c)
{
	struct ubifs_orphan *orph;

	while (c->orph_dnext) {
		orph = c->orph_dnext;
		c->orph_dnext = orph->dnext;
		list_del(&orph->list);
		kfree(orph);
	}

	while (!list_empty(&c->orph_list)) {
		orph = list_entry(c->orph_list.next, struct ubifs_orphan, list);
		list_del(&orph->list);
		kfree(orph);
		ubifs_err(c, "orphan list not empty at unmount");
	}

	vfree(c->orph_buf);
	c->orph_buf = NULL;
}

/**
 * free_buds - free per-bud objects.
 * @c: UBIFS file-system description object
 * @delete_from_list: whether to delete the bud from list
 */
void free_buds(struct ubifs_info *c, bool delete_from_list)
{
	struct ubifs_bud *bud, *n;

	rbtree_postorder_for_each_entry_safe(bud, n, &c->buds, rb) {
		if (delete_from_list)
			list_del(&bud->list);
		kfree(bud->log_hash);
		kfree(bud);
	}

	c->buds = RB_ROOT;
}

/**
 * destroy_journal - destroy journal data structures.
 * @c: UBIFS file-system description object
 *
 * This function destroys journal data structures including those that may have
 * been created by recovery functions.
 */
void destroy_journal(struct ubifs_info *c)
{
	while (!list_empty(&c->unclean_leb_list)) {
		struct ubifs_unclean_leb *ucleb;

		ucleb = list_entry(c->unclean_leb_list.next,
				   struct ubifs_unclean_leb, list);
		list_del(&ucleb->list);
		kfree(ucleb);
	}
	while (!list_empty(&c->old_buds)) {
		struct ubifs_bud *bud;

		bud = list_entry(c->old_buds.next, struct ubifs_bud, list);
		list_del(&bud->list);
		kfree(bud->log_hash);
		kfree(bud);
	}
	ubifs_destroy_idx_gc(c);
	ubifs_destroy_size_tree(c);
	ubifs_tnc_close(c);
	free_buds(c, false);
}
