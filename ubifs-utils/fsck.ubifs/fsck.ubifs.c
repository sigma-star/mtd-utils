// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Huawei Technologies Co, Ltd.
 *
 * Authors: Zhihao Cheng <chengzhihao1@huawei.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>

#include "bitops.h"
#include "kmem.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "misc.h"
#include "fsck.ubifs.h"

/*
 * Because we copy functions from the kernel, we use a subset of the UBIFS
 * file-system description object struct ubifs_info.
 */
struct ubifs_info info_;
static struct ubifs_info *c = &info_;

int exit_code = FSCK_OK;

static const char *optstring = "Vrg:abyn";

static const struct option longopts[] = {
	{"version",            0, NULL, 'V'},
	{"reserve",            1, NULL, 'r'},
	{"debug",              1, NULL, 'g'},
	{"auto",               1, NULL, 'a'},
	{"rebuild",            1, NULL, 'b'},
	{"yes",                1, NULL, 'y'},
	{"nochange",           1, NULL, 'n'},
	{NULL, 0, NULL, 0}
};

static const char *helptext =
"Usage: fsck.ubifs [OPTIONS] ubi_volume\n"
"Check & repair UBIFS filesystem on a given UBI volume\n\n"
"Options:\n"
"-V, --version            Display version information\n"
"-g, --debug=LEVEL        Display debug information (0 - none, 1 - error message,\n"
"                         2 - warning message[default], 3 - notice message, 4 - debug message)\n"
"-a, --auto               Automatic safely repair without droping data (No questions).\n"
"                         Can not be specified at the same time as the -y or -n options\n"
"-y, --yes                Assume \"yes\" to all questions. Automatic repair and report dropping data (No questions).\n"
"                         There are two submodes for this working mode:\n"
"                           a. default - Fail if TNC/master/log is corrupted. Only -y option is specified\n"
"                           b. rebuild fs - Turn to rebuild fs if TNC/master/log is corrupted. Specify -b option to make effect\n"
"                         Can not be specified at the same time as the -a or -n options\n"
"-b, --rebuild            Forcedly repair the filesystem even by rebuilding filesystem.\n"
"                         Depends on -y option\n"
"-n, --nochange           Make no changes to the filesystem, only check filesystem.\n"
"                         Can not be specified at the same time as the -a or -y options\n"
"Examples:\n"
"\t1. Check and repair filesystem from UBI volume /dev/ubi0_0\n"
"\t   fsck.ubifs /dev/ubi0_0\n"
"\t2. Only check without modifying filesystem from UBI volume /dev/ubi0_0\n"
"\t   fsck.ubifs -n /dev/ubi0_0\n"
"\t3. Check and safely repair filesystem from UBI volume /dev/ubi0_0\n"
"\t   fsck.ubifs -a /dev/ubi0_0\n"
"\t4. Check and forcedly repair filesystem from UBI volume /dev/ubi0_0\n"
"\t   fsck.ubifs -y -b /dev/ubi0_0\n\n";

static inline void usage(void)
{
	printf("%s", helptext);
	exit_code |= FSCK_USAGE;
	exit(exit_code);
}

static void get_options(int argc, char *argv[], int *mode)
{
	int opt, i, submode = 0;
	char *endp;

	while (1) {
		opt = getopt_long(argc, argv, optstring, longopts, &i);
		if (opt == -1)
			break;
		switch (opt) {
		case 'V':
			common_print_version();
			exit(FSCK_OK);
		case 'g':
			c->debug_level = strtol(optarg, &endp, 0);
			if (*endp != '\0' || endp == optarg ||
			    c->debug_level < 0 || c->debug_level > DEBUG_LEVEL) {
				log_err(c, 0, "bad debugging level '%s'", optarg);
				usage();
			}
			break;
		case 'a':
			if (*mode != NORMAL_MODE) {
conflict_opt:
				log_err(c, 0, "Only one of the options -a, -n or -y may be specified");
				usage();
			}
			*mode = SAFE_MODE;
			break;
		case 'y':
			if (*mode != NORMAL_MODE)
				goto conflict_opt;
			*mode = DANGER_MODE0;
			break;
		case 'b':
			submode = 1;
			break;
		case 'n':
			if (*mode != NORMAL_MODE)
				goto conflict_opt;
			*mode = CHECK_MODE;
			c->ro_mount = 1;
			break;
		case 'r':
			/* Compatible with FSCK(8). */
			break;
		default:
			usage();
		}
	}

	if (submode) {
		if (*mode != DANGER_MODE0) {
			log_err(c, 0, "Option -y is not specified when -b is used");
			usage();
		} else
			*mode = DANGER_MODE1;
	}

	if (optind != argc) {
		c->dev_name = strdup(argv[optind]);
		if (!c->dev_name) {
			log_err(c, errno, "can not allocate dev_name");
			exit_code |= FSCK_ERROR;
			exit(exit_code);
		}
	}

	if (!c->dev_name) {
		log_err(c, 0, "no ubi_volume specified");
		usage();
	}
}

static void exit_callback(void)
{
	if (exit_code & FSCK_NONDESTRUCT)
		log_out(c, "********** Filesystem was modified **********");
	if (exit_code & FSCK_UNCORRECTED)
		log_out(c, "********** WARNING: Filesystem still has errors **********");
	if (exit_code & ~(FSCK_OK|FSCK_NONDESTRUCT))
		log_out(c, "FSCK failed, exit code %d", exit_code);
	else
		log_out(c, "FSCK success!");
}

static void fsck_assert_failed(__unused const struct ubifs_info *c)
{
	exit_code |= FSCK_ERROR;
	exit(exit_code);
}

static void signal_cancel(int sig)
{
	ubifs_warn(c, "killed by signo %d", sig);
	exit_code |= FSCK_CANCELED;
	exit(exit_code);
}

static int init_fsck_info(struct ubifs_info *c, int argc, char *argv[])
{
	int err = 0, mode = NORMAL_MODE;
	struct sigaction sa;
	struct ubifs_fsck_info *fsck = NULL;

	if (atexit(exit_callback)) {
		log_err(c, errno, "can not set exit callback");
		return -errno;
	}

	init_ubifs_info(c, FSCK_PROGRAM_TYPE);
	get_options(argc, argv, &mode);

	fsck = calloc(1, sizeof(struct ubifs_fsck_info));
	if (!fsck) {
		err = -errno;
		log_err(c, errno, "can not allocate fsck info");
		goto out_err;
	}

	c->private = fsck;
	FSCK(c)->mode = mode;
	c->assert_failed_cb = fsck_assert_failed;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = signal_cancel;
	if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL)) {
		err = -errno;
		log_err(c, errno, "can not set signal handler");
		goto out_err;
	}

	return 0;

out_err:
	free(fsck);
	free(c->dev_name);
	c->dev_name = NULL;
	return err;
}

static void destroy_fsck_info(struct ubifs_info *c)
{
	free(c->private);
	c->private = NULL;
	free(c->dev_name);
	c->dev_name = NULL;
}

/*
 * do_fsck - Check & repair the filesystem.
 */
static int do_fsck(void)
{
	return 0;
}

int main(int argc, char *argv[])
{
	int err;

	err = init_fsck_info(c, argc, argv);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out_exit;
	}

	err = ubifs_open_volume(c, c->dev_name);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out_destroy_fsck;
	}

	err = do_fsck();

	ubifs_close_volume(c);
out_destroy_fsck:
	destroy_fsck_info(c);
out_exit:
	return exit_code;
}
