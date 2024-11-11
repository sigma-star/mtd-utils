/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

#ifndef __UBIFS_DEBUG_H__
#define __UBIFS_DEBUG_H__

/* Checking helper functions */
typedef int (*dbg_leaf_callback)(struct ubifs_info *c,
				 struct ubifs_zbranch *zbr, void *priv);
typedef int (*dbg_znode_callback)(struct ubifs_info *c,
				  struct ubifs_znode *znode, void *priv);

void ubifs_assert_failed(struct ubifs_info *c, const char *expr,
	const char *file, int line);

#define ubifs_assert(c, expr) do {                                             \
	if (unlikely(!(expr))) {                                               \
		ubifs_assert_failed((struct ubifs_info *)c, #expr, __FILE__,   \
		 __LINE__);                                                    \
	}                                                                      \
} while (0)

#define ubifs_assert_cmt_locked(c) do {                                        \
	if (unlikely(down_write_trylock(&(c)->commit_sem))) {                  \
		up_write(&(c)->commit_sem);                                    \
		ubifs_err(c, "commit lock is not locked!\n");                  \
		ubifs_assert(c, 0);                                            \
	}                                                                      \
} while (0)

#define ubifs_dbg_msg(type, fmt, ...)					    \
	pr_debug("UBIFS DBG " type " " fmt "\n", ##__VA_ARGS__)

#define DBG_KEY_BUF_LEN 48
#define ubifs_dbg_msg_key(type, key, fmt, ...) do {			    \
	char __tmp_key_buf[DBG_KEY_BUF_LEN];				    \
	pr_debug("UBIFS DBG " type " " fmt "%s\n", ##__VA_ARGS__,	    \
		 dbg_snprintf_key(c, key, __tmp_key_buf, DBG_KEY_BUF_LEN)); \
} while (0)

/* General messages */
#define dbg_gen(fmt, ...)   ubifs_dbg_msg("gen", fmt, ##__VA_ARGS__)
/* Additional journal messages */
#define dbg_jnl(fmt, ...)   ubifs_dbg_msg("jnl", fmt, ##__VA_ARGS__)
#define dbg_jnlk(key, fmt, ...) \
	ubifs_dbg_msg_key("jnl", key, fmt, ##__VA_ARGS__)
/* Additional TNC messages */
#define dbg_tnc(fmt, ...)   ubifs_dbg_msg("tnc", fmt, ##__VA_ARGS__)
#define dbg_tnck(key, fmt, ...) \
	ubifs_dbg_msg_key("tnc", key, fmt, ##__VA_ARGS__)
/* Additional lprops messages */
#define dbg_lp(fmt, ...)    ubifs_dbg_msg("lp", fmt, ##__VA_ARGS__)
/* Additional LEB find messages */
#define dbg_find(fmt, ...)  ubifs_dbg_msg("find", fmt, ##__VA_ARGS__)
/* Additional mount messages */
#define dbg_mnt(fmt, ...)   ubifs_dbg_msg("mnt", fmt, ##__VA_ARGS__)
#define dbg_mntk(key, fmt, ...) \
	ubifs_dbg_msg_key("mnt", key, fmt, ##__VA_ARGS__)
/* Additional I/O messages */
#define dbg_io(fmt, ...)    ubifs_dbg_msg("io", fmt, ##__VA_ARGS__)
/* Additional commit messages */
#define dbg_cmt(fmt, ...)   ubifs_dbg_msg("cmt", fmt, ##__VA_ARGS__)
/* Additional budgeting messages */
#define dbg_budg(fmt, ...)  ubifs_dbg_msg("budg", fmt, ##__VA_ARGS__)
/* Additional log messages */
#define dbg_log(fmt, ...)   ubifs_dbg_msg("log", fmt, ##__VA_ARGS__)
/* Additional gc messages */
#define dbg_gc(fmt, ...)    ubifs_dbg_msg("gc", fmt, ##__VA_ARGS__)
/* Additional scan messages */
#define dbg_scan(fmt, ...)  ubifs_dbg_msg("scan", fmt, ##__VA_ARGS__)
/* Additional recovery messages */
#define dbg_rcvry(fmt, ...) ubifs_dbg_msg("rcvry", fmt, ##__VA_ARGS__)
/* Additional fsck messages */
#define dbg_fsck(fmt, ...) ubifs_dbg_msg("fsck", fmt, ##__VA_ARGS__)

static inline int dbg_is_chk_index(__unused const struct ubifs_info *c)
{ return 0; }

/* Dump functions */
const char *ubifs_get_key_name(int type);
const char *ubifs_get_type_name(int type);
const char *dbg_ntype(int type);
const char *dbg_cstate(int cmt_state);
const char *dbg_jhead(int jhead);
const char *dbg_get_key_dump(const struct ubifs_info *c,
			     const union ubifs_key *key);
const char *dbg_snprintf_key(const struct ubifs_info *c,
			     const union ubifs_key *key, char *buffer, int len);
void ubifs_dump_node(const struct ubifs_info *c, const void *node,
		     int node_len);
void ubifs_dump_lstats(const struct ubifs_lp_stats *lst);
void ubifs_dump_budg(struct ubifs_info *c, const struct ubifs_budg_info *bi);
void ubifs_dump_lprop(const struct ubifs_info *c,
		      const struct ubifs_lprops *lp);
void ubifs_dump_lprops(struct ubifs_info *c);
void ubifs_dump_lpt_info(struct ubifs_info *c);
void ubifs_dump_leb(const struct ubifs_info *c, int lnum);
void ubifs_dump_znode(const struct ubifs_info *c,
		      const struct ubifs_znode *znode);
void ubifs_dump_heap(struct ubifs_info *c, struct ubifs_lpt_heap *heap,
		     int cat);
void ubifs_dump_pnode(struct ubifs_info *c, struct ubifs_pnode *pnode,
		      struct ubifs_nnode *parent, int iip);
void ubifs_dump_index(struct ubifs_info *c);
void ubifs_dump_lpt_lebs(const struct ubifs_info *c);

int dbg_walk_index(struct ubifs_info *c, dbg_leaf_callback leaf_cb,
		   dbg_znode_callback znode_cb, void *priv);
int add_size(struct ubifs_info *c, struct ubifs_znode *znode, void *priv);

/* Checking functions */
static inline void dbg_save_space_info(__unused struct ubifs_info *c) {}
static inline int dbg_check_space_info(__unused struct ubifs_info *c)
{ return 0; }
static inline int dbg_check_lprops(__unused struct ubifs_info *c) { return 0; }
static inline int dbg_old_index_check_init(__unused struct ubifs_info *c,
					   __unused struct ubifs_zbranch *zroot)
{ return 0; }
static inline int dbg_check_old_index(__unused struct ubifs_info *c,
				      __unused struct ubifs_zbranch *zroot)
{ return 0; }
static inline int dbg_check_cats(__unused struct ubifs_info *c) { return 0; }
static inline int dbg_check_ltab(__unused struct ubifs_info *c) { return 0; }
static inline int dbg_chk_lpt_free_spc(__unused struct ubifs_info *c)
{ return 0; }
static inline int dbg_chk_lpt_sz(__unused struct ubifs_info *c,
				 __unused int action, __unused int len)
{ return 0; }
static inline int dbg_check_tnc(__unused struct ubifs_info *c,
				__unused int extra) { return 0; }
static inline int dbg_check_idx_size(__unused struct ubifs_info *c,
				     __unused long long idx_size) { return 0; }
static inline int dbg_check_filesystem(__unused struct ubifs_info *c)
{ return 0; }
static inline void dbg_check_heap(__unused struct ubifs_info *c,
				  __unused struct ubifs_lpt_heap *heap,
				  __unused int cat,
				  __unused int add_pos) {}
static inline int dbg_check_lpt_nodes(__unused struct ubifs_info *c,
				      __unused struct ubifs_cnode *cnode,
				      __unused int row,
				      __unused int col) { return 0; }
static inline int dbg_check_data_nodes_order(__unused struct ubifs_info *c,
					     __unused struct list_head *head)
{ return 0; }
static inline int dbg_check_nondata_nodes_order(__unused struct ubifs_info *c,
						__unused struct list_head *head)
{ return 0; }
static inline int dbg_leb_write(__unused struct ubifs_info *c,
				__unused int lnum, __unused const void *buf,
				__unused int offs, __unused int len)
{ return 0; }
static inline int dbg_leb_change(__unused struct ubifs_info *c,
				 __unused int lnum, __unused const void *buf,
				 __unused int len) { return 0; }
static inline int dbg_leb_unmap(__unused struct ubifs_info *c,
				__unused int lnum) { return 0; }
static inline int dbg_leb_map(__unused struct ubifs_info *c, __unused int lnum)
{ return 0; }

extern void print_hex_dump(const char *prefix_str,
			   int prefix_type, int rowsize, int groupsize,
			   const void *buf, size_t len, bool ascii);

#endif /* !__UBIFS_DEBUG_H__ */
