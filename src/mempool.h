#ifndef LIBMEMPOOL
#define LIBMEMPOOL

/* We need these macros to access:
 * posix_memalign - align-aware dynamic allocation
 * ffs - finds first bit set in word
 */
#define _POSIX_C_SOURCE 201312L
#define _BSD_SOURCE 1

#include <mempool_config.h>
// we need size_t type
#include <stdlib.h>

#ifdef __cplusplus
	extern "C" {
#endif

/**
 * SLAB chunk class definition.
 * Each cached block in SLAB is considered as object. Each object has its class
 * (obviously). Each class has its own destructor/constructor. ctag may be used
 * as class identifier in the case you have the same constructor/destructor
 * for several classes in family. reinit is used when its needed to refresh
 * (recycle) constructed and cached object for the next use. Actually,
 * constructor is invoked only once during object lifecycle. It's invoked only
 * during new SLAB chunk creation for each slot. The same is true for destructor
 * as well with the difference that destructor is invoked for each slot in SLAB
 * chunk during SLAB eviction (reaping) or cache destruction. reinit is working
 * horse of object recycling. Each time object is put to the cache by client,
 * reinit will be invoked to prepare existing object for further use. It can be
 * considered as combination of lightweight destructor with lightweight
 * constructor. Class may not have ctor, dtor or reinit. Simple memory
 * allocation and freeing will be performed in this case.
 * @see cache_t
 * @see pool_create
 */
typedef struct {
	size_t blk_sz; /**< Resulting block size after adjustments and corrections
					made in cache constructor.*/
	size_t align; /**< Requested alignment of data block.*/
	void *ctag; /**< Value will be passed to ctor/dtor/reinit. Can be NULL */
	void ( *ctor )( void *obj, void *ctag ); /**< Object constructor.
												Can be NULL. */
	void ( *dtor )( void *obj, void *ctag ); /**< Object destructor.
												Can be NULL. */
	void ( *reinit )( void *obj, void *ctag ); /**< Object "recycler".
												Can be NULL. */
} slab_class_t;

/**
 * Whether blocks in chunks is referable.
 * Defines whether blocks in cache will have
 * reference counter or not. If they don't then no extra space-per-block for
 * reference counter will be allocated.
 * @see cache_t
 */
#define SLAB_REFERABLE 1

typedef struct {
	slab_list_t *( *get_slab_list )( cache_t* );
	void ( *pool_destroy )( cache_t* );
	void ( *pool_evict )( cache_t* );
} cache_class_t;

/**
 * Cache structure.
 * Structure contains all common information needed for pointer arithmetic
 * related to block/slab allocation.
 * @see slab_class_t
 * @see slab_list_t
 * @see pool_create
 * @see pool_free
 * @see pool_alloc
 */
typedef struct {
	unsigned int options; /**< Allocation options. It has the sole allowed
							option - SLAB_REFERABLE.*/
	size_t align; /**< Requested alignment of data block.*/
	size_t blk_sz; /**< Resulting block size after adjustments and corrections
					made in cache constructor.*/
	size_t header_sz; /**< Size of chunk header with accounted padding related
						to requested alignment and service hidden fields.*/
	unsigned int init_sz; /**< Cache initial size. */
	cache_class_t cache_class; /**< Cache class (type) */
	slab_class_t slab_class; /**< Object class. */
} cache_t;

/**
 * Destroys created pool (or cache).
 * Destroys created pool (or cache) with all its chunks. Deallocates memory via
 * backend routine.
 * @param cache cache going to be destroyed
 * @see pool_create
 * @see pool_reap
 */
static inline void pool_free( cache_t *cache ) {
	assert( cache != NULL );
	cache->cache_class.pool_destroy( cache );
	free( cache );
}

/**
 * Evicts empty chunks.
 * Deallocates absolutely free chunks (chunks don't contain any
 * allocated blocks) and evicts them from chunk list. Be used when it's needed
 * to free some memory (during high memory pressure, for example).
 * @param cache cache which empty chunks will be evicted
 * @see pool_free
 */
static inline void pool_reap( cache_t *cache ) {
	cache->cache_class.pool_evict( cache )
}

/**
 * Allocates block from pool (cache).
 * Allocates block marked as unallocated from one of the chunks of the cache.
 * Allocates new chunk if all existing chunks are empty (has no free blocks).
 * If it's requested to be reference-aware then block reference counter will be
 * set to 1.
 * @param cache cache which block will be allocated from
 * @return !=NULL - allocated block; ==NULL - something went wrong
 * @see pool_object_get
 * @see pool_object_put
 */
extern void *pool_object_alloc( cache_t *cache );

/**
 * Increments block reference counter.
 * If it's requested to be reference-aware then reference counter of the block
 * will be incremented. Nothing will be done otherwise.
 * @param cache cache which block will be allocated from
 * @param obj allocated block
 * @return obj will be returned
 */
extern void *pool_object_get( cache_t *cache, void *obj );

/**
 * Decrements reference counter/frees the block.
 * Decrements reference counter of the block if the case. If counter approaches
 * zero or if cache doesn't support reference counters then the routine will
 * free the block (mark block as unallocated in corresponding chunk). In other
 * words, it will return object back to the cache.
 * @param cache cache which block will be allocated from
 * @param obj block itself
 * @return != NULL - block itself (reference counter was decreased);
 * 			== NULL - block was returned back to the cache (was freed)
 */
extern void *pool_object_put( cache_t *cache, void *obj );

#ifdef __cplusplus
	}
#endif

#endif
