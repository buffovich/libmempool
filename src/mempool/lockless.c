#include <atomic_ops.h>

typedef struct _thread_list_t {
	struct _thread_hp_t *next;
	struct _thread_hp_t *prev;
} thread_list_t;

#define HAZARD_PTRS_K 3

typedef struct _thread_hp_t {
	thread_list_t list_header;
	slab_t *ptrs[ HAZARD_PTRS_K ];
} thread_hp_t;

typedef struct {
	cache_t abstract_cache; /**< Cache header.*/
	slab_list_t slab_list; /**< Slab list.*/
	pthread_key_t thread_hps;
	thread_list_t hplist;
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

static inline void _populate_free_list( lockless_cache_t *cache ) {
	assert( cache != NULL );
	slab_list_t *sl = &( cache->slab_list );

	slab_t *newhead = NULL, *newtail = NULL;
	_prepopulate_list( cache, &newhead, &newtail );

	if( ! AO_compare_and_swap_full( &( sl->free_list ), NULL, newhead ) ) {
		slab_t *oldfl;
		do {
			oldfl = AO_load_full( &( sl->free_list ) );
			AO_store_full( &( newtail->next ), oldfl );
		} while(
			! AO_compare_and_swap_full( &( sl->free_list ), oldfl, newhead )
		)
	}
}

#define POP_FREE_LIST 1ul

static inline int _is_being_poped( slab_t *s ) {
	return ( ( ( AO_t ) s ) & POP_FREE_LIST );
}

static inline slab_t *_get_regular_ptr( slab_t* s ) {
	return ( ( ( AO_t ) s ) & ( ~POP_FREE_LIST ) );
}

static inline slab_t *_get_reserved_ptr( slab_t* s ) {
	return ( ( ( AO_t ) s ) | POP_FREE_LIST );
}

static inline slab_t *_help_with_pop( slab_t* volatile *stack, slab_t *top ) {
	if( _is_being_poped( top ) )
		AO_compare_and_swap_full( stack,
			top,
			_get_regular_ptr( top )->next
		);

		return _get_regular_ptr( top )->next;
	}

	return top;
}

static slab_t *pop( slab_t* volatile *stack ) {
	slab_t* volatile top;

	do {
		top = AO_load_full( stack );

		if( top == NULL )
			return NULL;

		_hazard_ptr( _get_regular_ptr( top ) );

		if( AO_load_full( stack ) != top ) {
			_unhazard_ptr( _get_regular_ptr( top ) );
			continue;
		}

		if( _help_with_pop( stack, top ) != top )
			continue;

		if ( ! AO_compare_and_swap_full( stack,
			top,
			_get_reserved_ptr( top )
		)
			continue;

		_help_with_pop( stack, AO_load_full( stack ) );
		break;
	} while( 1 );

	return top;
}

static void push( slab_t* volatile *stack, slab_t *what ) {
	slab_t* volatile top;

	do {
		top = _help_with_pop( stack, AO_load_full( stack ) );
		AO_store_full( &( what->next ), top );
	} while( ! AO_compare_and_swap_full( stack, top, what ) );
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
