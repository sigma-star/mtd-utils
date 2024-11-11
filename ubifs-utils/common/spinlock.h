/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SPINLOCK_H_
#define __LINUX_SPINLOCK_H_

#include <pthread.h>

#define spinlock_t		pthread_mutex_t
#define DEFINE_SPINLOCK(x)	pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER
#define spin_lock_init(x)	pthread_mutex_init(x, NULL)

#define spin_lock(x)		pthread_mutex_lock(x)
#define spin_unlock(x)		pthread_mutex_unlock(x)

#endif
