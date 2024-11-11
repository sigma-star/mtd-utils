// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Huawei Technologies Co, Ltd.
 *
 * Authors: Zhihao Cheng <chengzhihao1@huawei.com>
 */

#ifndef __FSCK_UBIFS_H__
#define __FSCK_UBIFS_H__

/* Exit codes used by fsck-type programs */
#define FSCK_OK			0	/* No errors */
#define FSCK_NONDESTRUCT	1	/* File system errors corrected */
#define FSCK_REBOOT		2	/* System should be rebooted */
#define FSCK_UNCORRECTED	4	/* File system errors left uncorrected */
#define FSCK_ERROR		8	/* Operational error */
#define FSCK_USAGE		16	/* Usage or syntax error */
#define FSCK_CANCELED		32	/* Aborted with a signal or ^C */
#define FSCK_LIBRARY		128	/* Shared library error */

/*
 * There are 6 working modes for fsck:
 * NORMAL_MODE:	Check the filesystem, ask user whether or not to fix the
 *		problem as long as inconsistent data is found during checking.
 * SAFE_MODE:	Check and safely repair the filesystem, if there are any
 *		data dropping operations needed by fixing, fsck will fail.
 * DANGER_MODE0:Check and repair the filesystem according to TNC, data dropping
 *              will be reported. If TNC/master/log is corrupted, fsck will fail.
 * DANGER_MODE1:Check and forcedly repair the filesystem according to TNC,
 *		turns to @REBUILD_MODE mode automatically if TNC/master/log is
 *		corrupted.
 * REBUILD_MODE:Scan entire UBI volume to find all nodes, and rebuild the
 *		filesystem, always make fsck success.
 * CHECK_MODE:	Make no changes to the filesystem, only check the filesystem.
 */
enum { NORMAL_MODE = 0, SAFE_MODE, DANGER_MODE0,
       DANGER_MODE1, REBUILD_MODE, CHECK_MODE };

/* Types of inconsistent problems */
enum { SB_CORRUPTED = 0 };

struct scanned_file;

/**
 * scanned_node - common header node.
 * @exist: whether the node is found by scanning
 * @lnum: LEB number of the scanned node
 * @offs: scanned node's offset within @lnum
 * @len: length of scanned node
 * @sqnum: sequence number
 */
struct scanned_node {
	bool exist;
	int lnum;
	int offs;
	int len;
	unsigned long long sqnum;
};

/**
 * scanned_ino_node - scanned inode node.
 * @header: common header of scanned node
 * @key: the key of inode node
 * @is_xattr: %1 for xattr inode, otherwise %0
 * @is_encrypted: %1 for encrypted inode, otherwise %0
 * @mode: file mode
 * @nlink: number of hard links
 * @xcnt: count of extended attributes this inode has
 * @xsz: summarized size of all extended attributes in bytes
 * @xnms: sum of lengths of all extended attribute names
 * @size: inode size in bytes
 * @rb: link in the tree of valid inode nodes or deleted inode nodes
 */
struct scanned_ino_node {
	struct scanned_node header;
	union ubifs_key key;
	unsigned int is_xattr:1;
	unsigned int is_encrypted:1;
	unsigned int mode;
	unsigned int nlink;
	unsigned int xcnt;
	unsigned int xsz;
	unsigned int xnms;
	unsigned long long size;
	struct rb_node rb;
};

/**
 * scanned_dent_node - scanned dentry node.
 * @header: common header of scanned node
 * @key: the key of dentry node
 * @can_be_found: whether this dentry can be found from '/'
 * @type: file type, reg/dir/symlink/block/char/fifo/sock
 * @nlen: name length
 * @name: dentry name
 * @inum: target inode number
 * @file: corresponding file
 * @rb: link in the trees of:
 *  1) valid dentry nodes or deleted dentry node
 *  2) all scanned dentry nodes from same file
 * @list: link in the list dentries for looking up/deleting
 */
struct scanned_dent_node {
	struct scanned_node header;
	union ubifs_key key;
	bool can_be_found;
	unsigned int type;
	unsigned int nlen;
	char name[UBIFS_MAX_NLEN + 1];
	ino_t inum;
	struct scanned_file *file;
	struct rb_node rb;
	struct list_head list;
};

/**
 * scanned_data_node - scanned data node.
 * @header: common header of scanned node
 * @key: the key of data node
 * @size: uncompressed data size in bytes
 * @rb: link in the tree of all scanned data nodes from same file
 * @list: link in the list for deleting
 */
struct scanned_data_node {
	struct scanned_node header;
	union ubifs_key key;
	unsigned int size;
	struct rb_node rb;
	struct list_head list;
};

/**
 * scanned_trun_node - scanned truncation node.
 * @header: common header of scanned node
 * @new_size: size after truncation
 */
struct scanned_trun_node {
	struct scanned_node header;
	unsigned long long new_size;
};

/**
 * scanned_file - file info scanned from UBIFS volume.
 *
 * @calc_nlink: calculated count of directory entries refer this inode
 * @calc_xcnt: calculated count of extended attributes
 * @calc_xsz: calculated summary size of all extended attributes
 * @calc_xnms: calculated sum of lengths of all extended attribute names
 * @calc_size: calculated file size
 * @has_encrypted_info: whether the file has encryption related xattrs
 *
 * @inum: inode number
 * @ino: inode node
 * @trun: truncation node
 *
 * @rb: link in the tree of all scanned files
 * @list: link in the list files for kinds of processing
 * @dent_nodes: tree of all scanned dentry nodes
 * @data_nodes: tree of all scanned data nodes
 * @xattr_files: tree of all scanned xattr files
 */
struct scanned_file {
	unsigned int calc_nlink;
	unsigned int calc_xcnt;
	unsigned int calc_xsz;
	unsigned int calc_xnms;
	unsigned long long calc_size;
	bool has_encrypted_info;

	ino_t inum;
	struct scanned_ino_node ino;
	struct scanned_trun_node trun;

	struct rb_node rb;
	struct list_head list;
	struct rb_root dent_nodes;
	struct rb_root data_nodes;
	struct rb_root xattr_files;
};

/**
 * ubifs_rebuild_info - UBIFS rebuilding information.
 * @used_lebs: a bitmap used for recording used lebs
 * @lpts: lprops table
 * @scanned_files: tree of all scanned files
 * @write_buf: write buffer for LEB @head_lnum
 * @head_lnum: current writing LEB number
 * @head_offs: current writing position in LEB @head_lnum
 */
struct ubifs_rebuild_info {
	unsigned long *used_lebs;
	struct ubifs_lprops *lpts;
	struct rb_root scanned_files;
	void *write_buf;
	int head_lnum;
	int head_offs;
};

/**
 * struct ubifs_fsck_info - UBIFS fsck information.
 * @mode: working mode
 * @failure_reason: reasons for failed operations
 * @lpt_status: the status of lpt, could be: %0(OK), %FR_LPT_CORRUPTED or
 *		%FR_LPT_INCORRECT
 * @try_rebuild: %true means that try to rebuild fs when fsck failed
 * @rebuild: rebuilding-related information
 */
struct ubifs_fsck_info {
	int mode;
	unsigned int failure_reason;
	unsigned int lpt_status;
	bool try_rebuild;
	struct ubifs_rebuild_info *rebuild;
};

#define FSCK(c) ((struct ubifs_fsck_info*)c->private)

static inline const char *mode_name(const struct ubifs_info *c)
{
	if (!c->private)
		return "";

	switch (FSCK(c)->mode) {
	case NORMAL_MODE:
		return ",normal mode";
	case SAFE_MODE:
		return ",safe mode";
	case DANGER_MODE0:
		return ",danger mode";
	case DANGER_MODE1:
		return ",danger + rebuild mode";
	case REBUILD_MODE:
		return ",rebuild mode";
	case CHECK_MODE:
		return ",check mode";
	default:
		return "";
	}
}

#define log_out(c, fmt, ...)						\
	printf("%s[%d] (%s%s): " fmt "\n", c->program_name ? : "noprog",\
	       getpid(), c->dev_name ? : "-", mode_name(c),		\
	       ##__VA_ARGS__)

#define log_err(c, err, fmt, ...) do {					\
	printf("%s[%d][ERROR] (%s%s): %s: " fmt,			\
	       c->program_name ? : "noprog", getpid(),			\
	       c->dev_name ? : "-", mode_name(c),			\
	       __FUNCTION__, ##__VA_ARGS__);				\
	if (err)							\
		printf(" - %s", strerror(err));				\
	printf("\n");							\
} while (0)

/* Exit code for fsck program. */
extern int exit_code;

/* problem.c */
bool fix_problem(const struct ubifs_info *c, int problem_type);

/* load_fs.c */
int ubifs_load_filesystem(struct ubifs_info *c);
void ubifs_destroy_filesystem(struct ubifs_info *c);

/* extract_files.c */
bool parse_ino_node(struct ubifs_info *c, int lnum, int offs, void *node,
		    union ubifs_key *key, struct scanned_ino_node *ino_node);
bool parse_dent_node(struct ubifs_info *c, int lnum, int offs, void *node,
		     union ubifs_key *key, struct scanned_dent_node *dent_node);
bool parse_data_node(struct ubifs_info *c, int lnum, int offs, void *node,
		     union ubifs_key *key, struct scanned_data_node *data_node);
bool parse_trun_node(struct ubifs_info *c, int lnum, int offs, void *node,
		     union ubifs_key *key, struct scanned_trun_node *trun_node);
int insert_or_update_file(struct ubifs_info *c, struct rb_root *file_tree,
			  struct scanned_node *sn, int key_type, ino_t inum);
void destroy_file_content(struct ubifs_info *c, struct scanned_file *file);
void destroy_file_tree(struct ubifs_info *c, struct rb_root *file_tree);
struct scanned_file *lookup_file(struct rb_root *file_tree, ino_t inum);
bool file_is_valid(struct ubifs_info *c, struct scanned_file *file,
		   struct rb_root *file_tree);
bool file_is_reachable(struct ubifs_info *c, struct scanned_file *file,
		       struct rb_root *file_tree);
int check_and_correct_files(struct ubifs_info *c);

/* rebuild_fs.c */
int ubifs_rebuild_filesystem(struct ubifs_info *c);

#endif
