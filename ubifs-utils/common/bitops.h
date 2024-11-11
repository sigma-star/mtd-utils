#ifndef __BITOPS_H__
#define __BITOPS_H__

/*
 * Non-atomic bitops.
 */

#include <stdbool.h>

#define BITS_PER_LONG			__LONG_WIDTH__
#define DIV_ROUND_UP(n, d)		(((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)		DIV_ROUND_UP(nr, BITS_PER_LONG)

#define BIT_MASK(nr)			(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)			((nr) / BITS_PER_LONG)
#define BITMAP_FIRST_WORD_MASK(start)	(~0UL << ((start) & (BITS_PER_LONG - 1)))

static inline void __set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p  |= mask;
}

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	__set_bit(nr, addr);
}

static inline void __clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p &= ~mask;
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	__clear_bit(nr, addr);
}

static inline bool test_bit(int nr, const volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	return (*p & mask) != 0;
}

/* Sets and returns original value of the bit */
static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	if (test_bit(nr, addr))
		return 1;
	set_bit(nr, addr);
	return 0;
}

/**
 * fls - find last (most-significant) bit set
 * @x: the word to search
 *
 * This is defined the same way as ffs.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */
static inline int fls(int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}

/**
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static inline unsigned long __ffs(unsigned long word)
{
	int num = 0;

#if BITS_PER_LONG == 64
	if ((word & 0xffffffff) == 0) {
		num += 32;
		word >>= 32;
	}
#endif
	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}
	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}
	if ((word & 0xf) == 0) {
		num += 4;
		word >>= 4;
	}
	if ((word & 0x3) == 0) {
		num += 2;
		word >>= 2;
	}
	if ((word & 0x1) == 0)
		num += 1;
	return num;
}

unsigned long _find_next_bit(const unsigned long *addr,
		unsigned long nbits, unsigned long start, unsigned long invert);

/*
 * Find the next set bit in a memory region.
 */
static inline unsigned long find_next_bit(const unsigned long *addr,
			unsigned long size, unsigned long offset)
{
	return _find_next_bit(addr, size, offset, 0UL);
}

static inline unsigned long find_next_zero_bit(const unsigned long *addr,
			unsigned long size, unsigned long offset)
{
	return _find_next_bit(addr, size, offset, ~0UL);
}

#endif
