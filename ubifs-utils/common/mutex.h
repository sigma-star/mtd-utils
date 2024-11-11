/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MUTEX_H_
#define __LINUX_MUTEX_H_

#include <pthread.h>

struct mutex {
	pthread_mutex_t lock;
};

#define mutex_init(x)		pthread_mutex_init(&(x)->lock, NULL)

#define mutex_lock(x)		pthread_mutex_lock(&(x)->lock)
#define mutex_lock_nested(x, c)	pthread_mutex_lock(&(x)->lock)
#define mutex_unlock(x)		pthread_mutex_unlock(&(x)->lock)
#define mutex_is_locked(x)	(pthread_mutex_trylock(&(x)->lock) == EBUSY)

#endif
