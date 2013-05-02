#include <mempool.h>

#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <limits.h>

typedef unsigned int counter_t;

#define SLOTS_NUM ( sizeof( blockmap_t ) * 8 )

#define COUNTER_ALIGN ( alignof( counter_t ) )

#define COUNTER_SIZE ( sizeof( counter_t ) )

#define SLAB_ALIGNMENT ( ( sizeof( void* ) > alignof( slab_t ) ) ? \
	sizeof( void* ) : \
	alignof( slab_t ) \
)

#define EMPTY_MAP ( ~ ( 0u ) )

#define SLAB_LIST_TERMINATOR 0x80

/**
 * Simple cache.
 * Structure represents simple thread-unsafe single-arena cache. Instance
 * of this structure will be returned to you if you didn't mention any
 * thread-safe flags during cache creation with pool_create.
 * @see cache_t
 * @see slab_list_t
 * @see pool_create
 * @see pool_free
 */

static inline void *_bzero( size_t sz ) {
	void *b = malloc( sz );
	memset( c, 0, sz );
	return b;
}

/**
 * Simple cache class.
 * Obvious thread-unaware cache.
 * @see cache_t
 * @see slab_list_t
 */
typedef struct {
	cache_t abstract_cache; /**< Cache header.*/
	slab_list_t slab_list; /**< Slab list.*/
} simple_cache_t;

cache_t *pool_simple_create( unsigned int options,
	slab_class_t *slab_class,
	unsigned int inum
) {
	simple_cache_t *c = _bzero( sizeof( simple_cache_t ) );
	_pool_init( c, slab_class, &_G_simple_cache, options, inum );
	return c;
}

static slab_list_t *_get_simple_slab_list( cache_t *cache ) {
	return ( ( simple_cache_t * ) from )->slab_list;
}

static void _pool_simple_destroy( cache_t *c ) {
	_free_slab_list( &( ( ( simple_cache_t* ) c )->slab_list ) );
}

static cache_class_t _G_simple_cache = {
	.get_slab_list = _get_simple_slab_list,
	.pool_destroy = _pool_simple_destroy
};

#if LIBMEMPOOL_MULTITHREADED
	/**
	 * Locking cache.
	 * Cache which employs the simplest synchronization mechanism - global
	 * cache lock.
	 * @see cache_t
	 * @see slab_list_t
	 * @see pool_create
	 * @see pool_free
	 */
	typedef struct {
		simple_cache_t simple_cache; /**< Inherits structure from simple
										cache. */
		pthread_mutex_t protect; /**< Dummy simple global mutex.*/
	} lockable_cache_t;

	cache_t *pool_lockable_create( unsigned int options,
		slab_class_t *slab_class,
		unsigned int inum
	) {
		lockable_cache_t *c = _bzero( sizeof( lockable_cache_t ) );

		pthread_mutex_init( &( c->protect ), NULL );

		_pool_init( c, slab_class, &_G_lockable_cache, options, inum );
		return c;
	}

	void _pool_lockable_destroy( cache_t *c ) {
		// what would you do if the cache is freed already? this branch a way
		// to get an idea about tis fact
		if( pthread_mutex_lock( &( cache->protect ) ) )
			return;

		_pool_simple_destroy( c );
		
		pthread_mutex_unlock( &( cache->protect ) );
		pthread_mutex_destroy( &( cache->protect ) );
	}

	static cache_class_t _G_lockable_cache = {
		.get_slab_list = _get_simple_slab_list,
		.pool_destroy = _pool_lockable_destroy
	};

	/**
	 * Thread-local cache.
	 * Cache which employs therad local storage and thread-local slab lists
	 * (zones) for getting advantage of lockless allocation/getting/putting
	 * objects.
	 * @see cache_t
	 * @see slab_list_t
	 * @see pool_create
	 * @see pool_free
	 */
	typedef struct {
		cache_t abstract_cache; /**< Cache header.*/
		pthread_key_t thread_local; /**< The case if SLAB_THREAD_LOCAL_STORAGE
										has been specified. Key for extraction
										of pointer to thread-local allocation
										arena.*/
	} zoned_cache_t;

	cache_t *pool_zone_create( unsigned int options,
		slab_class_t *slab_class,
		unsigned int inum
	) {
		lockable_cache_t *c = _bzero( sizeof( zoned_cache_t ) );
		
		pthread_key_create( &( c->thread_local ), _free_slab_list );
		
		_pool_init( c, slab_class, &_G_zoned_cache, options, inum );
		return c;
	}

	static slab_list_t *_get_zoned_slab_list( cache_t *c ) {
		pthread_key_t key = ( ( ( zoned_cache_t* ) c )->thread_local );
		slab_list_t *sl = pthread_getspecific( key );
		
		if( sl == NULL ) {
			sl = malloc( sizeof( slab_list_t ) );
			memset( sl, 0, sizeof( slab_list_t ) );
			pthread_setspecific( key, sl );
		}

		return sl;
	}

	void _pool_zoned_destroy( cache_t *c ) {
		pthread_key_t key = ( ( zoned_cache_t* ) c )->thread_local;

		

		pthread_key_delete( key );
	}

	static cache_class_t _G_zoned_cache = {
		.get_slab_list = _get_zoned_slab_list,
		.pool_destroy = _pool_zoned_destroy
	};
#endif

static void _pool_init( cache_t *cache,
	slab_class_t *slab_class,
	cache_class_t *cache_class,
	unsigned int options,
	unsigned int inum
) {
	assert( slab_class->blk_sz > 0 );
	assert( !( options & ( ~SLAB_REFERABLE ) ) );
	assert( cache != NULL );
	assert( slab_class != NULL );
	assert( cache_class != NULL );

	// one byte for sequence number (used to calculate slab header position)
	cache->blk_sz = slab_class->blk_sz + 1;
	// COUNTER_SIZE bytes for number of references if relevant
	if( options & SLAB_REFERABLE )
		cache->blk_sz = _adjust_align( cache->blk_sz, COUNTER_ALIGN ) +
			COUNTER_SIZE;

	// adjust block size to match alignment
	cache->blk_sz = _adjust_align( cache->blk_sz, cache->align );

	cache->align = ( slab_class->align == 0 ) ? sizeof( void* ) :
		slab_class->align;

	cache->slab_class = *slab_class;
	cache->cache_class = *cache_class;

	// by default we should allocate block with exact total size
	// then we should consider alignment restrictions and add padding
	cache->header_sz = _adjust_align( sizeof( slab_t ), cache->align );

	if( inum > 0 )
		_prepopulate_list( inum, cache, cache_class->get_slab_list( c ) );
}

static void _prepopulate_list( unsigned int inum,
	cache_t *cache,
	slab_list_t *slist
) {
	assert( inum > 0 );
	assert( cache != NULL );
	assert( slist != NULL );

	// last bucket isn't full
	unsigned int last_bucket_sz = inum % SLOTS_NUM;
	int from = 0;
	if( last_bucket_sz )
		slist->head = _alloc_slab( cache, last_bucket_sz );
	else {
		from = 1;
		slist->head = _alloc_slab( cache, SLOTS_NUM );
	}

	slab_t *cur = slist->head;
	for( int cyc = from, nbuckets = inum / SLOTS_NUM;
		cyc < nbuckets;
		++cyc
	) {
		cur->next = _alloc_slab( cache, SLOTS_NUM );
		cur->next->prev = cur;
		cur = cur->next;
	}

	slist->tail = cur;
}

static inline slab_t *_alloc_slab( cache_t *cache, unsigned int nslots ) {
	assert( nslots <= SLOTS_NUM );
	assert( nslots > 0 );
	
	slab_t *ret = NULL;

	// TODO: handle errors
	assert(
		posix_memalign( ( void** ) &ret,
			SLAB_ALIGNMENT,
			cache->header_sz + cache->blk_sz * nslots
		) == 0
	);

	memset( ret, 0, sizeof( slab_t ) );
	ret->map = EMPTY_MAP;

	if( nslots < SLOTS_NUM )
		ret->map >>= ( SLOTS_NUM - nslots );

	// Let's fill sequential numbers. They are additional byte-length values
	// placed at the very end of slot.
	unsigned char *cur = ( ( unsigned char * ) ret ) +
		cache->header_sz + cache->blk_sz - 1;
	for( unsigned char cyc = 0;
		cyc < nslots;
		++cyc, cur += cache->blk_sz
	)
		*cur = cyc;
		
	// sequential number of the last allocated slot indicates that it was
	// the last slot; used during SLAB destruction to define how many times
	// object destructor should be invoked
	*( cur - cache->blk_sz ) |= SLAB_LIST_TERMINATOR;

	if( cache->slab_class.ctor != NULL ) {
		// invoke constructor for each object in SLAB if the case
		cur = ( ( unsigned char * ) ret ) + cache->header_sz;
		for( unsigned char cyc = 0; cyc < nslots; ++cyc, cur += cache->blk_sz )
			cache->slab_class.ctor( cur, cache->slab_class.ctag );
	}

	return ret;
}

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

static inline size_t _adjust_align( size_t blk_sz, unsigned int align ) {
	// slot size in this case should have such value that the next allocated
	// slot would begin according to the align alignment
	if( blk_sz & ( align - 1 ) ) {
		blk_sz &= ~( align - 1 );
		blk_sz += align;
	}

	return blk_sz;
}

static void _free_slab_list( slab_list_t *sl ) {
	slab_t *cur = sl->head, *next;
	while( cur != NULL ) {
		next = cur->next;
		_slab_free( cache, cur );
		cur = next;
	}
}

void pool_free( cache_t *cache ) {
	assert( cache != NULL );
	cache->cache_class.pool_destroy( cache );
	free( cache );
}

void _slab_free( cache_t *cache, slab_t *slab ) {
	if( cache->slab_class->dtor != NULL ) {
		// destroy all object if the case
		unsigned char *cur = ( ( unsigned char * ) slab ) + cache->header_sz;
		// this one is pointer to sequence number of the current slot
		unsigned char *nptr = cur + cache->blk_sz - 1;
		for( unsigned char cyc = 0;
			!( ( *nptr ) & SLAB_LIST_TERMINATOR );
			++cyc, cur += cache->blk_sz, nptr += cache->blk_sz
		)
			cache->slab_class->dtor( cur, cache->slab_class->ctag );

		// destroying the last one
		cache->slab_class->dtor( cur, cache->slab_class->ctag );
	}

	free( slab );
}

void pool_reap( cache_t *cache ) {
	#if LIBMEMPOOL_MULTITHREADED
		if( pthread_mutex_lock( &( cache->protect ) ) )
			return;
	#endif
	
	slab_t *cur = cache->head, *next;
	while( ( cur != NULL ) && ( cur->map ) )
		if( cur->map == EMPTY_MAP ) {
			// OK! SLAB has no allocated blocks. It's candidate for eviction.
			if( cur->prev != NULL )
				cur->prev->next = cur->next;
				
			if( cur->next != NULL )
				cur->next->prev = cur->prev;
				
			next = cur->next;
			_slab_free( cache, cur );
			cur = next;
		}

	#if LIBMEMPOOL_MULTITHREADED
		pthread_mutex_unlock( &( cache->protect ) );
	#endif
}

static inline void _reset_refcount( cache_t *cache, void *blk ) {
	*( _get_counter_ptr( cache, blk ) ) = 1;
}

static inline void _inc_refcount( cache_t *cache, void *blk ) {
	++( *( _get_counter_ptr( cache, blk ) ) );
}

static inline counter_t _dec_refcount( cache_t *cache, void *blk ) {
	assert( *( _get_counter_ptr( cache, blk ) ) > 0 );
	return --( *( _get_counter_ptr( cache, blk ) ) );
}

static inline void *_get_block( cache_t *c ) {
	assert( c->head->map );

	slab_t *s = c->head;
	// find the first bit set in the map; it will be sequence number
	// of the first unallocated slot
	int slotn = ffs( ( int ) s->map ) - 1;
	// set corresponding map bit to 1
	s->map &= ~( 1u << slotn );
	
	return ( ( ( char *) s ) + c->header_sz + ( c->blk_sz * slotn ) );
}

// mark object as allocated and increment reference number if the case
void *pool_object_alloc( cache_t *cache ) {
	assert( cache != NULL );

	#if LIBMEMPOOL_MULTITHREADED
		if( pthread_mutex_lock( &( cache->protect ) ) )
			return NULL;
	#endif

	// do we have allocated head? if not the n we should allocate it
	if( cache->head != NULL ) {
		// is head empty?
		if( ! cache->head->map ) {
			// if yes, we should elect new head with free slots
			// does the next SLAB exists?
			if( cache->head->next == NULL ) {
				// No. we have just one SLAB in cache.
				cache->head = _alloc_slab( cache, SLOTS_NUM );
				cache->head->next = cache->tail;
				cache->tail->prev = cache->head;
			} else {
				// OK. we got it.
				// does the next one have some free slots?
				if( cache->head->next->map ) {
					// YEP. just elect it as new head; former head goes to
					// the tail; thereby all full SLABs are accumulated at
					// the end of SLAB list
					slab_t *oldhead = cache->head,
						*oldtail = cache->tail,
						*newhead = oldhead->next;
					( oldtail->next = oldhead )->prev = oldtail;
					( cache->tail = oldhead )->next = NULL;
					( cache->head = newhead )->prev = NULL;
				} else {
					// NOPE. As far as separation of full SLABs and SLABs which
					// have free slots is being maintaining continuosly then
					// we won't have any chance to find something further; we
					// should allocate new SLAB chunk and elect it as new head.
					slab_t *news = _alloc_slab( cache, SLOTS_NUM );
					( news->next = cache->head )->prev = news;
					cache->head = news;
				}
			}
		}
	} else
		cache->head = cache->tail = _alloc_slab( cache, SLOTS_NUM );

	void *ret = _get_block( cache );
	if( cache->options & SLAB_REFERABLE )
		_reset_refcount( cache, ret );

	#if LIBMEMPOOL_MULTITHREADED
		pthread_mutex_unlock( &( cache->protect ) );
	#endif

	return ret;
}

// increment reference number if the case
void *pool_object_get( cache_t *cache, void *obj ) {
	assert( cache != NULL );
	assert( obj != NULL );

	#if LIBMEMPOOL_MULTITHREADED
		if( pthread_mutex_lock( &( cache->protect ) ) )
			return NULL;
	#endif
	
	if( cache->options & SLAB_REFERABLE )
		_inc_refcount( cache, obj );

	#if LIBMEMPOOL_MULTITHREADED
		pthread_mutex_unlock( &( cache->protect ) );
	#endif

	return obj;
}

// decrement reference number if the case; when number approaches zero then
// object will be marked as free
void *pool_object_put( cache_t *cache, void *obj ) {
	assert( cache != NULL );
	assert( obj != NULL );

	#if LIBMEMPOOL_MULTITHREADED
		if( pthread_mutex_lock( &( cache->protect ) ) )
			return NULL;
	#endif

	if( ( !( cache->options & SLAB_REFERABLE ) ) ||
		( !_dec_refcount( cache, obj ) )
	) {
		if( cache->slab_class->reinit != NULL )
			cache->slab_class->reinit( obj, cache->slab_class->ctag );

		// get the sequence number
		unsigned int pos = *( ( ( unsigned char* ) obj ) + cache->blk_sz - 1 );
		// rid off terminator bit
		pos &= ~ SLAB_LIST_TERMINATOR;

		// calculating slab header memory address
		slab_t *cur = ( slab_t* ) (
			( ( unsigned char* ) obj ) - cache->blk_sz * pos - cache->header_sz
		);

		// was the slab full?
		if( ( ! cur->map ) && ( cur != cache->head ) ) {
			// if slab was full and it wasn't the head then
			// we should elect it as new head to maintain separation
			// of full SLABs and not ful ones
			cur->prev->next = cur->next;

			if( cur->next != NULL )
				cur->next->prev = cur->prev;

			( cache->head->prev = cur )->prev = NULL;
			cur->next = cache->head;
			cache->head = cur;
		}

		// set corresponding bit in map
		cur->map |= 1u << pos;

		return NULL;
	}

	#if LIBMEMPOOL_MULTITHREADED
		pthread_mutex_unlock( &( cache->protect ) );
	#endif

	return obj;
}
