//Source: http://golubenco.org/atomic-operations.html
#ifndef __ATOMIC_H__
#define __ATOMIC_H__

/* Check GCC version, just to be safe */
#if !defined(__GNUC__) || (__GNUC__ < 4) || (__GNUC_MINOR__ < 1)
# error atomic.h works only with GCC newer than version 4.1
#endif /* GNUC >= 4.1 */

/**
 * Atomic type.
 */
typedef struct {
	volatile long counter;
} atomic_long_t;

#define ATOMIC_INIT(i)  { (i) }

/**
 * Read atomic variable
 * @param v pointer of type atomic_long_t
 *
 * Atomically reads the value of @v.
 */
#define atomic_long_read(v) ((v)->counter)

/**
 * Set atomic variable
 * @param v pointer of type atomic_long_t
 * @param i required value
 */
#define atomic_long_set(v,i) (((v)->counter) = (i))

/**
 * Add to the atomic variable
 * @param i integer value to add
 * @param v pointer of type atomic_long_t
 */
static inline void atomic_long_add( int i, atomic_long_t *v )
{
	(void)__sync_add_and_fetch(&v->counter, i);
}

/**
 * Subtract the atomic variable
 * @param i integer value to subtract
 * @param v pointer of type atomic_long_t
 *
 * Atomically subtracts @i from @v.
 */
static inline void atomic_long_sub( int i, atomic_long_t *v )
{
	(void)__sync_sub_and_fetch(&v->counter, i);
}

/**
 * Subtract value from variable and test result
 * @param i integer value to subtract
 * @param v pointer of type atomic_long_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
static inline int atomic_long_sub_and_test( int i, atomic_long_t *v )
{
	return !(__sync_sub_and_fetch(&v->counter, i));
}

/**
 * Increment atomic variable
 * @param v pointer of type atomic_long_t
 *
 * Atomically increments @v by 1.
 */
static inline void atomic_long_inc( atomic_long_t *v )
{
	(void)__sync_fetch_and_add(&v->counter, 1);
}

/**
 * @brief decrement atomic variable
 * @param v: pointer of type atomic_long_t
 *
 * Atomically decrements @v by 1.  Note that the guaranteed
 * useful range of an atomic_long_t is only 24 bits.
 */
static inline void atomic_long_dec( atomic_long_t *v )
{
	(void)__sync_fetch_and_sub(&v->counter, 1);
}

/**
 * @brief Decrement and test
 * @param v pointer of type atomic_long_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
static inline int atomic_long_dec_and_test( atomic_long_t *v )
{
	return !(__sync_sub_and_fetch(&v->counter, 1));
}

/**
 * @brief Increment and test
 * @param v pointer of type atomic_long_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
static inline int atomic_long_inc_and_test( atomic_long_t *v )
{
	return !(__sync_add_and_fetch(&v->counter, 1));
}

/**
 * @brief add and test if negative
 * @param v pointer of type atomic_long_t
 * @param i integer value to add
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
static inline int atomic_long_add_negative( int i, atomic_long_t *v )
{
	return (__sync_add_and_fetch(&v->counter, i) < 0);
}

#endif
