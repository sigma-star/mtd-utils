// SPDX-License-Identifier: GPL-2.0
/*
 * Realizations of bit operations.
 */

#include "bitops.h"
#include "defs.h"

/*
 * This is a common helper function for find_next_bit and
 * find_next_zero_bit.  The difference is the "invert" argument, which
 * is XORed with each fetched word before searching it for one bits.
 */
unsigned long _find_next_bit(const unsigned long *addr,
		unsigned long nbits, unsigned long start, unsigned long invert)
{
	unsigned long tmp;

	if (!nbits || start >= nbits)
		return nbits;

	tmp = addr[start / BITS_PER_LONG] ^ invert;

	/* Handle 1st word. */
	tmp &= BITMAP_FIRST_WORD_MASK(start);
	start = round_down(start, BITS_PER_LONG);

	while (!tmp) {
		start += BITS_PER_LONG;
		if (start >= nbits)
			return nbits;

		tmp = addr[start / BITS_PER_LONG] ^ invert;
	}

	return min(start + __ffs(tmp), nbits);
}
