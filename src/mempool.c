#include <pcfgm/internal/alloc.h>

#include <stdlib.h>
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

static inline slab_t *__alloc_slab( cache_t *cache, unsigned int nslots ) {
	assert( nslots <= SLOTS_NUM );
	
	// allocating slab with SLOTS_NUM slots
	slab_t **ret;

	posix_memalign( ret,
		SLAB_ALIGNMENT,
		cache->header_sz + cache->blk_sz * nslots
	);

	memset( *ret, 0, sizeof( slab_t ) );
	( *ret )->map = EMPTY_MAP;

	if( nslots < SLOTS_NUM )
		( *ret )->map >>= ( SLOTS_NUM - nslots );

	unsigned char *cur = *ret + cache->header_sz + cache->blk_sz - 1;
	for( unsigned char cyc = 0; cur < nslots; ++cyc, cur += cache->blk_sz )
		*cur = cyc;

	return *ret;
}

static inline size_t __adjust_align( size_t blk_sz, unsigned int align ) {
	if( blk_sz & ( align - 1 ) ) {
		blk_sz &= ~( align - 1 );
		blk_sz += align;
	}

	return blk_sz;
}

cache_t *_cfg_cache_create( unsigned int options,
	size_t blk_sz,
	unsigned int align,
	unsigned int inum
) {
	assert( blk_sz > 0 );
	
	// TODO: think about alternative schemes of allocation
	cache_t *c = calloc( 1, sizeof( cache_t ) );

	c->options = options;

	if( align == 0 )
		align = sizeof( void* );

	// one byte for sequence number (used to calculate slab header position)
	blk_sz += 1;
	// COUNTER_SIZE bytes for number of references if relevant
	if( options & SLAB_REFERABLE )
		blk_sz = __adjust_align( blk_sz, COUNTER_ALIGN ) + COUNTER_SIZE;

	// adjust block size to match alignment
	blk_sz = __adjust_align( blk_sz, align );

	c->blk_sz = blk_sz;
	c->align = align;

	// by default we should allocate block with exact total size
	// then we should consider alignment restrictions and add padding
	c->header_sz = __adjust_align( sizeof( slab_t ), align );

	if( inum > 0 ) {
		// last bucket isn't full
		unsigned int last_bucket_sz = inum % SLOTS_NUM;
		int from = 0;
		if( last_bucket_sz )
			c->head = __alloc_slab( c, last_bucket_sz );
		else {
			from = 1;
			c->head = __alloc_slab( c, SLOTS_NUM );
		}

		slab_t *cur = c->head;
		for( int cyc = from, nbuckets = inum / SLOTS_NUM;
			cyc < nbuckets;
			++cyc
		) {
			cur->next = __alloc_slab( c, SLOTS_NUM );
			cur->next->prev = cur;
			cur = cur->next;
		}

		c->tail = cur;
	}

	return c;
}

void _cfg_cache_free( cache_t *cache ) {
	slab_t *cur = cache->head, *next;
	while( cur != NULL ) {
		next = cur->next;
		free( cur );
		cur = next;
	}

	free( cache );
}

void _cfg_cache_reap( cache_t *cache ) {
	slab_t *cur = cache->head, *next;
	while( ( cur != NULL ) && ( cur->map ) )
		if( cur->map == EMPTY_MAP ) {
			if( cur->prev != NULL )
				cur->prev->next = cur->next;
				
			if( cur->next != NULL )
				cur->next->prev = cur->prev;
				
			next = cur->next;
			free( cur );
			cur = next;
		}
}

// tricky, right? here, we find the address of reference counter
// which is placed at the very end of block
#define COUNTER_LVALUE( cache, blk ) *( ( counter_t* ) \
	( ( ( char* ) ( blk ) ) + \
		( ( ( cache )->blk_sz - 1 - COUNTER_SIZE ) & \
			( ~( COUNTER_ALIGN - 1 ) ) \
		) \
	) \
)

static inline void __reset_refcount( cache_t *cache, void *blk ) {
	COUNTER_LVALUE( cache, blk ) = 1;
}

static inline void __inc_refcount( cache_t *cache, void *blk ) {
	++COUNTER_LVALUE( cache, blk );
}

static inline counter_t __dec_refcount( cache_t *cache, void *blk ) {
	assert( COUNTER_LVALUE( cache, blk ) > 0 );
	return --COUNTER_LVALUE( cache, blk );
}

static inline void *__get_block( cache_t *c ) {
	assert( c->head->map );

	int slotn = ffs( ( int ) cache->head->map ) - 1;
	s->map &= ~( 1u << slotn );
	
	return ( ( ( char *) s ) + c->header_sz + ( c->blk_sz * slotn ) );
}

// mark object as allocated and increment reference number if the case
void *_cfg_object_alloc( cache_t *cache ) {
	assert( cache != NULL );
	
	if( cache->head != NULL ) {
		if( ! cache->head->map ) {
			if( cache->head->next == NULL ) {
				cache->head = __alloc_slab( cache, SLOTS_NUM );
				cache->head->next = cache->tail;
				cache->tail->prev = cache->head;
			} else {
				if( cache->head->next->map ) {
					slab_t *oldhead = cache->head,
						*oldtail = cache->tail,
						*newhead = oldhead->next;
					( oldtail->next = oldhead )->prev = oldtail;
					( cache->tail = oldhead )->next = NULL;
					( cache->head = newhead )->prev = NULL;
				} else {
					slab_t *news = __alloc_slab( cache, SLOTS_NUM );
					( news->next = cache->head )->prev = news;
					cache->head = news;
				}
			}
		}
	} else
		cache->head = cache->tail = __alloc_slab( cache, SLOTS_NUM );

	void *ret = __get_block( cache );
	if( cache->options & SLAB_REFERABLE )
		__reset_refcount( cache, ret );

	return NULL;
}

// increment reference number if the case
void *_cfg_object_get( cache_t *cache, void *obj ) {
	assert( cache != NULL );
	
	if( cache->options & SLAB_REFERABLE )
		__inc_refcount( cache, obj );

	return obj;
}

// decrement reference number if the case; when number approaches zero then
// object will be marked as free
void *_cfg_object_put( cache_t *cache, void *obj ) {
	assert( cache != NULL );

	if( ( !( cache->options & SLAB_REFERABLE ) ) ||
		( !__dec_refcount( cache, obj ) )
	) {
		unsigned int pos = ( ( ( unsigned char* ) obj ) + cache->blk_sz - 1 );
		slab_t *cur = ( ( unsigned char* ) obj ) - cache->blk_sz * pos -
			cache->header_sz;

		if( ( ! cur->map ) && ( cur != cache->head ) ) {
			cur->prev->next = cur->next;

			if( cur->next != NULL )
				cur->next->prev = cur->prev;

			( cache->head->prev = cur )->prev = NULL;
			cur->next = cache->head;
			cache->head = cur;
		}

		cur->map |= 1u << pos;
	}

	return obj;
}
