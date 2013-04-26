#include <mempool.h>
#include <stdio.h>
#include <stdalign.h>
#include <assert.h>

int main( void ) {
	static slab_class_t sclass = {
		.blk_sz = 13,
		.align = 64,
		.ctag = NULL,
		.ctor = NULL,
		.dtor = NULL,
		.reinit = NULL
	};
	
	cache_t *c = pool_create( SLAB_REFERABLE, &sclass, 100 );
	void *p1 = pool_object_alloc( c );
	p1 = pool_object_get( c, p1 );
	assert( pool_object_put( c, p1 ) != NULL );
	assert( ( ( ( unsigned long ) p1 ) & ( ~ 63ul ) ) == ( ( unsigned long ) p1 ) );
	assert( pool_object_put( c, p1 ) == NULL );
	pool_free( c );
}
