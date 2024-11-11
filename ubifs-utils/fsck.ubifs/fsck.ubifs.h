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

/**
 * struct ubifs_fsck_info - UBIFS fsck information.
 * @mode: working mode
 */
struct ubifs_fsck_info {
	int mode;
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

#endif
