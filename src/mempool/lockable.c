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
	cache_t abstract_cache; /**< Cache header.*/
	slab_list_t slab_list; /**< Slab list.*/
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

static slab_list_t *_get_lockable_slab_list( cache_t *cache ) {
	slab_list_t *sl = &( ( ( lockable_cache_t* ) cache )->slab_list );
	
	if( sl->free_list == NULL )
		_prepopulate_list( cache, &( sl->free_list ), NULL );
	
	return sl;
}

static void _pool_lockable_evict( cache_t *c ) {
	// what would you do if the cache is freed already? this branch a way
	// to get an idea about this fact
	if( pthread_mutex_lock( &( cache->protect ) ) )
		return;

	_evict_slab_list( _get_simple_slab_list( c ),
		c->slab_class.dtor,
		c->slab_class.ctag
	);

	pthread_mutex_unlock( &( cache->protect ) );
}

static void _pool_lockable_destroy( cache_t *c ) {
	// what would you do if the cache is freed already? this branch a way
	// to get an idea about this fact
	if( pthread_mutex_lock( &( cache->protect ) ) )
		return;

	_free_slab_list( _get_simple_slab_list( c ),
		c->slab_class.dtor,
		c->slab_class.ctag
	);
	
	pthread_mutex_unlock( &( cache->protect ) );
	pthread_mutex_destroy( &( cache->protect ) );
}

static cache_class_t _G_lockable_cache = {
	.get_slab_list = _get_lockable_slab_list,
	.pool_evict = _pool_lockable_evict,
	.pool_destroy = _pool_lockable_destroy
};
