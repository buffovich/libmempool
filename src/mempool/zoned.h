#ifndef LIBMEMPOOL_ZONED_H
#define LIBMEMPOOL_ZONED_H

#include <mmepool.h>

extern cache_t *pool_zone_create( unsigned int options,
	slab_class_t *slab_class,
	unsigned int inum
);

#endif
