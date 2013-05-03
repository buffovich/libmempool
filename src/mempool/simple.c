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
	slab_list_t *sl = &( ( ( simple_cache_t* ) cache )->slab_list );
	
	if( sl->free_list == NULL )
		_prepopulate_list( cache, &( sl->free_list ), NULL );
	
	return sl;
}

static void _pool_simple_evict( cache_t *c ) {
	_evict_slab_list( _get_simple_slab_list( cache ),
		c->slab_class.dtor,
		c->slab_class.ctag
	);
}

static void _pool_simple_destroy( cache_t *c ) {
	_free_slab_list(
		_get_simple_slab_list( c ),
		c->slab_class.dtor,
		c->slab_class.ctag
	);
}

static cache_class_t _G_simple_cache = {
	.get_slab_list = _get_simple_slab_list,
	.pool_destroy = _pool_simple_destroy,
	.pool_evict = _pool_simple_evict
};
