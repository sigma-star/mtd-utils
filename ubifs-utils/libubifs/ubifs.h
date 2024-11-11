/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

#ifndef __UBIFS_H__
#define __UBIFS_H__

#include <string.h>

#include "linux_types.h"
#include "list.h"
#include "rbtree.h"
#include "spinlock.h"
#include "mutex.h"
#include "rwsem.h"
#include "atomic.h"
#include "libubi.h"
#include "ubifs-media.h"

/* Number of UBIFS blocks per VFS page */
#define UBIFS_BLOCKS_PER_PAGE (PAGE_SIZE / UBIFS_BLOCK_SIZE)
#define UBIFS_BLOCKS_PER_PAGE_SHIFT (PAGE_SHIFT - UBIFS_BLOCK_SHIFT)

/* "File system end of life" sequence number watermark */
#define SQNUM_WARN_WATERMARK 0xFFFFFFFF00000000ULL
#define SQNUM_WATERMARK      0xFFFFFFFFFF000000ULL

/*
 * Minimum amount of LEBs reserved for the index. At present the index needs at
 * least 2 LEBs: one for the index head and one for in-the-gaps method (which
 * currently does not cater for the index head and so excludes it from
 * consideration).
 */
#define MIN_INDEX_LEBS 2

/* Maximum logical eraseblock size in bytes */
#define UBIFS_MAX_LEB_SZ (2*1024*1024)

/* Minimum amount of data UBIFS writes to the flash */
#define MIN_WRITE_SZ (UBIFS_DATA_NODE_SZ + 8)

/*
 * Currently we do not support inode number overlapping and re-using, so this
 * watermark defines dangerous inode number level. This should be fixed later,
 * although it is difficult to exceed current limit. Another option is to use
 * 64-bit inode numbers, but this means more overhead.
 */
#define INUM_WARN_WATERMARK 0xFFF00000
#define INUM_WATERMARK      0xFFFFFF00

/* Maximum number of entries in each LPT (LEB category) heap */
#define LPT_HEAP_SZ 256

/* Maximum possible inode number (only 32-bit inodes are supported now) */
#define MAX_INUM 0xFFFFFFFF

/* Number of non-data journal heads */
#define NONDATA_JHEADS_CNT 2

/* Shorter names for journal head numbers for internal usage */
#define GCHD   UBIFS_GC_HEAD
#define BASEHD UBIFS_BASE_HEAD
#define DATAHD UBIFS_DATA_HEAD

/* 'No change' value for 'ubifs_change_lp()' */
#define LPROPS_NC 0x80000001

/*
 * There is no notion of truncation key because truncation nodes do not exist
 * in TNC. However, when replaying, it is handy to introduce fake "truncation"
 * keys for truncation nodes because the code becomes simpler. So we define
 * %UBIFS_TRUN_KEY type.
 *
 * But otherwise, out of the journal reply scope, the truncation keys are
 * invalid.
 */
#define UBIFS_TRUN_KEY    UBIFS_KEY_TYPES_CNT
#define UBIFS_INVALID_KEY UBIFS_KEY_TYPES_CNT

/*
 * How much a directory entry/extended attribute entry adds to the parent/host
 * inode.
 */
#define CALC_DENT_SIZE(name_len) ALIGN(UBIFS_DENT_NODE_SZ + (name_len) + 1, 8)

/* How much an extended attribute adds to the host inode */
#define CALC_XATTR_BYTES(data_len) ALIGN(UBIFS_INO_NODE_SZ + (data_len) + 1, 8)

/* Maximum expected tree height for use by bottom_up_buf */
#define BOTTOM_UP_HEIGHT 64

#define UBIFS_HASH_ARR_SZ UBIFS_MAX_HASH_LEN
#define UBIFS_HMAC_ARR_SZ UBIFS_MAX_HMAC_LEN

/*
 * Znode flags (actually, bit numbers which store the flags).
 *
 * DIRTY_ZNODE: znode is dirty
 * COW_ZNODE: znode is being committed and a new instance of this znode has to
 *            be created before changing this znode
 * OBSOLETE_ZNODE: znode is obsolete, which means it was deleted, but it is
 *                 still in the commit list and the ongoing commit operation
 *                 will commit it, and delete this znode after it is done
 */
enum {
	DIRTY_ZNODE    = 0,
	COW_ZNODE      = 1,
	OBSOLETE_ZNODE = 2,
};

/*
 * Commit states.
 *
 * COMMIT_RESTING: commit is not wanted
 * COMMIT_BACKGROUND: background commit has been requested
 * COMMIT_REQUIRED: commit is required
 * COMMIT_RUNNING_BACKGROUND: background commit is running
 * COMMIT_RUNNING_REQUIRED: commit is running and it is required
 * COMMIT_BROKEN: commit failed
 */
enum {
	COMMIT_RESTING = 0,
	COMMIT_BACKGROUND,
	COMMIT_REQUIRED,
	COMMIT_RUNNING_BACKGROUND,
	COMMIT_RUNNING_REQUIRED,
	COMMIT_BROKEN,
};

/*
 * 'ubifs_scan_a_node()' return values.
 *
 * SCANNED_GARBAGE:  scanned garbage
 * SCANNED_EMPTY_SPACE: scanned empty space
 * SCANNED_A_NODE: scanned a valid node
 * SCANNED_A_CORRUPT_NODE: scanned a corrupted node
 * SCANNED_A_BAD_PAD_NODE: scanned a padding node with invalid pad length
 *
 * Greater than zero means: 'scanned that number of padding bytes'
 */
enum {
	SCANNED_GARBAGE        = 0,
	SCANNED_EMPTY_SPACE    = -1,
	SCANNED_A_NODE         = -2,
	SCANNED_A_CORRUPT_NODE = -3,
	SCANNED_A_BAD_PAD_NODE = -4,
};

/*
 * LPT cnode flag bits.
 *
 * DIRTY_CNODE: cnode is dirty
 * OBSOLETE_CNODE: cnode is being committed and has been copied (or deleted),
 *                 so it can (and must) be freed when the commit is finished
 * COW_CNODE: cnode is being committed and must be copied before writing
 */
enum {
	DIRTY_CNODE    = 0,
	OBSOLETE_CNODE = 1,
	COW_CNODE      = 2,
};

/*
 * Dirty flag bits (lpt_drty_flgs) for LPT special nodes.
 *
 * LTAB_DIRTY: ltab node is dirty
 * LSAVE_DIRTY: lsave node is dirty
 */
enum {
	LTAB_DIRTY  = 1,
	LSAVE_DIRTY = 2,
};

/*
 * Return codes used by the garbage collector.
 * @LEB_FREED: the logical eraseblock was freed and is ready to use
 * @LEB_FREED_IDX: indexing LEB was freed and can be used only after the commit
 * @LEB_RETAINED: the logical eraseblock was freed and retained for GC purposes
 */
enum {
	LEB_FREED,
	LEB_FREED_IDX,
	LEB_RETAINED,
};

/**
 * struct ubifs_old_idx - index node obsoleted since last commit start.
 * @rb: rb-tree node
 * @lnum: LEB number of obsoleted index node
 * @offs: offset of obsoleted index node
 */
struct ubifs_old_idx {
	struct rb_node rb;
	int lnum;
	int offs;
};

/* The below union makes it easier to deal with keys */
union ubifs_key {
	uint8_t u8[UBIFS_SK_LEN];
	uint32_t u32[UBIFS_SK_LEN/4];
	uint64_t u64[UBIFS_SK_LEN/8];
	__le32 j32[UBIFS_SK_LEN/4];
};

/**
 * struct ubifs_scan_node - UBIFS scanned node information.
 * @list: list of scanned nodes
 * @key: key of node scanned (if it has one)
 * @sqnum: sequence number
 * @type: type of node scanned
 * @offs: offset with LEB of node scanned
 * @len: length of node scanned
 * @node: raw node
 */
struct ubifs_scan_node {
	struct list_head list;
	union ubifs_key key;
	unsigned long long sqnum;
	int type;
	int offs;
	int len;
	void *node;
};

/**
 * struct ubifs_scan_leb - UBIFS scanned LEB information.
 * @lnum: logical eraseblock number
 * @nodes_cnt: number of nodes scanned
 * @nodes: list of struct ubifs_scan_node
 * @endpt: end point (and therefore the start of empty space)
 * @buf: buffer containing entire LEB scanned
 */
struct ubifs_scan_leb {
	int lnum;
	int nodes_cnt;
	struct list_head nodes;
	int endpt;
	void *buf;
};

/**
 * struct ubifs_gced_idx_leb - garbage-collected indexing LEB.
 * @list: list
 * @lnum: LEB number
 * @unmap: OK to unmap this LEB
 *
 * This data structure is used to temporary store garbage-collected indexing
 * LEBs - they are not released immediately, but only after the next commit.
 * This is needed to guarantee recoverability.
 */
struct ubifs_gced_idx_leb {
	struct list_head list;
	int lnum;
	int unmap;
};

/**
 * struct inode - inode description.
 * @uid: owner ID
 * @gid: group ID
 * @mode: access flags
 * @nlink: number of hard links
 * @inum: inode number
 * @atime_sec: access time seconds
 * @ctime_sec: creation time seconds
 * @mtime_sec: modification time seconds
 * @atime_nsec: access time nanoseconds
 * @ctime_nsec: creation time nanoseconds
 * @mtime_nsec: modification time nanoseconds
 */
struct inode {
	unsigned int uid;
	unsigned int gid;
	unsigned int mode;
	unsigned int nlink;
	ino_t inum;
	unsigned long long atime_sec;
	unsigned long long ctime_sec;
	unsigned long long mtime_sec;
	unsigned int atime_nsec;
	unsigned int ctime_nsec;
	unsigned int mtime_nsec;
};

/**
 * struct ubifs_inode - UBIFS in-memory inode description.
 * @vfs_inode: VFS inode description object
 * @creat_sqnum: sequence number at time of creation
 * @xattr_size: summarized size of all extended attributes in bytes
 * @xattr_cnt: count of extended attributes this inode has
 * @xattr_names: sum of lengths of all extended attribute names belonging to
 *               this inode
 * @ui_size: inode size used by UBIFS when writing to flash
 * @flags: inode flags (@UBIFS_COMPR_FL, etc)
 * @compr_type: default compression type used for this inode
 * @data_len: length of the data attached to the inode
 * @data: inode's data
 */
struct ubifs_inode {
	struct inode vfs_inode;
	unsigned long long creat_sqnum;
	unsigned int xattr_size;
	unsigned int xattr_cnt;
	unsigned int xattr_names;
	unsigned int compr_type:2;
	loff_t ui_size;
	int flags;
	int data_len;
	void *data;
};

/**
 * struct ubifs_unclean_leb - records a LEB recovered under read-only mode.
 * @list: list
 * @lnum: LEB number of recovered LEB
 * @endpt: offset where recovery ended
 *
 * This structure records a LEB identified during recovery that needs to be
 * cleaned but was not because UBIFS was mounted read-only. The information
 * is used to clean the LEB when remounting to read-write mode.
 */
struct ubifs_unclean_leb {
	struct list_head list;
	int lnum;
	int endpt;
};

/*
 * LEB properties flags.
 *
 * LPROPS_UNCAT: not categorized
 * LPROPS_DIRTY: dirty > free, dirty >= @c->dead_wm, not index
 * LPROPS_DIRTY_IDX: dirty + free > @c->min_idx_node_sze and index
 * LPROPS_FREE: free > 0, dirty < @c->dead_wm, not empty, not index
 * LPROPS_HEAP_CNT: number of heaps used for storing categorized LEBs
 * LPROPS_EMPTY: LEB is empty, not taken
 * LPROPS_FREEABLE: free + dirty == leb_size, not index, not taken
 * LPROPS_FRDI_IDX: free + dirty == leb_size and index, may be taken
 * LPROPS_CAT_MASK: mask for the LEB categories above
 * LPROPS_TAKEN: LEB was taken (this flag is not saved on the media)
 * LPROPS_INDEX: LEB contains indexing nodes (this flag also exists on flash)
 */
enum {
	LPROPS_UNCAT     =  0,
	LPROPS_DIRTY     =  1,
	LPROPS_DIRTY_IDX =  2,
	LPROPS_FREE      =  3,
	LPROPS_HEAP_CNT  =  3,
	LPROPS_EMPTY     =  4,
	LPROPS_FREEABLE  =  5,
	LPROPS_FRDI_IDX  =  6,
	LPROPS_CAT_MASK  = 15,
	LPROPS_TAKEN     = 16,
	LPROPS_INDEX     = 32,
};

/**
 * struct ubifs_lprops - logical eraseblock properties.
 * @free: amount of free space in bytes
 * @dirty: amount of dirty space in bytes
 * @flags: LEB properties flags (see above)
 * @lnum: LEB number
 * @end: the end postition of LEB calculated by the last node
 * @used: amount of used space in bytes
 * @list: list of same-category lprops (for LPROPS_EMPTY and LPROPS_FREEABLE)
 * @hpos: heap position in heap of same-category lprops (other categories)
 */
struct ubifs_lprops {
	int free;
	int dirty;
	int flags;
	int lnum;
	int end;
	int used;
	union {
		struct list_head list;
		int hpos;
	};
};

/**
 * struct ubifs_lpt_lprops - LPT logical eraseblock properties.
 * @free: amount of free space in bytes
 * @dirty: amount of dirty space in bytes
 * @tgc: trivial GC flag (1 => unmap after commit end)
 * @cmt: commit flag (1 => reserved for commit)
 */
struct ubifs_lpt_lprops {
	int free;
	int dirty;
	unsigned tgc:1;
	unsigned cmt:1;
};

/**
 * struct ubifs_lp_stats - statistics of eraseblocks in the main area.
 * @empty_lebs: number of empty LEBs
 * @taken_empty_lebs: number of taken LEBs
 * @idx_lebs: number of indexing LEBs
 * @total_free: total free space in bytes (includes all LEBs)
 * @total_dirty: total dirty space in bytes (includes all LEBs)
 * @total_used: total used space in bytes (does not include index LEBs)
 * @total_dead: total dead space in bytes (does not include index LEBs)
 * @total_dark: total dark space in bytes (does not include index LEBs)
 *
 * The @taken_empty_lebs field counts the LEBs that are in the transient state
 * of having been "taken" for use but not yet written to. @taken_empty_lebs is
 * needed to account correctly for @gc_lnum, otherwise @empty_lebs could be
 * used by itself (in which case 'unused_lebs' would be a better name). In the
 * case of @gc_lnum, it is "taken" at mount time or whenever a LEB is retained
 * by GC, but unlike other empty LEBs that are "taken", it may not be written
 * straight away (i.e. before the next commit start or unmount), so either
 * @gc_lnum must be specially accounted for, or the current approach followed
 * i.e. count it under @taken_empty_lebs.
 *
 * @empty_lebs includes @taken_empty_lebs.
 *
 * @total_used, @total_dead and @total_dark fields do not account indexing
 * LEBs.
 */
struct ubifs_lp_stats {
	int empty_lebs;
	int taken_empty_lebs;
	int idx_lebs;
	long long total_free;
	long long total_dirty;
	long long total_used;
	long long total_dead;
	long long total_dark;
};

struct ubifs_nnode;

/**
 * struct ubifs_cnode - LEB Properties Tree common node.
 * @parent: parent nnode
 * @cnext: next cnode to commit
 * @flags: flags (%DIRTY_LPT_NODE or %OBSOLETE_LPT_NODE)
 * @iip: index in parent
 * @level: level in the tree (zero for pnodes, greater than zero for nnodes)
 * @num: node number
 */
struct ubifs_cnode {
	struct ubifs_nnode *parent;
	struct ubifs_cnode *cnext;
	unsigned long flags;
	int iip;
	int level;
	int num;
};

/**
 * struct ubifs_pnode - LEB Properties Tree leaf node.
 * @parent: parent nnode
 * @cnext: next cnode to commit
 * @flags: flags (%DIRTY_LPT_NODE or %OBSOLETE_LPT_NODE)
 * @iip: index in parent
 * @level: level in the tree (always zero for pnodes)
 * @num: node number
 * @lprops: LEB properties array
 */
struct ubifs_pnode {
	struct ubifs_nnode *parent;
	struct ubifs_cnode *cnext;
	unsigned long flags;
	int iip;
	int level;
	int num;
	struct ubifs_lprops lprops[UBIFS_LPT_FANOUT];
};

/**
 * struct ubifs_nbranch - LEB Properties Tree internal node branch.
 * @lnum: LEB number of child
 * @offs: offset of child
 * @nnode: nnode child
 * @pnode: pnode child
 * @cnode: cnode child
 */
struct ubifs_nbranch {
	int lnum;
	int offs;
	union {
		struct ubifs_nnode *nnode;
		struct ubifs_pnode *pnode;
		struct ubifs_cnode *cnode;
	};
};

/**
 * struct ubifs_nnode - LEB Properties Tree internal node.
 * @parent: parent nnode
 * @cnext: next cnode to commit
 * @flags: flags (%DIRTY_LPT_NODE or %OBSOLETE_LPT_NODE)
 * @iip: index in parent
 * @level: level in the tree (always greater than zero for nnodes)
 * @num: node number
 * @nbranch: branches to child nodes
 */
struct ubifs_nnode {
	struct ubifs_nnode *parent;
	struct ubifs_cnode *cnext;
	unsigned long flags;
	int iip;
	int level;
	int num;
	struct ubifs_nbranch nbranch[UBIFS_LPT_FANOUT];
};

/**
 * struct ubifs_lpt_heap - heap of categorized lprops.
 * @arr: heap array
 * @cnt: number in heap
 * @max_cnt: maximum number allowed in heap
 *
 * There are %LPROPS_HEAP_CNT heaps.
 */
struct ubifs_lpt_heap {
	struct ubifs_lprops **arr;
	int cnt;
	int max_cnt;
};

/*
 * Return codes for LPT scan callback function.
 *
 * LPT_SCAN_CONTINUE: continue scanning
 * LPT_SCAN_ADD: add the LEB properties scanned to the tree in memory
 * LPT_SCAN_STOP: stop scanning
 */
enum {
	LPT_SCAN_CONTINUE = 0,
	LPT_SCAN_ADD = 1,
	LPT_SCAN_STOP = 2,
};

struct ubifs_info;

/* Callback used by the 'ubifs_lpt_scan_nolock()' function */
typedef int (*ubifs_lpt_scan_callback)(struct ubifs_info *c,
				       const struct ubifs_lprops *lprops,
				       int in_tree, void *data);

/**
 * struct ubifs_wbuf - UBIFS write-buffer.
 * @c: UBIFS file-system description object
 * @buf: write-buffer (of min. flash I/O unit size)
 * @lnum: logical eraseblock number the write-buffer points to
 * @offs: write-buffer offset in this logical eraseblock
 * @avail: number of bytes available in the write-buffer
 * @used:  number of used bytes in the write-buffer
 * @size: write-buffer size (in [@c->min_io_size, @c->max_write_size] range)
 * @jhead: journal head the mutex belongs to (note, needed only to shut lockdep
 *         up by 'mutex_lock_nested()).
 * @sync_callback: write-buffer synchronization callback
 * @io_mutex: serializes write-buffer I/O
 * @lock: serializes @buf, @lnum, @offs, @avail, @used, @next_ino and @inodes
 *        fields
 * @next_ino: points to the next position of the following inode number
 * @inodes: stores the inode numbers of the nodes which are in wbuf
 *
 * The write-buffer synchronization callback is called when the write-buffer is
 * synchronized in order to notify how much space was wasted due to
 * write-buffer padding and how much free space is left in the LEB.
 *
 * Note: the fields @buf, @lnum, @offs, @avail and @used can be read under
 * spin-lock or mutex because they are written under both mutex and spin-lock.
 * @buf is appended to under mutex but overwritten under both mutex and
 * spin-lock. Thus the data between @buf and @buf + @used can be read under
 * spinlock.
 */
struct ubifs_wbuf {
	struct ubifs_info *c;
	void *buf;
	int lnum;
	int offs;
	int avail;
	int used;
	int size;
	int jhead;
	int (*sync_callback)(struct ubifs_info *c, int lnum, int free, int pad);
	struct mutex io_mutex;
	spinlock_t lock;
	int next_ino;
	ino_t *inodes;
};

/**
 * struct ubifs_bud - bud logical eraseblock.
 * @lnum: logical eraseblock number
 * @start: where the (uncommitted) bud data starts
 * @jhead: journal head number this bud belongs to
 * @list: link in the list buds belonging to the same journal head
 * @rb: link in the tree of all buds
 * @log_hash: the log hash from the commit start node up to this bud
 */
struct ubifs_bud {
	int lnum;
	int start;
	int jhead;
	struct list_head list;
	struct rb_node rb;
	struct shash_desc *log_hash;
};

/**
 * struct ubifs_jhead - journal head.
 * @wbuf: head's write-buffer
 * @buds_list: list of bud LEBs belonging to this journal head
 * @grouped: non-zero if UBIFS groups nodes when writing to this journal head
 * @log_hash: the log hash from the commit start node up to this journal head
 *
 * Note, the @buds list is protected by the @c->buds_lock.
 */
struct ubifs_jhead {
	struct ubifs_wbuf wbuf;
	struct list_head buds_list;
	unsigned int grouped:1;
	struct shash_desc *log_hash;
};

/**
 * struct ubifs_zbranch - key/coordinate/length branch stored in znodes.
 * @key: key
 * @znode: znode address in memory
 * @lnum: LEB number of the target node (indexing node or data node)
 * @offs: target node offset within @lnum
 * @len: target node length
 * @hash: the hash of the target node
 */
struct ubifs_zbranch {
	union ubifs_key key;
	union {
		struct ubifs_znode *znode;
		void *leaf;
	};
	int lnum;
	int offs;
	int len;
	u8 hash[UBIFS_HASH_ARR_SZ];
};

/**
 * struct ubifs_znode - in-memory representation of an indexing node.
 * @parent: parent znode or NULL if it is the root
 * @cnext: next znode to commit
 * @cparent: parent node for this commit
 * @ciip: index in cparent's zbranch array
 * @flags: znode flags (%DIRTY_ZNODE, %COW_ZNODE or %OBSOLETE_ZNODE)
 * @time: last access time (seconds)
 * @level: level of the entry in the TNC tree
 * @child_cnt: count of child znodes
 * @iip: index in parent's zbranch array
 * @alt: lower bound of key range has altered i.e. child inserted at slot 0
 * @lnum: LEB number of the corresponding indexing node
 * @offs: offset of the corresponding indexing node
 * @len: length  of the corresponding indexing node
 * @zbranch: array of znode branches (@c->fanout elements)
 *
 * Note! The @lnum, @offs, and @len fields are not really needed - we have them
 * only for internal consistency check. They could be removed to save some RAM.
 */
struct ubifs_znode {
	struct ubifs_znode *parent;
	struct ubifs_znode *cnext;
	struct ubifs_znode *cparent;
	int ciip;
	unsigned long flags;
	time64_t time;
	int level;
	int child_cnt;
	int iip;
	int alt;
	int lnum;
	int offs;
	int len;
	struct ubifs_zbranch zbranch[];
};

/**
 * struct ubifs_node_range - node length range description data structure.
 * @len: fixed node length
 * @min_len: minimum possible node length
 * @max_len: maximum possible node length
 *
 * If @max_len is %0, the node has fixed length @len.
 */
struct ubifs_node_range {
	union {
		int len;
		int min_len;
	};
	int max_len;
};

/**
 * struct ubifs_budget_req - budget requirements of an operation.
 *
 * @fast: non-zero if the budgeting should try to acquire budget quickly and
 *        should not try to call write-back
 * @recalculate: non-zero if @idx_growth, @data_growth, and @dd_growth fields
 *               have to be re-calculated
 * @new_page: non-zero if the operation adds a new page
 * @dirtied_page: non-zero if the operation makes a page dirty
 * @new_dent: non-zero if the operation adds a new directory entry
 * @mod_dent: non-zero if the operation removes or modifies an existing
 *            directory entry
 * @new_ino: non-zero if the operation adds a new inode
 * @new_ino_d: how much data newly created inode contains
 * @dirtied_ino: how many inodes the operation makes dirty
 * @dirtied_ino_d: how much data dirtied inode contains
 * @idx_growth: how much the index will supposedly grow
 * @data_growth: how much new data the operation will supposedly add
 * @dd_growth: how much data that makes other data dirty the operation will
 *             supposedly add
 *
 * @idx_growth, @data_growth and @dd_growth are not used in budget request. The
 * budgeting subsystem caches index and data growth values there to avoid
 * re-calculating them when the budget is released. However, if @idx_growth is
 * %-1, it is calculated by the release function using other fields.
 *
 * An inode may contain 4KiB of data at max., thus the widths of @new_ino_d
 * is 13 bits, and @dirtied_ino_d - 15, because up to 4 inodes may be made
 * dirty by the re-name operation.
 *
 * Note, UBIFS aligns node lengths to 8-bytes boundary, so the requester has to
 * make sure the amount of inode data which contribute to @new_ino_d and
 * @dirtied_ino_d fields are aligned.
 */
struct ubifs_budget_req {
	unsigned int fast:1;
	unsigned int recalculate:1;
#ifndef UBIFS_DEBUG
	unsigned int new_page:1;
	unsigned int dirtied_page:1;
	unsigned int new_dent:1;
	unsigned int mod_dent:1;
	unsigned int new_ino:1;
	unsigned int new_ino_d:13;
	unsigned int dirtied_ino:4;
	unsigned int dirtied_ino_d:15;
#else
	/* Not bit-fields to check for overflows */
	unsigned int new_page;
	unsigned int dirtied_page;
	unsigned int new_dent;
	unsigned int mod_dent;
	unsigned int new_ino;
	unsigned int new_ino_d;
	unsigned int dirtied_ino;
	unsigned int dirtied_ino_d;
#endif
	int idx_growth;
	int data_growth;
	int dd_growth;
};

/**
 * struct ubifs_orphan - stores the inode number of an orphan.
 * @rb: rb-tree node of rb-tree of orphans sorted by inode number
 * @list: list head of list of orphans in order added
 * @new_list: list head of list of orphans added since the last commit
 * @cnext: next orphan to commit
 * @dnext: next orphan to delete
 * @inum: inode number
 * @new: %1 => added since the last commit, otherwise %0
 * @cmt: %1 => commit pending, otherwise %0
 * @del: %1 => delete pending, otherwise %0
 */
struct ubifs_orphan {
	struct rb_node rb;
	struct list_head list;
	struct list_head new_list;
	struct ubifs_orphan *cnext;
	struct ubifs_orphan *dnext;
	ino_t inum;
	unsigned new:1;
	unsigned cmt:1;
	unsigned del:1;
};

/**
 * struct ubifs_budg_info - UBIFS budgeting information.
 * @idx_growth: amount of bytes budgeted for index growth
 * @data_growth: amount of bytes budgeted for cached data
 * @dd_growth: amount of bytes budgeted for cached data that will make
 *             other data dirty
 * @uncommitted_idx: amount of bytes were budgeted for growth of the index, but
 *                   which still have to be taken into account because the index
 *                   has not been committed so far
 * @old_idx_sz: size of index on flash
 * @min_idx_lebs: minimum number of LEBs required for the index
 * @nospace: non-zero if the file-system does not have flash space (used as
 *           optimization)
 * @nospace_rp: the same as @nospace, but additionally means that even reserved
 *              pool is full
 * @page_budget: budget for a page (constant, never changed after mount)
 * @inode_budget: budget for an inode (constant, never changed after mount)
 * @dent_budget: budget for a directory entry (constant, never changed after
 *               mount)
 */
struct ubifs_budg_info {
	long long idx_growth;
	long long data_growth;
	long long dd_growth;
	long long uncommitted_idx;
	unsigned long long old_idx_sz;
	int min_idx_lebs;
	unsigned int nospace:1;
	unsigned int nospace_rp:1;
	int page_budget;
	int inode_budget;
	int dent_budget;
};

/**
 * struct ubifs_info - UBIFS file-system description data structure
 * (per-superblock).
 *
 * @sup_node: The super block node as read from the device
 *
 * @highest_inum: highest used inode number
 * @max_sqnum: current global sequence number
 * @cmt_no: commit number of the last successfully completed commit, protected
 *          by @commit_sem
 * @cnt_lock: protects @highest_inum and @max_sqnum counters
 * @fmt_version: UBIFS on-flash format version
 * @ro_compat_version: R/O compatibility version
 *
 * @debug_level: level of debug messages, 0 - none, 1 - error message,
 *		 2 - warning message, 3 - notice message, 4 - debug message
 * @program_type: used to identify the type of current program
 * @program_name: program name
 * @dev_name: device name
 * @dev_fd: opening handler for an UBI volume or an image file
 * @libubi: opening handler for libubi
 *
 * @lhead_lnum: log head logical eraseblock number
 * @lhead_offs: log head offset
 * @ltail_lnum: log tail logical eraseblock number (offset is always 0)
 * @log_mutex: protects the log, @lhead_lnum, @lhead_offs, @ltail_lnum, and
 *             @bud_bytes
 * @min_log_bytes: minimum required number of bytes in the log
 * @cmt_bud_bytes: used during commit to temporarily amount of bytes in
 *                 committed buds
 *
 * @buds: tree of all buds indexed by bud LEB number
 * @bud_bytes: how many bytes of flash is used by buds
 * @buds_lock: protects the @buds tree, @bud_bytes, and per-journal head bud
 *             lists
 * @jhead_cnt: count of journal heads
 * @jheads: journal heads (head zero is base head)
 * @max_bud_bytes: maximum number of bytes allowed in buds
 * @bg_bud_bytes: number of bud bytes when background commit is initiated
 * @old_buds: buds to be released after commit ends
 * @max_bud_cnt: maximum number of buds
 *
 * @commit_sem: synchronizes committer with other processes
 * @cmt_state: commit state
 * @cs_lock: commit state lock
 *
 * @big_lpt: flag that LPT is too big to write whole during commit
 * @space_fixup: flag indicating that free space in LEBs needs to be cleaned up
 * @double_hash: flag indicating that we can do lookups by hash
 * @encrypted: flag indicating that this file system contains encrypted files
 * @no_chk_data_crc: do not check CRCs when reading data nodes (except during
 *                   recovery)
 * @authenticated: flag indigating the FS is mounted in authenticated mode
 * @superblock_need_write: flag indicating that we need to write superblock node
 *
 * @tnc_mutex: protects the Tree Node Cache (TNC), @zroot, @cnext, @enext, and
 *             @calc_idx_sz
 * @zroot: zbranch which points to the root index node and znode
 * @cnext: next znode to commit
 * @enext: next znode to commit to empty space
 * @gap_lebs: array of LEBs used by the in-gaps commit method
 * @cbuf: commit buffer
 * @ileb_buf: buffer for commit in-the-gaps method
 * @ileb_len: length of data in ileb_buf
 * @ihead_lnum: LEB number of index head
 * @ihead_offs: offset of index head
 * @ilebs: pre-allocated index LEBs
 * @ileb_cnt: number of pre-allocated index LEBs
 * @ileb_nxt: next pre-allocated index LEBs
 * @old_idx: tree of index nodes obsoleted since the last commit start
 * @bottom_up_buf: a buffer which is used by 'dirty_cow_bottom_up()' in tnc.c
 *
 * @mst_node: master node
 * @mst_offs: offset of valid master node
 *
 * @log_lebs: number of logical eraseblocks in the log
 * @log_bytes: log size in bytes
 * @log_last: last LEB of the log
 * @lpt_lebs: number of LEBs used for lprops table
 * @lpt_first: first LEB of the lprops table area
 * @lpt_last: last LEB of the lprops table area
 * @orph_lebs: number of LEBs used for the orphan area
 * @orph_first: first LEB of the orphan area
 * @orph_last: last LEB of the orphan area
 * @main_lebs: count of LEBs in the main area
 * @main_first: first LEB of the main area
 * @main_bytes: main area size in bytes
 * @default_compr: default compression type
 * @favor_lzo: favor LZO compression method
 * @favor_percent: lzo vs. zlib threshold used in case favor LZO
 *
 * @key_hash_type: type of the key hash
 * @key_hash: direntry key hash function
 * @key_fmt: key format
 * @key_len: key length
 * @fanout: fanout of the index tree (number of links per indexing node)
 *
 * @min_io_size: minimal input/output unit size
 * @min_io_shift: number of bits in @min_io_size minus one
 * @max_write_size: maximum amount of bytes the underlying flash can write at a
 *                  time (MTD write buffer size)
 * @max_write_shift: number of bits in @max_write_size minus one
 * @leb_size: logical eraseblock size in bytes
 * @half_leb_size: half LEB size
 * @idx_leb_size: how many bytes of an LEB are effectively available when it is
 *                used to store indexing nodes (@leb_size - @max_idx_node_sz)
 * @leb_cnt: count of logical eraseblocks
 * @max_leb_cnt: maximum count of logical eraseblocks
 * @ro_media: the underlying UBI volume is read-only
 * @ro_mount: the file-system was mounted as read-only
 * @ro_error: UBIFS switched to R/O mode because an error happened
 *
 * @dirty_pg_cnt: number of dirty pages (not used)
 * @dirty_zn_cnt: number of dirty znodes
 * @clean_zn_cnt: number of clean znodes
 *
 * @space_lock: protects @bi and @lst
 * @lst: lprops statistics
 * @bi: budgeting information
 * @calc_idx_sz: temporary variable which is used to calculate new index size
 *               (contains accurate new index size at end of TNC commit start)
 *
 * @ref_node_alsz: size of the LEB reference node aligned to the min. flash
 *                 I/O unit
 * @mst_node_alsz: master node aligned size
 * @min_idx_node_sz: minimum indexing node aligned on 8-bytes boundary
 * @max_idx_node_sz: maximum indexing node aligned on 8-bytes boundary
 * @max_inode_sz: maximum possible inode size in bytes
 * @max_znode_sz: size of znode in bytes
 *
 * @leb_overhead: how many bytes are wasted in an LEB when it is filled with
 *                data nodes of maximum size - used in free space reporting
 * @dead_wm: LEB dead space watermark
 * @dark_wm: LEB dark space watermark
 *
 * @ranges: UBIFS node length ranges
 * @di: UBI device information
 * @vi: UBI volume information
 *
 * @orph_tree: rb-tree of orphan inode numbers
 * @orph_list: list of orphan inode numbers in order added
 * @orph_new: list of orphan inode numbers added since last commit
 * @orph_cnext: next orphan to commit
 * @orph_dnext: next orphan to delete
 * @orphan_lock: lock for orph_tree and orph_new
 * @orph_buf: buffer for orphan nodes
 * @new_orphans: number of orphans since last commit
 * @cmt_orphans: number of orphans being committed
 * @tot_orphans: number of orphans in the rb_tree
 * @max_orphans: maximum number of orphans allowed
 * @ohead_lnum: orphan head LEB number
 * @ohead_offs: orphan head offset
 * @no_orphs: non-zero if there are no orphans
 *
 * @gc_lnum: LEB number used for garbage collection
 * @sbuf: a buffer of LEB size used by GC and replay for scanning
 * @idx_gc: list of index LEBs that have been garbage collected
 * @idx_gc_cnt: number of elements on the idx_gc list
 * @gc_seq: incremented for every non-index LEB garbage collected
 * @gced_lnum: last non-index LEB that was garbage collected
 *
 * @space_bits: number of bits needed to record free or dirty space
 * @lpt_lnum_bits: number of bits needed to record a LEB number in the LPT
 * @lpt_offs_bits: number of bits needed to record an offset in the LPT
 * @lpt_spc_bits: number of bits needed to space in the LPT
 * @pcnt_bits: number of bits needed to record pnode or nnode number
 * @lnum_bits: number of bits needed to record LEB number
 * @nnode_sz: size of on-flash nnode
 * @pnode_sz: size of on-flash pnode
 * @ltab_sz: size of on-flash LPT lprops table
 * @lsave_sz: size of on-flash LPT save table
 * @pnode_cnt: number of pnodes
 * @nnode_cnt: number of nnodes
 * @lpt_hght: height of the LPT
 * @pnodes_have: number of pnodes in memory
 *
 * @lp_mutex: protects lprops table and all the other lprops-related fields
 * @lpt_lnum: LEB number of the root nnode of the LPT
 * @lpt_offs: offset of the root nnode of the LPT
 * @nhead_lnum: LEB number of LPT head
 * @nhead_offs: offset of LPT head
 * @lpt_drty_flgs: dirty flags for LPT special nodes e.g. ltab
 * @dirty_nn_cnt: number of dirty nnodes
 * @dirty_pn_cnt: number of dirty pnodes
 * @check_lpt_free: flag that indicates LPT GC may be needed
 * @lpt_sz: LPT size
 * @lpt_nod_buf: buffer for an on-flash nnode or pnode
 * @lpt_buf: buffer of LEB size used by LPT
 * @nroot: address in memory of the root nnode of the LPT
 * @lpt_cnext: next LPT node to commit
 * @lpt_heap: array of heaps of categorized lprops
 * @dirty_idx: a (reverse sorted) copy of the LPROPS_DIRTY_IDX heap as at
 *             previous commit start
 * @uncat_list: list of un-categorized LEBs
 * @empty_list: list of empty LEBs
 * @freeable_list: list of freeable non-index LEBs (free + dirty == @leb_size)
 * @frdi_idx_list: list of freeable index LEBs (free + dirty == @leb_size)
 * @freeable_cnt: number of freeable LEBs in @freeable_list
 * @in_a_category_cnt: count of lprops which are in a certain category, which
 *                     basically meants that they were loaded from the flash
 *
 * @ltab_lnum: LEB number of LPT's own lprops table
 * @ltab_offs: offset of LPT's own lprops table
 * @lpt: lprops table
 * @ltab: LPT's own lprops table
 * @ltab_cmt: LPT's own lprops table (commit copy)
 * @lsave_cnt: number of LEB numbers in LPT's save table
 * @lsave_lnum: LEB number of LPT's save table
 * @lsave_offs: offset of LPT's save table
 * @lsave: LPT's save table
 * @lscan_lnum: LEB number of last LPT scan
 *
 * @rp_size: reserved pool size
 *
 * @hash_algo_name: the name of the hashing algorithm to use
 * @hash_algo: The hash algo number (from include/linux/hash_info.h)
 * @auth_key_filename: authentication key file name
 * @x509_filename: x509 certificate file name for authentication
 * @hash_len: the length of the hash
 * @root_idx_hash: The hash of the root index node
 * @lpt_hash: The hash of the LPT
 * @mst_hash: The hash of the master node
 * @log_hash: the log hash from the commit start node up to the latest reference
 *            node.
 *
 * @need_recovery: %1 if the file-system needs recovery
 * @replaying: %1 during journal replay
 * @mounting: %1 while mounting
 * @remounting_rw: %1 while re-mounting from R/O mode to R/W mode
 * @replay_list: temporary list used during journal replay
 * @replay_buds: list of buds to replay
 * @cs_sqnum: sequence number of first node in the log (commit start node)
 * @unclean_leb_list: LEBs to recover when re-mounting R/O mounted FS to R/W
 *                    mode
 * @rcvrd_mst_node: recovered master node to write when re-mounting R/O mounted
 *                  FS to R/W mode
 * @size_tree: inode size information for recovery
 *
 * @new_ihead_lnum: used by debugging to check @c->ihead_lnum
 * @new_ihead_offs: used by debugging to check @c->ihead_offs
 *
 * @private: private information related to specific situation, eg. fsck.
 * @assert_failed_cb: callback function to handle assertion failure
 * @set_failure_reason_cb: record reasons while certain failure happens
 * @get_failure_reason_cb: get failure reasons
 * @clear_failure_reason_cb: callback function to clear the error which is
 *			     caused by reading corrupted data or invalid lpt
 * @test_and_clear_failure_reason_cb: callback function to check and clear the
 *				      error which is caused by reading corrupted
 *				      data or invalid lpt
 * @set_lpt_invalid_cb: callback function to set the invalid lpt status
 * @test_lpt_valid_cb: callback function to check whether lpt is corrupted or
 *		       incorrect, should be called before updating lpt
 * @can_ignore_failure_cb: callback function to decide whether the failure
 *			   can be ignored
 * @handle_failure_cb: callback function to decide whether the failure can be
 *		       handled
 */
struct ubifs_info {
	struct ubifs_sb_node *sup_node;

	ino_t highest_inum;
	unsigned long long max_sqnum;
	unsigned long long cmt_no;
	spinlock_t cnt_lock;
	int fmt_version;
	int ro_compat_version;

	int debug_level;
	int program_type;
	const char *program_name;
	char *dev_name;
	int dev_fd;
	libubi_t libubi;

	int lhead_lnum;
	int lhead_offs;
	int ltail_lnum;
	struct mutex log_mutex;
	int min_log_bytes;
	long long cmt_bud_bytes;

	struct rb_root buds;
	long long bud_bytes;
	spinlock_t buds_lock;
	int jhead_cnt;
	struct ubifs_jhead *jheads;
	long long max_bud_bytes;
	long long bg_bud_bytes;
	struct list_head old_buds;
	int max_bud_cnt;

	struct rw_semaphore commit_sem;
	int cmt_state;
	spinlock_t cs_lock;

	unsigned int big_lpt:1;
	unsigned int space_fixup:1;
	unsigned int double_hash:1;
	unsigned int encrypted:1;
	unsigned int no_chk_data_crc:1;
	unsigned int authenticated:1;
	unsigned int superblock_need_write:1;

	struct mutex tnc_mutex;
	struct ubifs_zbranch zroot;
	struct ubifs_znode *cnext;
	struct ubifs_znode *enext;
	int *gap_lebs;
	void *cbuf;
	void *ileb_buf;
	int ileb_len;
	int ihead_lnum;
	int ihead_offs;
	int *ilebs;
	int ileb_cnt;
	int ileb_nxt;
	struct rb_root old_idx;
	int *bottom_up_buf;

	struct ubifs_mst_node *mst_node;
	int mst_offs;

	int log_lebs;
	long long log_bytes;
	int log_last;
	int lpt_lebs;
	int lpt_first;
	int lpt_last;
	int orph_lebs;
	int orph_first;
	int orph_last;
	int main_lebs;
	int main_first;
	long long main_bytes;
	int default_compr;
	int favor_lzo;
	int favor_percent;

	uint8_t key_hash_type;
	uint32_t (*key_hash)(const char *str, int len);
	int key_fmt;
	int key_len;
	int fanout;

	int min_io_size;
	int min_io_shift;
	int max_write_size;
	int max_write_shift;
	int leb_size;
	int half_leb_size;
	int idx_leb_size;
	int leb_cnt;
	int max_leb_cnt;
	unsigned int ro_media:1;
	unsigned int ro_mount:1;
	unsigned int ro_error:1;

	atomic_long_t dirty_pg_cnt;
	atomic_long_t dirty_zn_cnt;
	atomic_long_t clean_zn_cnt;

	spinlock_t space_lock;
	struct ubifs_lp_stats lst;
	struct ubifs_budg_info bi;
	unsigned long long calc_idx_sz;

	int ref_node_alsz;
	int mst_node_alsz;
	int min_idx_node_sz;
	int max_idx_node_sz;
	long long max_inode_sz;
	int max_znode_sz;

	int leb_overhead;
	int dead_wm;
	int dark_wm;

	struct ubifs_node_range ranges[UBIFS_NODE_TYPES_CNT];
	struct ubi_dev_info di;
	struct ubi_vol_info vi;

	struct rb_root orph_tree;
	struct list_head orph_list;
	struct list_head orph_new;
	struct ubifs_orphan *orph_cnext;
	struct ubifs_orphan *orph_dnext;
	spinlock_t orphan_lock;
	void *orph_buf;
	int new_orphans;
	int cmt_orphans;
	int tot_orphans;
	int max_orphans;
	int ohead_lnum;
	int ohead_offs;
	int no_orphs;

	int gc_lnum;
	void *sbuf;
	struct list_head idx_gc;
	int idx_gc_cnt;
	int gc_seq;
	int gced_lnum;

	int space_bits;
	int lpt_lnum_bits;
	int lpt_offs_bits;
	int lpt_spc_bits;
	int pcnt_bits;
	int lnum_bits;
	int nnode_sz;
	int pnode_sz;
	int ltab_sz;
	int lsave_sz;
	int pnode_cnt;
	int nnode_cnt;
	int lpt_hght;
	int pnodes_have;

	struct mutex lp_mutex;
	int lpt_lnum;
	int lpt_offs;
	int nhead_lnum;
	int nhead_offs;
	int lpt_drty_flgs;
	int dirty_nn_cnt;
	int dirty_pn_cnt;
	int check_lpt_free;
	long long lpt_sz;
	void *lpt_nod_buf;
	void *lpt_buf;
	struct ubifs_nnode *nroot;
	struct ubifs_cnode *lpt_cnext;
	struct ubifs_lpt_heap lpt_heap[LPROPS_HEAP_CNT];
	struct ubifs_lpt_heap dirty_idx;
	struct list_head uncat_list;
	struct list_head empty_list;
	struct list_head freeable_list;
	struct list_head frdi_idx_list;
	int freeable_cnt;
	int in_a_category_cnt;

	int ltab_lnum;
	int ltab_offs;
	struct ubifs_lprops *lpt;
	struct ubifs_lpt_lprops *ltab;
	struct ubifs_lpt_lprops *ltab_cmt;
	int lsave_cnt;
	int lsave_lnum;
	int lsave_offs;
	int *lsave;
	int lscan_lnum;

	long long rp_size;

	char *hash_algo_name;
	int hash_algo;
	char *auth_key_filename;
	char *auth_cert_filename;
	int hash_len;
	uint8_t root_idx_hash[UBIFS_MAX_HASH_LEN];
	uint8_t lpt_hash[UBIFS_MAX_HASH_LEN];
	uint8_t mst_hash[UBIFS_MAX_HASH_LEN];

	struct shash_desc *log_hash;

	unsigned int need_recovery:1;
	unsigned int replaying:1;
	unsigned int mounting:1;
	unsigned int remounting_rw:1;
	struct list_head replay_list;
	struct list_head replay_buds;
	unsigned long long cs_sqnum;
	struct list_head unclean_leb_list;
	struct ubifs_mst_node *rcvrd_mst_node;
	struct rb_root size_tree;

	int new_ihead_lnum;
	int new_ihead_offs;

	void *private;
	void (*assert_failed_cb)(const struct ubifs_info *c);
	void (*set_failure_reason_cb)(const struct ubifs_info *c,
				      unsigned int reason);
	unsigned int (*get_failure_reason_cb)(const struct ubifs_info *c);
	void (*clear_failure_reason_cb)(const struct ubifs_info *c);
	bool (*test_and_clear_failure_reason_cb)(const struct ubifs_info *c,
						 unsigned int reason);
	void (*set_lpt_invalid_cb)(const struct ubifs_info *c,
				   unsigned int reason);
	bool (*test_lpt_valid_cb)(const struct ubifs_info *c, int lnum,
				  int old_free, int old_dirty,
				  int free, int dirty);
	bool (*can_ignore_failure_cb)(const struct ubifs_info *c,
				      unsigned int reason);
	bool (*handle_failure_cb)(const struct ubifs_info *c,
				  unsigned int reason, void *priv);
};

extern atomic_long_t ubifs_clean_zn_cnt;

/* auth.c */
static inline int ubifs_authenticated(const struct ubifs_info *c)
{
	return c->authenticated;
}

/**
 * struct size_entry - inode size information for recovery.
 * @rb: link in the RB-tree of sizes
 * @inum: inode number
 * @i_size: size on inode
 * @d_size: maximum size based on data nodes
 * @exists: indicates whether the inode exists
 */
struct size_entry {
	struct rb_node rb;
	ino_t inum;
	loff_t i_size;
	loff_t d_size;
	int exists;
};

#ifdef WITH_CRYPTO
int ubifs_init_authentication(struct ubifs_info *c);
int ubifs_shash_init(const struct ubifs_info *c, struct shash_desc *desc);
int ubifs_shash_update(const struct ubifs_info *c, struct shash_desc *desc,
		       const void *buf, unsigned int len);
int ubifs_shash_final(const struct ubifs_info *c, struct shash_desc *desc,
		      u8 *out);
struct shash_desc *ubifs_hash_get_desc(const struct ubifs_info *c);
int __ubifs_node_calc_hash(const struct ubifs_info *c, const void *buf,
			   u8 *hash);
int ubifs_master_node_calc_hash(const struct ubifs_info *c, const void *node,
				uint8_t *hash);
int ubifs_sign_superblock_node(struct ubifs_info *c, void *node);
void ubifs_bad_hash(const struct ubifs_info *c, const void *node,
		    const u8 *hash, int lnum, int offs);
void __ubifs_exit_authentication(struct ubifs_info *c);
#else
static inline int ubifs_init_authentication(__unused struct ubifs_info *c)
{ return 0; }
static inline int ubifs_shash_init(__unused const struct ubifs_info *c,
				   __unused struct shash_desc *desc)
{ return 0; }
static inline int ubifs_shash_update(__unused const struct ubifs_info *c,
				     __unused struct shash_desc *desc,
				     __unused const void *buf,
				     __unused unsigned int len) { return 0; }
static inline int ubifs_shash_final(__unused const struct ubifs_info *c,
				    __unused struct shash_desc *desc,
				    __unused u8 *out) { return 0; }
static inline struct shash_desc *
ubifs_hash_get_desc(__unused const struct ubifs_info *c) { return NULL; }
static inline int __ubifs_node_calc_hash(__unused const struct ubifs_info *c,
					 __unused const void *buf,
					 __unused u8 *hash) { return 0; }
static inline int
ubifs_master_node_calc_hash(__unused const struct ubifs_info *c,
			    __unused const void *node, __unused uint8_t *hash)
{ return 0; }
static inline int ubifs_sign_superblock_node(__unused struct ubifs_info *c,
					     __unused void *node)
{ return 0; }
static inline void ubifs_bad_hash(__unused const struct ubifs_info *c,
				  __unused const void *node,
				  __unused const u8 *hash, __unused int lnum,
				  __unused int offs) {}
static inline void __ubifs_exit_authentication(__unused struct ubifs_info *c) {}
#endif

static inline int ubifs_prepare_auth_node(__unused struct ubifs_info *c,
					  __unused void *node,
					  __unused struct shash_desc *inhash)
{
	// To be implemented
	return 0;
}

static inline int
ubifs_node_calc_hash(const struct ubifs_info *c, const void *buf, u8 *hash)
{
	if (ubifs_authenticated(c))
		return __ubifs_node_calc_hash(c, buf, hash);
	else
		return 0;
}

static inline int
ubifs_node_check_hash(__unused const struct ubifs_info *c,
		      __unused const void *buf, __unused const u8 *expected)
{
	// To be implemented
	return 0;
}

/**
 * ubifs_check_hash - compare two hashes
 * @c: UBIFS file-system description object
 * @expected: first hash
 * @got: second hash
 *
 * Compare two hashes @expected and @got. Returns 0 when they are equal, a
 * negative error code otherwise.
 */
static inline int
ubifs_check_hash(__unused const struct ubifs_info *c,
		 __unused const u8 *expected, __unused const u8 *got)
{
	// To be implemented
	return 0;
}

/**
 * ubifs_check_hmac - compare two HMACs
 * @c: UBIFS file-system description object
 * @expected: first HMAC
 * @got: second HMAC
 *
 * Compare two hashes @expected and @got. Returns 0 when they are equal, a
 * negative error code otherwise.
 */
static inline int
ubifs_check_hmac(__unused const struct ubifs_info *c,
		 __unused const u8 *expected, __unused const u8 *got)
{
	// To be implemented
	return 0;
}

/**
 * ubifs_branch_hash - returns a pointer to the hash of a branch
 * @c: UBIFS file-system description object
 * @br: branch to get the hash from
 *
 * This returns a pointer to the hash of a branch. Since the key already is a
 * dynamically sized object we cannot use a struct member here.
 */
static inline u8 *
ubifs_branch_hash(struct ubifs_info *c, struct ubifs_branch *br)
{
	return (void *)br + sizeof(*br) + c->key_len;
}

/**
 * ubifs_copy_hash - copy a hash
 * @c: UBIFS file-system description object
 * @from: source hash
 * @to: destination hash
 *
 * With authentication this copies a hash, otherwise does nothing.
 */
static inline void
ubifs_copy_hash(const struct ubifs_info *c, const u8 *from, u8 *to)
{
	if (ubifs_authenticated(c))
		memcpy(to, from, c->hash_len);
}

static inline int
ubifs_node_insert_hmac(__unused const struct ubifs_info *c, __unused void *buf,
		       __unused int len, __unused int ofs_hmac)
{
	// To be implemented
	return 0;
}

static inline int
ubifs_node_verify_hmac(__unused const struct ubifs_info *c,
		       __unused const void *buf, __unused int len,
		       __unused int ofs_hmac)
{
	// To be implemented
	return 0;
}

/**
 * ubifs_auth_node_sz - returns the size of an authentication node
 * @c: UBIFS file-system description object
 *
 * This function returns the size of an authentication node which can
 * be 0 for unauthenticated filesystems or the real size of an auth node
 * authentication is enabled.
 */
static inline int
ubifs_auth_node_sz(__unused const struct ubifs_info *c)
{
	// To be implemented
	return 0;
}

static inline bool
ubifs_hmac_zero(__unused struct ubifs_info *c, __unused const u8 *hmac)
{
	// To be implemented
	return true;
}

static inline int
ubifs_shash_copy_state(__unused const struct ubifs_info *c,
		       __unused struct shash_desc *src,
		       __unused struct shash_desc *target)
{
	// To be implemented
	return 0;
}

static inline void ubifs_exit_authentication(struct ubifs_info *c)
{
	if (ubifs_authenticated(c))
		__ubifs_exit_authentication(c);
}

/* io.c */
void ubifs_ro_mode(struct ubifs_info *c, int err);
int ubifs_leb_read(const struct ubifs_info *c, int lnum, void *buf, int offs,
		   int len, int even_ebadmsg);
int ubifs_leb_write(struct ubifs_info *c, int lnum, const void *buf, int offs,
		    int len);
int ubifs_leb_change(struct ubifs_info *c, int lnum, const void *buf, int len);
int ubifs_leb_unmap(struct ubifs_info *c, int lnum);
int ubifs_leb_map(struct ubifs_info *c, int lnum);
int ubifs_is_mapped(const struct ubifs_info *c, int lnum);
int ubifs_wbuf_write_nolock(struct ubifs_wbuf *wbuf, void *buf, int len);
int ubifs_wbuf_seek_nolock(struct ubifs_wbuf *wbuf, int lnum, int offs);
int ubifs_wbuf_init(struct ubifs_info *c, struct ubifs_wbuf *wbuf);
int ubifs_read_node(const struct ubifs_info *c, void *buf, int type, int len,
		    int lnum, int offs);
int ubifs_read_node_wbuf(struct ubifs_wbuf *wbuf, void *buf, int type, int len,
			 int lnum, int offs);
int ubifs_write_node(struct ubifs_info *c, void *node, int len, int lnum,
		     int offs);
int ubifs_write_node_hmac(struct ubifs_info *c, void *buf, int len, int lnum,
			  int offs, int hmac_offs);
int ubifs_check_node(const struct ubifs_info *c, const void *buf, int len,
		     int lnum, int offs, int quiet, int must_chk_crc);
void ubifs_init_node(struct ubifs_info *c, void *buf, int len, int pad);
void ubifs_crc_node(struct ubifs_info *c, void *buf, int len);
void ubifs_prepare_node(struct ubifs_info *c, void *buf, int len, int pad);
int ubifs_prepare_node_hmac(struct ubifs_info *c, void *node, int len,
			    int hmac_offs, int pad);
void ubifs_prep_grp_node(struct ubifs_info *c, void *node, int len, int last);
int ubifs_io_init(struct ubifs_info *c);
void ubifs_pad(const struct ubifs_info *c, void *buf, int pad);
int ubifs_wbuf_sync_nolock(struct ubifs_wbuf *wbuf);

/* scan.c */
struct ubifs_scan_leb *ubifs_scan(const struct ubifs_info *c, int lnum,
				  int offs, void *sbuf, int quiet);
void ubifs_scan_destroy(struct ubifs_scan_leb *sleb);
int ubifs_scan_a_node(const struct ubifs_info *c, void *buf, int len, int lnum,
		      int offs, int quiet);
struct ubifs_scan_leb *ubifs_start_scan(const struct ubifs_info *c, int lnum,
					int offs, void *sbuf);
void ubifs_end_scan(const struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		    int lnum, int offs);
int ubifs_add_snod(const struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		   void *buf, int offs);
void ubifs_scanned_corruption(const struct ubifs_info *c, int lnum, int offs,
			      void *buf);

/* Failure reasons which are checked by fsck. */
enum {
	FR_DATA_CORRUPTED = 1,	/* Data is corrupted(master/log/orphan/main) */
	FR_TNC_CORRUPTED = 2,	/* TNC is corrupted */
	FR_LPT_CORRUPTED = 4,	/* LPT is corrupted */
	FR_LPT_INCORRECT = 8	/* Space statistics are wrong */
};
/* Partial failure reasons in common libs, which are handled by fsck. */
enum {
	FR_H_BUD_CORRUPTED = 0,		/* Bud LEB is corrupted */
	FR_H_TNC_DATA_CORRUPTED,	/* Data searched from TNC is corrupted */
	FR_H_ORPHAN_CORRUPTED,		/* Orphan LEB is corrupted */
	FR_H_LTAB_INCORRECT,		/* Lprops table is incorrect */
};
/* Callback functions for failure(which can be handled by fsck) happens. */
static inline void set_failure_reason_callback(const struct ubifs_info *c,
					       unsigned int reason)
{
	if (c->set_failure_reason_cb)
		c->set_failure_reason_cb(c, reason);
}
static inline unsigned int get_failure_reason_callback(
						const struct ubifs_info *c)
{
	if (c->get_failure_reason_cb)
		return c->get_failure_reason_cb(c);

	return 0;
}
static inline void clear_failure_reason_callback(const struct ubifs_info *c)
{
	if (c->clear_failure_reason_cb)
		c->clear_failure_reason_cb(c);
}
static inline bool test_and_clear_failure_reason_callback(
						const struct ubifs_info *c,
						unsigned int reason)
{
	if (c->test_and_clear_failure_reason_cb)
		return c->test_and_clear_failure_reason_cb(c, reason);

	return false;
}
static inline void set_lpt_invalid_callback(const struct ubifs_info *c,
					    unsigned int reason)
{
	if (c->set_lpt_invalid_cb)
		c->set_lpt_invalid_cb(c, reason);
}
static inline bool test_lpt_valid_callback(const struct ubifs_info *c, int lnum,
					   int old_free, int old_dirty,
					   int free, int dirty)
{
	if (c->test_lpt_valid_cb)
		return c->test_lpt_valid_cb(c, lnum,
					    old_free, old_dirty, free, dirty);

	return false;
}
static inline bool can_ignore_failure_callback(const struct ubifs_info *c,
					       unsigned int reason)
{
	if (c->can_ignore_failure_cb)
		return c->can_ignore_failure_cb(c, reason);

	return false;
}
static inline bool handle_failure_callback(const struct ubifs_info *c,
					   unsigned int reason, void *priv)
{
	if (c->handle_failure_cb)
		return c->handle_failure_cb(c, reason, priv);

	return false;
}

/* log.c */
void ubifs_add_bud(struct ubifs_info *c, struct ubifs_bud *bud);
void ubifs_create_buds_lists(struct ubifs_info *c);
int ubifs_add_bud_to_log(struct ubifs_info *c, int jhead, int lnum, int offs);
struct ubifs_bud *ubifs_search_bud(struct ubifs_info *c, int lnum);
struct ubifs_wbuf *ubifs_get_wbuf(struct ubifs_info *c, int lnum);
int ubifs_log_start_commit(struct ubifs_info *c, int *ltail_lnum);
int ubifs_log_end_commit(struct ubifs_info *c, int new_ltail_lnum);
int ubifs_log_post_commit(struct ubifs_info *c, int old_ltail_lnum);
int ubifs_consolidate_log(struct ubifs_info *c);

/* journal.c */
int ubifs_get_dent_type(int mode);
int ubifs_jnl_update_file(struct ubifs_info *c,
			  const struct ubifs_inode *dir_ui,
			  const struct fscrypt_name *nm,
			  const struct ubifs_inode *ui);

/* budget.c */
int ubifs_budget_space(struct ubifs_info *c, struct ubifs_budget_req *req);
void ubifs_release_budget(struct ubifs_info *c, struct ubifs_budget_req *req);
long long ubifs_get_free_space_nolock(struct ubifs_info *c);
int ubifs_calc_min_idx_lebs(struct ubifs_info *c);
long long ubifs_reported_space(const struct ubifs_info *c, long long free);
long long ubifs_calc_available(const struct ubifs_info *c, int min_idx_lebs);

/* find.c */
int ubifs_find_free_space(struct ubifs_info *c, int min_space, int *offs,
			  int squeeze);
int ubifs_find_free_leb_for_idx(struct ubifs_info *c);
int ubifs_find_dirty_leb(struct ubifs_info *c, struct ubifs_lprops *ret_lp,
			 int min_space, int pick_free);
int ubifs_find_dirty_idx_leb(struct ubifs_info *c);
int ubifs_save_dirty_idx_lnums(struct ubifs_info *c);

/* tnc.c */
int ubifs_lookup_level0(struct ubifs_info *c, const union ubifs_key *key,
			struct ubifs_znode **zn, int *n);
int ubifs_tnc_lookup_nm(struct ubifs_info *c, const union ubifs_key *key,
			void *node, const struct fscrypt_name *nm);
int ubifs_tnc_locate(struct ubifs_info *c, const union ubifs_key *key,
		     void *node, int *lnum, int *offs);
int ubifs_tnc_add(struct ubifs_info *c, const union ubifs_key *key, int lnum,
		  int offs, int len, const u8 *hash);
int ubifs_tnc_replace(struct ubifs_info *c, const union ubifs_key *key,
		      int old_lnum, int old_offs, int lnum, int offs, int len);
int ubifs_tnc_add_nm(struct ubifs_info *c, const union ubifs_key *key,
		     int lnum, int offs, int len, const u8 *hash,
		     const struct fscrypt_name *nm);
int ubifs_tnc_remove(struct ubifs_info *c, const union ubifs_key *key);
int ubifs_tnc_remove_nm(struct ubifs_info *c, const union ubifs_key *key,
			const struct fscrypt_name *nm);
int ubifs_tnc_remove_range(struct ubifs_info *c, union ubifs_key *from_key,
			   union ubifs_key *to_key);
int ubifs_tnc_remove_ino(struct ubifs_info *c, ino_t inum);
int ubifs_tnc_remove_node(struct ubifs_info *c, const union ubifs_key *key,
			  int lnum, int offs);
struct ubifs_dent_node *ubifs_tnc_next_ent(struct ubifs_info *c,
					   union ubifs_key *key,
					   const struct fscrypt_name *nm);
void ubifs_tnc_close(struct ubifs_info *c);
int ubifs_tnc_has_node(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs, int is_idx);
int ubifs_dirty_idx_node(struct ubifs_info *c, union ubifs_key *key, int level,
			 int lnum, int offs);
/* Shared by tnc.c for tnc_commit.c */
void destroy_old_idx(struct ubifs_info *c);
int is_idx_node_in_tnc(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs);
int insert_old_idx_znode(struct ubifs_info *c, struct ubifs_znode *znode);

/* tnc_misc.c */
int ubifs_search_zbranch(const struct ubifs_info *c,
			 const struct ubifs_znode *znode,
			 const union ubifs_key *key, int *n);
struct ubifs_znode *ubifs_tnc_postorder_first(struct ubifs_znode *znode);
struct ubifs_znode *ubifs_tnc_postorder_next(const struct ubifs_info *c,
					     struct ubifs_znode *znode);
long ubifs_destroy_tnc_subtree(const struct ubifs_info *c,
			       struct ubifs_znode *zr);
void ubifs_destroy_tnc_tree(struct ubifs_info *c);
struct ubifs_znode *ubifs_load_znode(struct ubifs_info *c,
				     struct ubifs_zbranch *zbr,
				     struct ubifs_znode *parent, int iip);
int ubifs_tnc_read_node(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			void *node);

/* tnc_commit.c */
int ubifs_tnc_start_commit(struct ubifs_info *c, struct ubifs_zbranch *zroot);
int ubifs_tnc_end_commit(struct ubifs_info *c);

/* commit.c */
void ubifs_commit_required(struct ubifs_info *c);
void ubifs_request_bg_commit(struct ubifs_info *c);
int ubifs_run_commit(struct ubifs_info *c);
void ubifs_recovery_commit(struct ubifs_info *c);
int ubifs_gc_should_commit(struct ubifs_info *c);
void ubifs_wait_for_commit(struct ubifs_info *c);

/* master.c */
int ubifs_compare_master_node(struct ubifs_info *c, void *m1, void *m2);
int ubifs_read_master(struct ubifs_info *c);
int ubifs_write_master(struct ubifs_info *c);

/* sb.c */
int ubifs_read_superblock(struct ubifs_info *c);
int ubifs_write_sb_node(struct ubifs_info *c, struct ubifs_sb_node *sup);
int ubifs_fixup_free_space(struct ubifs_info *c);

/* replay.c */
int ubifs_validate_entry(struct ubifs_info *c,
			 const struct ubifs_dent_node *dent);
int take_ihead(struct ubifs_info *c);
int ubifs_replay_journal(struct ubifs_info *c);

/* gc.c */
int ubifs_garbage_collect(struct ubifs_info *c, int anyway);
int ubifs_gc_start_commit(struct ubifs_info *c);
int ubifs_gc_end_commit(struct ubifs_info *c);
void ubifs_destroy_idx_gc(struct ubifs_info *c);
int ubifs_get_idx_gc_leb(struct ubifs_info *c);
int ubifs_garbage_collect_leb(struct ubifs_info *c, struct ubifs_lprops *lp);

/* orphan.c */
int ubifs_orphan_start_commit(struct ubifs_info *c);
int ubifs_orphan_end_commit(struct ubifs_info *c);
int ubifs_mount_orphans(struct ubifs_info *c, int unclean, int read_only);
int ubifs_clear_orphans(struct ubifs_info *c);

/* lpt.c */
int ubifs_calc_dflt_lpt_geom(struct ubifs_info *c, int *main_lebs, int *big_lpt);
int ubifs_calc_lpt_geom(struct ubifs_info *c);
int ubifs_create_lpt(struct ubifs_info *c, struct ubifs_lprops *lps, int lp_cnt,
		     u8 *hash, bool free_ltab);
int ubifs_lpt_init(struct ubifs_info *c, int rd, int wr);
struct ubifs_lprops *ubifs_lpt_lookup(struct ubifs_info *c, int lnum);
struct ubifs_lprops *ubifs_lpt_lookup_dirty(struct ubifs_info *c, int lnum);
int ubifs_lpt_scan_nolock(struct ubifs_info *c, int start_lnum, int end_lnum,
			  ubifs_lpt_scan_callback scan_cb, void *data);

/* Shared by lpt.c for lpt_commit.c */
void ubifs_pack_lsave(struct ubifs_info *c, void *buf, int *lsave);
void ubifs_pack_ltab(struct ubifs_info *c, void *buf,
		     struct ubifs_lpt_lprops *ltab);
void ubifs_pack_pnode(struct ubifs_info *c, void *buf,
		      struct ubifs_pnode *pnode);
void ubifs_pack_nnode(struct ubifs_info *c, void *buf,
		      struct ubifs_nnode *nnode);
struct ubifs_pnode *ubifs_get_pnode(struct ubifs_info *c,
				    struct ubifs_nnode *parent, int iip);
struct ubifs_nnode *ubifs_get_nnode(struct ubifs_info *c,
				    struct ubifs_nnode *parent, int iip);
struct ubifs_pnode *ubifs_pnode_lookup(struct ubifs_info *c, int i);
int ubifs_read_nnode(struct ubifs_info *c, struct ubifs_nnode *parent, int iip);
void ubifs_add_lpt_dirt(struct ubifs_info *c, int lnum, int dirty);
void ubifs_add_nnode_dirt(struct ubifs_info *c, struct ubifs_nnode *nnode);
uint32_t ubifs_unpack_bits(const struct ubifs_info *c, uint8_t **addr, int *pos, int nrbits);
int ubifs_calc_nnode_num(int row, int col);
struct ubifs_nnode *ubifs_first_nnode(struct ubifs_info *c, int *hght);
/* Needed only in debugging code in lpt_commit.c */
int ubifs_unpack_nnode(const struct ubifs_info *c, void *buf,
		       struct ubifs_nnode *nnode);
int ubifs_lpt_calc_hash(struct ubifs_info *c, u8 *hash);

/* lpt_commit.c */
struct ubifs_pnode *ubifs_find_next_pnode(struct ubifs_info *c,
					  struct ubifs_pnode *pnode);
void ubifs_make_nnode_dirty(struct ubifs_info *c, struct ubifs_nnode *nnode);
void ubifs_make_pnode_dirty(struct ubifs_info *c, struct ubifs_pnode *pnode);
int ubifs_lpt_start_commit(struct ubifs_info *c);
int ubifs_lpt_end_commit(struct ubifs_info *c);
int ubifs_lpt_post_commit(struct ubifs_info *c);
void ubifs_free_lpt_nodes(struct ubifs_info *c);
void ubifs_lpt_free(struct ubifs_info *c, int wr_only);
int dbg_check_ltab_lnum(struct ubifs_info *c, int lnum);

/* lprops.c */
const struct ubifs_lprops *ubifs_change_lp(struct ubifs_info *c,
					   const struct ubifs_lprops *lp,
					   int free, int dirty, int flags,
					   int idx_gc_cnt);
void ubifs_get_lp_stats(struct ubifs_info *c, struct ubifs_lp_stats *lst);
void ubifs_add_to_cat(struct ubifs_info *c, struct ubifs_lprops *lprops,
		      int cat);
void ubifs_replace_cat(struct ubifs_info *c, struct ubifs_lprops *old_lprops,
		       struct ubifs_lprops *new_lprops);
void ubifs_ensure_cat(struct ubifs_info *c, struct ubifs_lprops *lprops);
int ubifs_categorize_lprops(const struct ubifs_info *c,
			    const struct ubifs_lprops *lprops);
int ubifs_change_one_lp(struct ubifs_info *c, int lnum, int free, int dirty,
			int flags_set, int flags_clean, int idx_gc_cnt);
int ubifs_update_one_lp(struct ubifs_info *c, int lnum, int free, int dirty,
			int flags_set, int flags_clean);
int ubifs_read_one_lp(struct ubifs_info *c, int lnum, struct ubifs_lprops *lp);
const struct ubifs_lprops *ubifs_fast_find_free(struct ubifs_info *c);
const struct ubifs_lprops *ubifs_fast_find_empty(struct ubifs_info *c);
const struct ubifs_lprops *ubifs_fast_find_freeable(struct ubifs_info *c);
const struct ubifs_lprops *ubifs_fast_find_frdi_idx(struct ubifs_info *c);
int ubifs_calc_dark(const struct ubifs_info *c, int spc);

/* dir.c */
struct ubifs_inode *ubifs_lookup_by_inum(struct ubifs_info *c, ino_t inum);
struct ubifs_inode *ubifs_lookup(struct ubifs_info *c,
				 struct ubifs_inode *dir_ui,
				 const struct fscrypt_name *nm);
int ubifs_mkdir(struct ubifs_info *c, struct ubifs_inode *dir_ui,
		const struct fscrypt_name *nm, unsigned int mode);
int ubifs_link_recovery(struct ubifs_info *c, struct ubifs_inode *dir_ui,
			struct ubifs_inode *ui, const struct fscrypt_name *nm);
int ubifs_create_root(struct ubifs_info *c);

/* super.c */
int open_ubi(struct ubifs_info *c, const char *node);
void close_ubi(struct ubifs_info *c);
int open_target(struct ubifs_info *c);
int close_target(struct ubifs_info *c);
int ubifs_open_volume(struct ubifs_info *c, const char *volume_name);
int ubifs_close_volume(struct ubifs_info *c);
int check_volume_empty(struct ubifs_info *c);
void init_ubifs_info(struct ubifs_info *c, int program_type);
int init_constants_early(struct ubifs_info *c);
int init_constants_sb(struct ubifs_info *c);
void init_constants_master(struct ubifs_info *c);
int take_gc_lnum(struct ubifs_info *c);
int alloc_wbufs(struct ubifs_info *c);
void free_wbufs(struct ubifs_info *c);
void free_orphans(struct ubifs_info *c);
void free_buds(struct ubifs_info *c, bool delete_from_list);
void destroy_journal(struct ubifs_info *c);

/* recovery.c */
int ubifs_recover_master_node(struct ubifs_info *c);
struct ubifs_scan_leb *ubifs_recover_leb(struct ubifs_info *c, int lnum,
					 int offs, void *sbuf, int jhead);
struct ubifs_scan_leb *ubifs_recover_log_leb(struct ubifs_info *c, int lnum,
					     int offs, void *sbuf);
int ubifs_recover_inl_heads(struct ubifs_info *c, void *sbuf);
int ubifs_rcvry_gc_commit(struct ubifs_info *c);
int ubifs_recover_size_accum(struct ubifs_info *c, union ubifs_key *key,
			     int deletion, loff_t new_size);
int ubifs_recover_size(struct ubifs_info *c, bool in_place);
void ubifs_destroy_size_tree(struct ubifs_info *c);

/* Normal UBIFS messages */
enum { ERR_LEVEL = 1, WARN_LEVEL, INFO_LEVEL, DEBUG_LEVEL };
#define ubifs_msg(c, fmt, ...) do {					\
	if (c->debug_level >= INFO_LEVEL)				\
		printf("<INFO> %s[%d] (%s): %s: " fmt "\n",		\
		       c->program_name, getpid(),			\
		       c->dev_name, __FUNCTION__, ##__VA_ARGS__);	\
} while (0)
#define ubifs_warn(c, fmt, ...) do {					\
	if (c->debug_level >= WARN_LEVEL)				\
		printf("<WARN> %s[%d] (%s): %s: " fmt "\n",		\
		       c->program_name, getpid(),			\
		       c->dev_name, __FUNCTION__, ##__VA_ARGS__);	\
} while (0)
#define ubifs_err(c, fmt, ...) do {					\
	if (c->debug_level >= ERR_LEVEL)				\
		printf("<ERROR> %s[%d] (%s): %s: " fmt "\n",		\
		       c->program_name, getpid(),			\
		       c->dev_name, __FUNCTION__, ##__VA_ARGS__);	\
} while (0)

#endif /* !__UBIFS_H__ */
