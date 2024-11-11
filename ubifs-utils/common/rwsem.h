/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_RWSEM_H_
#define __LINUX_RWSEM_H_

#include <pthread.h>

struct rw_semaphore {
	pthread_mutex_t lock;
};

#define init_rwsem(x)			pthread_mutex_init(&(x)->lock, NULL)

#define down_read(x)			pthread_mutex_lock(&(x)->lock)
#define down_write(x)			pthread_mutex_lock(&(x)->lock)
#define up_read(x)			pthread_mutex_unlock(&(x)->lock)
#define up_write(x)			pthread_mutex_unlock(&(x)->lock)
#define down_write_trylock(x)		(pthread_mutex_trylock(&(x)->lock) == 0)

#endif
