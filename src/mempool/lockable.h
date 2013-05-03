#ifndef LIBMEMPOOL_LOCKABLE_H
#define LIBMEMPOOL_LOCKABLE_H

#include <mempool.h>

extern cache_t *pool_lockable_create( unsigned int options,
	slab_class_t *slab_class,
	unsigned int inum
);

#endif
