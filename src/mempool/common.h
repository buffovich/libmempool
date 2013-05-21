#ifndef LIBMEMPOOL_COMMON
#define LIBMEMPOOL_COMMON

#if LIBMEMPOOL_LOCKLESS
	#include <atomic_ops.h>
#endif

#ifdef __cplusplus
	extern "C" {
#endif

/**
 * Map of blocks in SLAB chunk.
 * SLAB chunk has fixed size in blocks. The size is bit length of
 * unsigned integer on target architecture. With such simple design decision
 * we can check chunk for emptiness/saturation with one processor instruction.
 * Chunk elements are named "slots"
 * @see slab_t
 */
#if LIBMEMPOOL_LOCKLESS
	typedef AO_t blockmap_t;
	typedef AO_t counter_t;
#else
	typedef unsigned int blockmap_t;
	typedef unsigned int counter_t;
#endif

#define COUNTER_ALIGN ( alignof( counter_t ) )

#define COUNTER_SIZE ( sizeof( counter_t ) )

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

/**
 * Structure represents double linked linear list.
 * Structure contains pointers to the head and the tail of chunks list. This 
 * split allows performing alloc/free operations in constant time.
 * Each time there is no more place in current head chunk (chunk is saturated),
 * library checks the next chunk for the availability of unallocated blocks.
 * If it's saturated as well then new chunk will be created and inserted at
 * the head of the list. If there are some free blocks, next chunk becomes new
 * head. In the meantime, the former head becomes new tail. Hence, you will
 * have partially filled/empty chunks placed firstly and fully saturated chunks
 * placed secondly. If block is released in the one of chunks then it becomes
 * new head as well.
 * @see slab_t
 * @see cache_t
 */
typedef struct {
	slab_t *free_list;
	slab_t *partial_list;
	slab_t *full_list;
} slab_list_t;

static inline void *_bzero( size_t sz ) {
	void *b = malloc( sz );
	memset( c, 0, sz );
	return b;
}

static inline void _evict_slab_list( slab_list_t *sl,
	void ( *dtor )( void *obj, void *ctag ),
	void *ctag
) {
	_purge_slab_chain( sl->free_list, dtor, ctag );
	sl->free_list = NULL;
}

extern void _purge_slab_chain( slab_t *sc,
	void ( *dtor )( void *obj, void *ctag ),
	void *ctag
);

extern void _free_slab_list( slab_list_t *sl,
	void ( *dtor )( void *obj, void *ctag ),
	void *ctag
);

static inline  counter_t *_get_counter_ptr( cache_t *cache, void *blk ) {
	// tricky, right? here, we find the address of reference counter
	// which is placed before sequential number which is placed at
	// the very end of block
	return ( counter_t* ) (
		( ( char* ) blk ) +
			(
				( cache->blk_sz - 1 - COUNTER_SIZE ) &
				( ~( COUNTER_ALIGN - 1 ) )
			)
	);
}

#ifdef __cplusplus
	}
#endif

#endif
