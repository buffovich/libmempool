#ifndef LIBMEMPOOL_LOCKLESS_H
#define LIBMEMPOOL_LOCKLESS_H

#include <mempool.h>

extern cache_t *pool_lockless_create( unsigned int options,
	slab_class_t *slab_class,
	unsigned int inum
);

#endif
