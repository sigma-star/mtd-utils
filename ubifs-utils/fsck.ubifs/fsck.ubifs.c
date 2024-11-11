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
"                         This mode don't check space, because unclean LEBs are not rewritten in readonly mode.\n"
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

static void fsck_set_failure_reason(const struct ubifs_info *c,
				    unsigned int reason)
{
	if (FSCK(c)->mode == REBUILD_MODE)
		return;

	FSCK(c)->failure_reason = reason;
	if (reason & FR_LPT_CORRUPTED) {
		log_out(c, "Found corrupted pnode/nnode, set lpt corrupted");
		FSCK(c)->lpt_status |= FR_LPT_CORRUPTED;
	}
	if (reason & FR_LPT_INCORRECT) {
		log_out(c, "Bad space statistics, set lpt incorrect");
		FSCK(c)->lpt_status |= FR_LPT_INCORRECT;
	}
}

static unsigned int fsck_get_failure_reason(const struct ubifs_info *c)
{
	ubifs_assert(c, FSCK(c)->mode != REBUILD_MODE);

	return FSCK(c)->failure_reason;
}

static void fsck_clear_failure_reason(const struct ubifs_info *c)
{
	ubifs_assert(c, FSCK(c)->mode != REBUILD_MODE);

	FSCK(c)->failure_reason = 0;
}

static bool fsck_test_and_clear_failure_reason(const struct ubifs_info *c,
					       unsigned int reason)
{
	bool res = (FSCK(c)->failure_reason & reason) != 0;

	ubifs_assert(c, FSCK(c)->mode != REBUILD_MODE);
	ubifs_assert(c, !(FSCK(c)->failure_reason & (~reason)));

	FSCK(c)->failure_reason = 0;

	return res;
}

static void fsck_set_lpt_invalid(const struct ubifs_info *c,
				 unsigned int reason)
{
	ubifs_assert(c, FSCK(c)->mode != REBUILD_MODE);

	if (reason & FR_LPT_CORRUPTED) {
		log_out(c, "Found corrupted pnode/nnode, set lpt corrupted");
		FSCK(c)->lpt_status |= FR_LPT_CORRUPTED;
	}
	if (reason & FR_LPT_INCORRECT) {
		log_out(c, "Bad space statistics, set lpt incorrect");
		FSCK(c)->lpt_status |= FR_LPT_INCORRECT;
	}
}

static bool fsck_test_lpt_valid(const struct ubifs_info *c, int lnum,
				int old_free, int old_dirty,
				int free, int dirty)
{
	ubifs_assert(c, FSCK(c)->mode != REBUILD_MODE);

	if (c->cmt_state != COMMIT_RESTING)
		/* Don't skip updating lpt when do commit. */
		goto out;

	if (FSCK(c)->lpt_status)
		return false;

	if (c->lst.empty_lebs < 0 || c->lst.empty_lebs > c->main_lebs) {
		log_out(c, "Bad empty_lebs %d(main_lebs %d), set lpt incorrect",
			c->lst.empty_lebs, c->main_lebs);
		goto out_invalid;
	}
	if (c->freeable_cnt < 0 || c->freeable_cnt > c->main_lebs) {
		log_out(c, "Bad freeable_cnt %d(main_lebs %d), set lpt incorrect",
			c->freeable_cnt, c->main_lebs);
		goto out_invalid;
	}
	if (c->lst.taken_empty_lebs < 0 ||
	    c->lst.taken_empty_lebs > c->lst.empty_lebs) {
		log_out(c, "Bad taken_empty_lebs %d(empty_lebs %d), set lpt incorrect",
			c->lst.taken_empty_lebs, c->lst.empty_lebs);
		goto out_invalid;
	}
	if (c->lst.total_free & 7) {
		log_out(c, "total_free(%lld) is not 8 bytes aligned, set lpt incorrect",
			c->lst.total_free);
		goto out_invalid;
	}
	if (c->lst.total_dirty & 7) {
		log_out(c, "total_dirty(%lld) is not 8 bytes aligned, set lpt incorrect",
			c->lst.total_dirty);
		goto out_invalid;
	}
	if (c->lst.total_dead & 7) {
		log_out(c, "total_dead(%lld) is not 8 bytes aligned, set lpt incorrect",
			c->lst.total_dead);
		goto out_invalid;
	}
	if (c->lst.total_dark & 7) {
		log_out(c, "total_dark(%lld) is not 8 bytes aligned, set lpt incorrect",
			c->lst.total_dark);
		goto out_invalid;
	}
	if (c->lst.total_used & 7) {
		log_out(c, "total_used(%lld) is not 8 bytes aligned, set lpt incorrect",
			c->lst.total_used);
		goto out_invalid;
	}
	if (old_free != LPROPS_NC && (old_free & 7)) {
		log_out(c, "LEB %d old_free(%d) is not 8 bytes aligned, set lpt incorrect",
			lnum, old_free);
		goto out_invalid;
	}
	if (old_dirty != LPROPS_NC && (old_dirty & 7)) {
		log_out(c, "LEB %d old_dirty(%d) is not 8 bytes aligned, set lpt incorrect",
			lnum, old_dirty);
		goto out_invalid;
	}
	if (free != LPROPS_NC && (free < 0 || free > c->leb_size)) {
		log_out(c, "LEB %d bad free %d(leb_size %d), set lpt incorrect",
			lnum, free, c->leb_size);
		goto out_invalid;
	}
	if (dirty != LPROPS_NC && dirty < 0) {
		/* Dirty may be more than c->leb_size before set_bud_lprops. */
		log_out(c, "LEB %d bad dirty %d(leb_size %d), set lpt incorrect",
			lnum, dirty, c->leb_size);
		goto out_invalid;
	}

out:
	return true;

out_invalid:
	FSCK(c)->lpt_status |= FR_LPT_INCORRECT;
	return false;
}

static bool fsck_can_ignore_failure(const struct ubifs_info *c,
				    unsigned int reason)
{
	ubifs_assert(c, FSCK(c)->mode != REBUILD_MODE);

	if (c->cmt_state != COMMIT_RESTING)
		/* Don't ignore failure when do commit. */
		return false;
	if (reason & (FR_LPT_CORRUPTED | FR_LPT_INCORRECT))
		return true;

	return false;
}

static const unsigned int reason_mapping_table[] = {
	BUD_CORRUPTED,		/* FR_H_BUD_CORRUPTED */
	TNC_DATA_CORRUPTED,	/* FR_H_TNC_DATA_CORRUPTED */
	ORPHAN_CORRUPTED,	/* FR_H_ORPHAN_CORRUPTED */
	LTAB_INCORRECT,		/* FR_H_LTAB_INCORRECT */
};

static bool fsck_handle_failure(const struct ubifs_info *c, unsigned int reason,
				void *priv)
{
	ubifs_assert(c, FSCK(c)->mode != REBUILD_MODE);

	return fix_problem(c, reason_mapping_table[reason], priv);
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
	INIT_LIST_HEAD(&FSCK(c)->disconnected_files);
	c->assert_failed_cb = fsck_assert_failed;
	c->set_failure_reason_cb = fsck_set_failure_reason;
	c->get_failure_reason_cb = fsck_get_failure_reason;
	c->clear_failure_reason_cb = fsck_clear_failure_reason;
	c->test_and_clear_failure_reason_cb = fsck_test_and_clear_failure_reason;
	c->set_lpt_invalid_cb = fsck_set_lpt_invalid;
	c->test_lpt_valid_cb = fsck_test_lpt_valid;
	c->can_ignore_failure_cb = fsck_can_ignore_failure;
	c->handle_failure_cb = fsck_handle_failure;

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

void handle_error(const struct ubifs_info *c, int reason_set)
{
	bool handled = false;
	unsigned int reason = get_failure_reason_callback(c);

	clear_failure_reason_callback(c);
	if ((reason_set & HAS_DATA_CORRUPTED) && (reason & FR_DATA_CORRUPTED)) {
		handled = true;
		reason &= ~FR_DATA_CORRUPTED;
		if (fix_problem(c, LOG_CORRUPTED, NULL))
			FSCK(c)->try_rebuild = true;
	}
	if ((reason_set & HAS_TNC_CORRUPTED) && (reason & FR_TNC_CORRUPTED)) {
		ubifs_assert(c, !handled);
		handled = true;
		reason &= ~FR_TNC_CORRUPTED;
		if (fix_problem(c, TNC_CORRUPTED, NULL))
			FSCK(c)->try_rebuild = true;
	}

	ubifs_assert(c, reason == 0);
	if (!handled)
		exit_code |= FSCK_ERROR;
}

static int commit_fix_modifications(struct ubifs_info *c, bool final_commit)
{
	int err;

	if (final_commit) {
		log_out(c, "Final committing");
		c->mst_node->flags &= ~cpu_to_le32(UBIFS_MST_DIRTY);
		c->mst_node->gc_lnum = cpu_to_le32(c->gc_lnum);
		/* Force UBIFS to do commit by setting @c->mounting. */
		c->mounting = 1;
	} else if (exit_code & FSCK_NONDESTRUCT) {
		log_out(c, "Commit problem fixing modifications");
		/* Force UBIFS to do commit by setting @c->mounting. */
		c->mounting = 1;
	}

	err = ubifs_run_commit(c);

	if (c->mounting)
		c->mounting = 0;

	return err;
}

/*
 * do_fsck - Check & repair the filesystem.
 */
static int do_fsck(void)
{
	int err;

	log_out(c, "Traverse TNC and construct files");
	err = traverse_tnc_and_construct_files(c);
	if (err) {
		handle_error(c, HAS_TNC_CORRUPTED);
		return err;
	}

	update_files_size(c);

	log_out(c, "Check and handle invalid files");
	err = handle_invalid_files(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto free_used_lebs;
	}

	log_out(c, "Check and handle unreachable files");
	err = handle_dentry_tree(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto free_disconnected_files;
	}

	log_out(c, "Check and correct files");
	err = check_and_correct_files(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto free_disconnected_files;
	}

	log_out(c, "Check whether the TNC is empty");
	if (tnc_is_empty(c) && fix_problem(c, EMPTY_TNC, NULL)) {
		err = -EINVAL;
		FSCK(c)->try_rebuild = true;
		goto free_disconnected_files;
	}

	err = check_and_correct_space(c);
	kfree(FSCK(c)->used_lebs);
	destroy_file_tree(c, &FSCK(c)->scanned_files);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto free_disconnected_files_2;
	}

	/*
	 * Committing is required once before allocating new space(subsequent
	 * steps may need), because building lpt could mark LEB(which holds
	 * stale data nodes) as unused, if the LEB is overwritten by new data,
	 * old data won't be found in the next fsck run(assume that first fsck
	 * run is interrupted by the powercut), which could affect the
	 * correctness of LEB properties after replaying journal in the second
	 * fsck run.
	 */
	err = commit_fix_modifications(c, false);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto free_disconnected_files_2;
	}

	log_out(c, "Check and correct the index size");
	err = check_and_correct_index_size(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto free_disconnected_files_2;
	}

	log_out(c, "Check and create root dir");
	err = check_and_create_root(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto free_disconnected_files_2;
	}

	if (list_empty(&FSCK(c)->disconnected_files))
		goto final_commit;

	log_out(c, "Check and create lost+found");
	err = check_and_create_lost_found(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto free_disconnected_files_2;
	}

	log_out(c, "Handle disconnected files");
	err = handle_disonnected_files(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto free_disconnected_files_2;
	}

final_commit:
	err = commit_fix_modifications(c, true);
	if (err)
		exit_code |= FSCK_ERROR;

	return err;

free_disconnected_files_2:
	destroy_file_list(c, &FSCK(c)->disconnected_files);
	return err;

free_disconnected_files:
	destroy_file_list(c, &FSCK(c)->disconnected_files);
free_used_lebs:
	kfree(FSCK(c)->used_lebs);
	destroy_file_tree(c, &FSCK(c)->scanned_files);
	return err;
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

	/*
	 * Init: Read superblock
	 * Step 1: Read master & init lpt
	 * Step 2: Replay journal
	 * Step 3: Handle orphan nodes
	 * Step 4: Consolidate log
	 * Step 5: Recover isize
	 */
	err = ubifs_load_filesystem(c);
	if (err) {
		if (FSCK(c)->try_rebuild)
			ubifs_rebuild_filesystem(c);
		goto out_close;
	}

	/*
	 * Step 6: Traverse tnc and construct files
	 * Step 7: Update files' size
	 * Step 8: Check and handle invalid files
	 * Step 9: Check and handle unreachable files
	 * Step 10: Check and correct files
	 * Step 11: Check whether the TNC is empty
	 * Step 12: Check and correct the space statistics
	 * Step 13: Commit problem fixing modifications
	 * Step 14: Check and correct the index size
	 * Step 15: Check and create root dir
	 * Step 16: Check and create lost+found
	 * Step 17: Handle disconnected files
	 * Step 18: Do final committing
	 */
	err = do_fsck();
	if (err && FSCK(c)->try_rebuild) {
		ubifs_destroy_filesystem(c);
		ubifs_rebuild_filesystem(c);
	} else {
		ubifs_destroy_filesystem(c);
	}

out_close:
	ubifs_close_volume(c);
out_destroy_fsck:
	destroy_fsck_info(c);
out_exit:
	return exit_code;
}
