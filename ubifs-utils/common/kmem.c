// SPDX-License-Identifier: GPL-2.0
/*
 * Simple memory interface
 */

#include "compiler_attributes.h"
#include "linux_types.h"
#include "kmem.h"
#include "defs.h"

static void *kmem_alloc(size_t size)
{
	void *ptr = malloc(size);

	if (ptr == NULL)
		sys_errmsg("malloc failed (%d bytes)", (int)size);
	return ptr;
}

static void *kmem_zalloc(size_t size)
{
	void *ptr = kmem_alloc(size);

	if (!ptr)
		return ptr;

	memset(ptr, 0, size);
	return ptr;
}

void *kmalloc(size_t size, gfp_t flags)
{
	if (flags & __GFP_ZERO)
		return kmem_zalloc(size);
	return kmem_alloc(size);
}

void *krealloc(void *ptr, size_t new_size, __unused gfp_t flags)
{
	ptr = realloc(ptr, new_size);
	if (ptr == NULL)
		sys_errmsg("realloc failed (%d bytes)", (int)new_size);
	return ptr;
}

void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;
	return kmalloc(bytes, flags);
}

void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
	void *p;

	p = kmalloc(len, gfp);
	if (p)
		memcpy(p, src, len);

	return p;
}
