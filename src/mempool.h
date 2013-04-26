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

#if LIBMEMPOOL_MULTITHREADED
	#include <pthread.h>
#endif


/**
 * Map of blocks in SLAB chunk.
 * SLAB chunk has fixed size in blocks. The size is bit length of
 * unsigned integer on target architecture. With such simple design decision
 * we can check chunk for emptiness/saturation with one processor instruction.
 * Chunk elements are named "slots"
 * @see slab_t
 */
typedef unsigned int blockmap_t;

/**
 * SLAB chunk header.
 * Allocation in cache is performed in chunks. Each time we need extra space
 * in cache, extra chunk is allocated via allocation backend. Then, newly
 * allocated chunk will be placed at the head of chunk list of cache.
 * @see cache_t
 */
typedef struct _slab_t {
	struct _slab_t *next; /**< Pointer to the next chunk in list.*/
	struct _slab_t *prev; /**< Pointer to the previous chunk in list.*/
	blockmap_t map; /**< Bitmap of free and occupied blocks.*/
} slab_t;

typedef struct {
	size_t blk_sz;
	unsigned int align;
	void *ctag;
	void ( *ctor )( void *obj, void *ctag );
	void ( *dtor )( void *obj, void *ctag );
	void ( *reinit )( void *obj, void *ctag );
} slab_class_t;

/**
 * Whether blocks in chunks is referable.
 * The sole cache creation option. Defines whether blocks in cache will have
 * reference counter or not. If they don't then no extra space-per-block for
 * reference counter will be allocated.
 * @see cache_t
 */
#define SLAB_REFERABLE 1

/**
 * Cache structure.
 * Structure contains all needed information for pointer arithmetic and
 * pointers to the head and the tail of chunks list. Such split allows
 * performing alloc/free operations in constant time. Each time there is no
 * more place in current head chunk (chunk is saturated), library checks the
 * next chunk for the availability of unallocated blocks. If it's saturated
 * as well then new chunk will be created and inserted at the head of the list.
 * If there are some free blocks, next chunk becomes new head. In the meantime,
 * the former head becomes new tail. Hence, you will have partially filled/empty
 * chunks placed firstly and fully saturated chunks placed secondly. If block
 * is released in the one of chunks then it becomes new head as well.
 * @see pool_create
 * @see pool_free
 * @see pool_alloc
 */
typedef struct {
	#if LIBMEMPOOL_MULTITHREADED
		pthread_mutex_t protect;
	#endif
	
	unsigned int options; /**< Allocation options. It has the sole allowed
							option - SLAB_REFERABLE.*/
	size_t align; /**< Requested alignment of data block.*/
	size_t blk_sz; /**< Resulting block size after adjustments and corrections
					made in cache constructor.*/
	size_t header_sz; /**< Size of chunk header with accounted padding related
						to requested alignment and service hidden fields.*/
	slab_class_t *slab_class;
	
	slab_t *head; /**< The head of the cache's chunk list.*/
	slab_t *tail; /**< The tail of the cache's chunk list.*/
} cache_t;

/**
 * Creates new object cache (or pool).
 * Creates new object cache (or pool) which will be able to allocate
 * chuncks with specified blk_sz size  and will follow specified block
 * alignment. Firstly, inum blocks will be reserved in cache for immediate
 * use. Currently, options parameter controls only whether blocks will have
 * reference counter or not.
 * @param options cache options (SLAB_REFERABLE is the only allowed option)
 * @param blk_sz block size in bytes
 * @param align alignment block address should have after allocation
 * @param inum number of blocks will be reserved for immediate use
 * @return !=NULL - it will be cache object; NULL - something went wrong
 * @see pool_free
 */
extern cache_t *pool_create( unsigned int options,
	slab_class_t *slab_class,
	unsigned int inum
);

/**
 * Destroys created pool (or cache).
 * Destroys created pool (or cache) with all its chunks. Deallocates memory via
 * backend routine.
 * @param cache cache going to be destroyed
 * @see pool_create
 * @see pool_reap
 */
extern void pool_free( cache_t *cache );

/**
 * Evicts empty chunks.
 * Deallocates absolutely free chunks (chunks don't contain any
 * allocated blocks) and evicts them from chunk list. Be used when it's needed
 * to free some memory (during high memory pressure, for example).
 * @param cache cache which empty chunks will be evicted
 * @see pool_free
 */
extern void pool_reap( cache_t *cache );

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

#endif
