#include <mepool/common.h>

#include <mempool.h>

#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <limits.h>

#define SLOTS_NUM ( sizeof( unsigned int ) * 8 )

#define SLAB_ALIGNMENT ( ( sizeof( void* ) > alignof( slab_t ) ) ? \
	sizeof( void* ) : \
	alignof( slab_t ) \
)

#define EMPTY_MAP ( ~ ( 0u ) )

#define SLAB_LIST_TERMINATOR 0x80

void _purge_slab_chain( slab_t *sc,
	void ( *dtor )( void *obj, void *ctag ),
	void *ctag
) {
	slab_t *next = NULL;
	while( sc != NULL ) {
		next = sc->next;
		_free_slab( sc, dtor, ctag );
		sc = next;
	}
}

void _free_slab_list( slab_list_t *sl,
	void ( *dtor )( void *obj, void *ctag ),
	void *ctag
) {
	_purge_slab_chain( sl->free_list, c->slab_class.dtor, c->slab_class.ctag );
	_purge_slab_chain( sl->partial_list,
		c->slab_class.dtor,
		c->slab_class.ctag
	);
	_purge_slab_chain( sl->full_list, c->slab_class.dtor, c->slab_class.ctag );
	memset( sl, 0, sizeof( sl ) );
}
