#ifndef LIBMEMPOOL_SIMPLE_H
#define LIBMEMPOOL_SIMPLE_H

#include <mempool.h>

/**
 * Creates new object cache (or pool).
 * Creates new object cache (or pool) which will be able to allocate
 * chuncks with specified blk_sz size  and will follow specified block
 * alignment. Firstly, inum blocks will be reserved in cache for immediate
 * use. Currently, options parameter controls only whether blocks will have
 * reference counter or not.
 * @param options cache options
 * @param slab_class SLAB object class
 * @param inum number of blocks will be reserved for immediate use
 * @return !=NULL - it will be cache object; NULL - something went wrong
 * @see pool_free
 * @see cache_t
 * @see slab_class_t
 */
extern cache_t *pool_simple_create( unsigned int options,
	slab_class_t *slab_class,
	unsigned int inum
);

#endif
