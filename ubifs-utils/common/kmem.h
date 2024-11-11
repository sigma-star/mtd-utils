// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2008 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __KMEM_H__
#define __KMEM_H__

#include <stdlib.h>

typedef unsigned int gfp_t;

#define GFP_KERNEL	0
#define GFP_NOFS	0
#define __GFP_NOWARN	0
#define __GFP_ZERO	1

#define vmalloc(size)	malloc(size)
#define vfree(ptr)	free(ptr)

extern void	*kmalloc(size_t, gfp_t);
extern void	*krealloc(void *, size_t, __attribute__((unused)) gfp_t);
extern void	*kmalloc_array(size_t, size_t, gfp_t);
extern void	*kmemdup(const void *src, size_t len, gfp_t gfp);

static inline void kfree(const void *ptr)
{
	free((void *)ptr);
}

static inline void kvfree(const void *ptr)
{
	kfree(ptr);
}

static inline void *kvmalloc(size_t size, gfp_t flags)
{
	return kmalloc(size, flags);
}

static inline void *kzalloc(size_t size, gfp_t flags)
{
	return kmalloc(size, flags | __GFP_ZERO);
}

static inline void *__vmalloc(unsigned long size, gfp_t gfp_mask)
{
	return kmalloc(size, gfp_mask);
}

static inline void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	return kmalloc_array(n, size, flags | __GFP_ZERO);
}

#endif
