#include <atomic_ops.h>

typedef struct {
	cache_t abstract_cache; /**< Cache header.*/
	slab_list_t slab_list; /**< Slab list.*/
	AO_t modify;
} lockless_cache_t;

cache_t *pool_lockless_create( unsigned int options,
	slab_class_t *slab_class,
	unsigned int inum
) {
	/* we will play with pointers as atomic values
	 * ptrs properties must match atomic word properties
	 */
	assert(
		( alignof( AO_t ) == alignof( void* ) ) &&
		( sizeof( AO_t ) == sizeof( void* ) )
	);

	simple_cache_t *c = _bzero( sizeof( simple_cache_t ) );

	_pool_init( c, slab_class, &_G_lockless_cache, options, inum );
	return c;
}

static slab_list_t *_get_lockless_slab_list( cache_t *cache ) {
	slab_list_t *sl = &( ( ( lockless_cache_t* ) cache )->slab_list );
	return sl;
}

static inline slab_t *_get_free_list(){
	if( AO_load_full( &( sl->free_list ) ) == NULL ) {
		slab_t *newhead = NULL, *newtail = NULL;
		_prepopulate_list( cache, &newfl, &newtail );
		
		slab_t *oldfl;
		do {
			do {
				oldfl = AO_load_full( &( sl->free_list ) );
			} while(
				! AO_compare_and_swap_full( &( sl->free_list ), oldfl, NULL )
			);

			AO_store_full( &( newtail->next ), oldfl );
			AO_store_full( &( oldfl->prev ), newtail );
			for( ; newtail->next != NULL; newtail = newtail->next ) ;
		} while(
			! AO_compare_and_swap_full( &( sl->free_list ), NULL, newhead )
		)
	}

	return AO_load_full( &( sl->free_list ) );
}

static void _pool_lockless_evict( cache_t *c ) {
	slab_list_t *sl = _get_simple_slab_list( c );

	slab_t *flist = NULL;
	do {
		flist = AO_load_full( &( sl->free_list ) );
		if( flist == NULL )
			return;
	} while(
		! AO_compare_and_swap_full( &( sl->free_list ), flist, NULL )
	);
	
	_purge_slab_chain( flist, c->slab_class.dtor, c->slab_class.ctag );
}

static cache_class_t _G_lockless_cache = {
	.get_slab_list = _get_lockless_slab_list,
	.pool_destroy = _pool_lockless_destroy,
	.pool_evict = _pool_lockless_evict
};
