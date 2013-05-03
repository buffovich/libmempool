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
	
	pthread_key_create( &( c->thread_local ), _free_zone );
	_pool_init( c, slab_class, &_G_zoned_cache, options, inum );
	
	return c;
}

typedef struct {
	slab_list_t slab_list;
	void ( *dtor )( void *obj, void *ctag );
	void *ctag
} zoned_slab_list_t;

static void _free_zone( zoned_slab_list_t *z ) {
	_free_slab_list( &( z->slab_list ), z->dtor, z->ctag );
	free( z );
}

static slab_list_t *_get_zoned_slab_list( cache_t *c ) {
	pthread_key_t key = ( ( ( zoned_cache_t* ) c )->thread_local );
	zoned_slab_list_t *lsl = pthread_getspecific( key );
	
	if( lsl == NULL ) {
		lsl = malloc( sizeof( zoned_slab_list_t ) );
		memset( &( lsl->slab_list ), 0, sizeof( slab_list_t ) );
		lsl->dtor = c->slab_class.dtor;
		lsl->ctag = c->slab_class.ctag;
		pthread_setspecific( key, lsl );
	}

	if( lsl->slab_list.free_list == NULL )
		_prepopulate_list( cache, &( lsl->slab_list.free_list ), NULL );

	return &( lsl->slab_list );
}

static void _pool_zoned_destroy( cache_t *c ) { }

static void _pool_zoned_evict( cache_t *c ) {
	_evict_slab_list( _get_zoned_slab_list( c ),
		c->slab_class.dtor,
		c->slab_class.ctag
	);
}

static cache_class_t _G_zoned_cache = {
	.get_slab_list = _get_zoned_slab_list,
	.pool_destroy = _pool_zoned_destroy,
	.pool_evict = _pool_zoned_evict
};
