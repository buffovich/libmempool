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

static inline slab_t *_alloc_slab( cache_t *cache, unsigned int nslots ) {
	assert( nslots <= SLOTS_NUM );
	
	// allocating slab with SLOTS_NUM slots
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

	if( cache->slab_class->ctor != NULL ) {
		// invoke constructor for each object in SLAB if the case
		cur = ( ( unsigned char * ) ret ) + cache->header_sz;
		for( unsigned char cyc = 0; cyc < nslots; ++cyc, cur += cache->blk_sz )
			cache->slab_class->ctor( cur, cache->slab_class->ctag );
	}

	return ret;
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

cache_t *pool_create( unsigned int options,
	slab_class_t *slab_class,
	unsigned int inum
) {
	assert( slab_class->blk_sz > 0 );
	
	// TODO: think about alternative schemes of allocation
	cache_t *c = malloc( sizeof( cache_t ) );
	memset( c, 0, sizeof( cache_t ) );

	c->options = options;

	// one byte for sequence number (used to calculate slab header position)
	c->blk_sz = slab_class->blk_sz + 1;
	// COUNTER_SIZE bytes for number of references if relevant
	if( options & SLAB_REFERABLE )
		c->blk_sz = _adjust_align( c->blk_sz, COUNTER_ALIGN ) + COUNTER_SIZE;

	// adjust block size to match alignment
	c->blk_sz = _adjust_align( c->blk_sz, c->align );

	c->align = ( slab_class->align == 0 ) ? sizeof( void* ) :
		slab_class->align;

	c->slab_class = slab_class;

	// by default we should allocate block with exact total size
	// then we should consider alignment restrictions and add padding
	c->header_sz = _adjust_align( sizeof( slab_t ), c->align );

	if( inum > 0 ) {
		// last bucket isn't full
		unsigned int last_bucket_sz = inum % SLOTS_NUM;
		int from = 0;
		if( last_bucket_sz )
			c->head = _alloc_slab( c, last_bucket_sz );
		else {
			from = 1;
			c->head = _alloc_slab( c, SLOTS_NUM );
		}

		slab_t *cur = c->head;
		for( int cyc = from, nbuckets = inum / SLOTS_NUM;
			cyc < nbuckets;
			++cyc
		) {
			cur->next = _alloc_slab( c, SLOTS_NUM );
			cur->next->prev = cur;
			cur = cur->next;
		}

		c->tail = cur;
	}

	#if LIBMEMPOOL_MULTITHREADED
		pthread_mutex_init( &( c->protect ), NULL );
	#endif

	return c;
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

void pool_free( cache_t *cache ) {
	assert( cache != NULL );
	
	#if LIBMEMPOOL_MULTITHREADED
		// what would you do if the cache is freed already? this branch a way
		// to get an idea about tis fact
		if( pthread_mutex_lock( &( cache->protect ) ) )
			return;
	#endif

	slab_t *cur = cache->head, *next;
	while( cur != NULL ) {
		next = cur->next;
		_slab_free( cache, cur );
		cur = next;
	}

	#if LIBMEMPOOL_MULTITHREADED
		pthread_mutex_unlock( &( cache->protect ) );
		pthread_mutex_destroy( &( cache->protect ) );
	#endif
	
	free( cache );
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
