#include <atomic_ops.h>

typedef struct _thread_list_t {
	thread_hp_t *next;
} thread_list_t;

#define HAZARD_PTRS_K 3

enum hazard_type {
	HAZARD_PUSH_POP = 0,
};

typedef struct _thread_hp_t {
	thread_list_t header;
	thread_list_t *prev;
	hazard_list_t *list_header;
	slab_t *ptrs[ HAZARD_PTRS_K ];
} thread_hp_t;

typedef struct _hazard_list_t {
	pthread_key_t thread_hps;
	pthread_mutex_t write_guard;
	AO_t readers;
	thread_list_t hplist;
} hazard_list_t;

typedef struct {
	cache_t abstract_cache; /**< Cache header.*/
	slab_list_t slab_list; /**< Slab list.*/
	hazard_list_t hlist;
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

static inline int _hazard_ptr( slab_t *ptr,
	slab_t* hptrs[],
	enum hazard_type htype
) {
	if( hptrs[ htype ] != NULL )
		return 0;

	hptrs[ htype ] = ptr;
	return 1;
}

static inline void _unhazard_ptr( slab_t* hptrs[], enum hazard_type htype ) {
	hptrs[ htype ] = NULL;
}

static inline slab_t *_help_with_pop( slab_t* volatile *stack, slab_t *top ) {
	if( _is_being_poped( top ) ) {
		AO_compare_and_swap_full( stack,
			top,
			_get_regular_ptr( top )->next
		);

		return _get_regular_ptr( top )->next;
	}

	return top;
}

static slab_t *_pop_free_list( slab_t* volatile *stack,
	slab_t *hptrs[]
) {
	slab_t* volatile top;

	do {
		top = AO_load_full( stack );

		if( top == NULL )
			return NULL;

		assert(
			_hazard_ptr( _get_regular_ptr( top ), hptrs, HAZARD_PUSH_POP )
		);

		if( AO_load_full( stack ) != top ) {
			_unhazard_ptr( hptrs, HAZARD_PUSH_POP );
			continue;
		}

		if( _help_with_pop( stack, top ) != top )
			continue;

		if (
			! AO_compare_and_swap_full( stack,
				top,
				_get_reserved_ptr( top )
			)
		)
			continue;

		_help_with_pop( stack, AO_load_full( stack ) );
		break;
	} while( 1 );

	top->prev = NULL;
	top->next = NULL;
	AO_nop_full();

	return top;
}

static void _push_free_list( slab_t* volatile *stack,
	slab_t *what,
	slab_t *hptrs[]
) {
	slab_t* volatile top;

	do {
		top = AO_load_full( stack );
		_hazard_ptr( _get_regular_ptr( top ), hptrs, HAZARD_PUSH_POP );
		if( top != AO_load_full( stack ) ) {
			_unhazard_ptr( hptrs, HAZARD_PUSH_POP );
			continue;
		}
		
		top = _help_with_pop( stack, top );
		_unhazard_ptr( hptrs, HAZARD_PUSH_POP );
		AO_store_full( &( what->next ), top );
	} while( ! AO_compare_and_swap_full( stack, top, what ) );
}

void *pool_object_alloc( lockless_cache_t *cache ) {
	
	slab_t *slab = AO_load_full( &( cache->slab_list.partial_list ) );
	
	if( slab == NULL ) {
		slab = _pop_free_list( &( cache->slab_list.free_list ), hptrs );
	}
}

static slab_t *_get_hp_list( hazard_list_t *hlist ) {
	thread_hp_t *hp = pthread_getspecific( hlist->thread_hps );
	
	if( hp == NULL ) {
		hp = _create_hp_list( hlist );
		pthread_setspecific( hlist->thread_hps, hp );
	}

	return hp->ptrs;
}

static thread_hp_t *_create_hp_list( hazard_list_t *hlist ) {
	phlist = malloc( sizeof( thread_hp_t ) );
	memset( &( phlist->ptrs ), 0, sizeof( slab_t* ) * HAZARD_PTRS_K );
	phlist->list_header = hlist;
	phlist->prev = &( hlist->hplist );

	pthread_mutex_lock( hlist->write_guard );
	phlist->header.next = hlist->hplist.next;
	phlist->header.next->prev = phlist;
	hlist->hplist.next = phlist;
	pthread_mutex_unlock( hlist->write_guard );

	return phlist;
}

// RCU here
static void _free_hp_list( thread_hp_t *phlist ) {
	pthread_mutex_lock( phlist->list_header->write_guard );
	phlist->prev->next = phlist->header.next;
	phlist->header.next->prev = phlist->prev;
	pthread_mutex_unlock( phlist->list_header->write_guard );

	// spinning
	while( ! ( AO_load_full( &( phlist->list_header->readers ) ) == 0 ) )
		pthread_yield();

	free( phlist );
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
