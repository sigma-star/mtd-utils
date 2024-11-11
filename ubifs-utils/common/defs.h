/*
 * Greate deal of the code was taken from the kernel UBIFS implementation, and
 * this file contains some "glue" definitions.
 */

#ifndef __UBIFS_DEFS_H__
#define __UBIFS_DEFS_H__

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <execinfo.h>

#include "ubifs.h"

/* common.h requires the PROGRAM_NAME macro */
extern struct ubifs_info info_;
#define PROGRAM_NAME (info_.program_name)
#include "common.h"

#define MKFS_PROGRAM_NAME "mkfs.ubifs"

enum { MKFS_PROGRAM_TYPE = 0 };

enum { ERR_LEVEL = 1, WARN_LEVEL, INFO_LEVEL, DEBUG_LEVEL };

enum {
	DUMP_PREFIX_NONE,
	DUMP_PREFIX_ADDRESS,
	DUMP_PREFIX_OFFSET
};

#define pr_debug(fmt, ...) do { if (info_.debug_level >= DEBUG_LEVEL)	\
	printf("<DEBUG> %s[%d] (%s): %s: " fmt, PROGRAM_NAME, getpid(),	\
	       info_.dev_name, __FUNCTION__, ##__VA_ARGS__);		\
} while(0)

#define pr_notice(fmt, ...) do { if (info_.debug_level >= INFO_LEVEL)	\
	printf("<INFO> %s[%d] (%s): %s: " fmt, PROGRAM_NAME, getpid(),	\
	       info_.dev_name, __FUNCTION__, ##__VA_ARGS__);		\
} while(0)

#define pr_warn(fmt, ...) do { if (info_.debug_level >= WARN_LEVEL)	\
	printf("<WARN> %s[%d] (%s): %s: " fmt, PROGRAM_NAME, getpid(),	\
	       info_.dev_name, __FUNCTION__, ##__VA_ARGS__);		\
} while(0)

#define pr_err(fmt, ...) do { if (info_.debug_level >= ERR_LEVEL)	\
	printf("<ERROR> %s[%d] (%s): %s: " fmt, PROGRAM_NAME, getpid(),	\
	       info_.dev_name, __FUNCTION__, ##__VA_ARGS__);		\
} while(0)

#define pr_cont(fmt, ...) do { if (info_.debug_level >= ERR_LEVEL)	\
	printf(fmt, ##__VA_ARGS__);					\
} while(0)

static inline void dump_stack(void)
{
#define STACK_SIZE 512
	int j, nptrs;
	void *buffer[STACK_SIZE];
	char **strings;

	if (info_.debug_level < ERR_LEVEL)
		return;

	nptrs = backtrace(buffer, STACK_SIZE);
	strings = backtrace_symbols(buffer, nptrs);

	printf("dump_stack:\n");
	for (j = 0; j < nptrs; j++)
		printf("%s\n", strings[j]);

	free(strings);
}

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
