/*
 * Greate deal of the code was taken from the kernel UBIFS implementation, and
 * this file contains some "glue" definitions.
 */

#ifndef __UBIFS_DEFS_H__
#define __UBIFS_DEFS_H__

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>

#include "ubifs.h"

/* common.h requires the PROGRAM_NAME macro */
extern struct ubifs_info info_;
#define PROGRAM_NAME (info_.program_name)
#include "common.h"

#define MKFS_PROGRAM_NAME "mkfs.ubifs"

enum { MKFS_PROGRAM_TYPE = 0 };

#define dbg_msg(lvl, fmt, ...) do {if (info_.debug_level >= lvl)	\
	printf("%s: %s: " fmt "\n", PROGRAM_NAME, __FUNCTION__, ##__VA_ARGS__); \
} while(0)

#define unlikely(x) (x)

#define do_div(n,base) ({ \
int __res; \
__res = ((unsigned long) n) % (unsigned) base; \
n = ((unsigned long) n) / (unsigned) base; \
__res; })

#if INT_MAX != 0x7fffffff
#error : sizeof(int) must be 4 for this program
#endif

#if (~0ULL) != 0xffffffffffffffffULL
#error : sizeof(long long) must be 8 for this program
#endif

#endif
