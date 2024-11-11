// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Huawei Technologies Co, Ltd.
 *
 * Authors: Zhihao Cheng <chengzhihao1@huawei.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "fsck.ubifs.h"

/*
 * problem flags.
 *
 * PROBLEM_FIXABLE: problem is fixable, unsolvable problem such as corrupted
 *		    super block will abort the fsck program
 * PROBLEM_MUST_FIX: problem must be fixed because it will affect the subsequent
 *		     fsck process, otherwise aborting the fsck program
 * PROBLEM_DROP_DATA: user data could be dropped after fixing the problem
 * PROBLEM_NEED_REBUILD: rebuilding filesystem is needed to fix the problem
 */
#define PROBLEM_FIXABLE		(1<<0)
#define PROBLEM_MUST_FIX	(1<<1)
#define PROBLEM_DROP_DATA	(1<<2)
#define PROBLEM_NEED_REBUILD	(1<<3)

struct fsck_problem {
	unsigned int flags;
	const char *desc;
};

static const struct fsck_problem problem_table[] = {
	{0, "Corrupted superblock"},	// SB_CORRUPTED
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA | PROBLEM_NEED_REBUILD, "Corrupted master node"},	// MST_CORRUPTED
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA | PROBLEM_NEED_REBUILD, "Corrupted log area"},	// LOG_CORRUPTED
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA, "Corrupted bud LEB"},	// BUD_CORRUPTED
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA | PROBLEM_NEED_REBUILD, "Corrupted index node"},	// TNC_CORRUPTED
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA, "Corrupted data searched from TNC"},	// TNC_DATA_CORRUPTED
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA, "Corrupted orphan LEB"},	// ORPHAN_CORRUPTED
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA, "Invalid inode node"},	// INVALID_INO_NODE
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA, "Invalid dentry node"},	// INVALID_DENT_NODE
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA, "Invalid data node"},	// INVALID_DATA_NODE
	{PROBLEM_FIXABLE | PROBLEM_MUST_FIX | PROBLEM_DROP_DATA, "Corrupted data is scanned"},	// SCAN_CORRUPTED
};

static const char *get_question(const struct fsck_problem *problem,
				int problem_type)
{
	if (problem->flags & PROBLEM_NEED_REBUILD)
		return "Rebuild filesystem?";

	switch (problem_type) {
	case BUD_CORRUPTED:
		return "Drop bud?";
	case TNC_DATA_CORRUPTED:
	case INVALID_INO_NODE:
	case INVALID_DENT_NODE:
	case INVALID_DATA_NODE:
	case SCAN_CORRUPTED:
		return "Drop it?";
	case ORPHAN_CORRUPTED:
		return "Drop orphans on the LEB?";
	}

	return "Fix it?";
}

static void print_problem(const struct ubifs_info *c,
			  const struct fsck_problem *problem, int problem_type,
			  const void *priv)
{
	switch (problem_type) {
	case BUD_CORRUPTED:
	{
		const struct ubifs_bud *bud = (const struct ubifs_bud *)priv;

		log_out(c, "problem: %s %d:%d %s", problem->desc, bud->lnum,
			bud->start, dbg_jhead(bud->jhead));
		break;
	}
	case ORPHAN_CORRUPTED:
	{
		const int *lnum = (const int *)priv;

		log_out(c, "problem: %s %d", problem->desc, *lnum);
		break;
	}
	case SCAN_CORRUPTED:
	{
		const struct ubifs_zbranch *zbr = (const struct ubifs_zbranch *)priv;

		log_out(c, "problem: %s in LEB %d, node in %d:%d becomes invalid",
			problem->desc, zbr->lnum, zbr->lnum, zbr->offs);
		break;
	}
	default:
		log_out(c, "problem: %s", problem->desc);
		break;
	}
}

static void fatal_error(const struct ubifs_info *c,
			const struct fsck_problem *problem)
{
	if (!(problem->flags & PROBLEM_FIXABLE))
		log_out(c, "inconsistent problem cannot be fixed");
	else
		log_out(c, "inconsistent problem must be fixed");
	exit(exit_code);
}

/**
 * fix_problem - whether fixing the inconsistent problem
 * @c: UBIFS file-system description object
 * @problem_type: the type of inconsistent problem
 * @priv: private data for problem instance
 *
 * This function decides to fix/skip the inconsistent problem or abort the
 * program according to @problem_type, returns %true if the problem should
 * be fixed, returns %false if the problem will be skipped.
 */
bool fix_problem(const struct ubifs_info *c, int problem_type, const void *priv)
{
	bool ans, ask = true, def_y = true;
	const struct fsck_problem *problem = &problem_table[problem_type];
	const char *question = get_question(problem, problem_type);

	ubifs_assert(c, FSCK(c)->mode != REBUILD_MODE);

	if (!(problem->flags & PROBLEM_FIXABLE)) {
		exit_code |= FSCK_UNCORRECTED;
		fatal_error(c, problem);
	}

	if (FSCK(c)->mode == CHECK_MODE ||
	    ((problem->flags & PROBLEM_DROP_DATA) && FSCK(c)->mode == SAFE_MODE) ||
	    ((problem->flags & PROBLEM_NEED_REBUILD) &&
	     (FSCK(c)->mode == SAFE_MODE || FSCK(c)->mode == DANGER_MODE0)))
		def_y = false;

	if ((problem->flags & PROBLEM_NEED_REBUILD) &&
	    (FSCK(c)->mode == DANGER_MODE0 || FSCK(c)->mode == DANGER_MODE1))
		ask = false;

	print_problem(c, problem, problem_type, priv);
	ans = def_y;
	if (FSCK(c)->mode == NORMAL_MODE) {
		printf("%s[%d] (%s%s)", c->program_name, getpid(),
		       c->dev_name ? : "-", mode_name(c));
		if (prompt(question, def_y))
			ans = true;
		else
			ans = false;
	} else {
		if (ask)
			log_out(c, "%s %c\n", question, def_y ? 'y' : 'n');
	}

	if (!ans) {
		exit_code |= FSCK_UNCORRECTED;
		if (problem->flags & PROBLEM_MUST_FIX)
			fatal_error(c, problem);
	} else {
		exit_code |= FSCK_NONDESTRUCT;
	}

	return ans;
}
