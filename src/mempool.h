#ifndef PCFGM_INTERNAL_ALLOC
#define PCFGM_INTERNAL_ALLOC

typedef unsigned int blockmap_t;

typedef struct {
	slab_t *next;
	slab_t *prev;
	// bitmap of free and occupied blocks
	blockmap_t map;
} slab_t;

#defined SLAB_REFERABLE 1

typedef struct {
	unsigned int options;
	// applied alignment in data block
	size_t align;
	// element size
	size_t blk_sz;
	// size of header with accounted padding related to requested alignment
	size_t header_sz;
	slab_t *head;
	slab_t *tail;
} cache_t;

extern cache_t *_cfg_cache_create( unsigned int options,
	size_t blk_sz,
	unsigned int align,
	unsigned int inum
);

extern void _cfg_cache_free( cache_t *cache );

extern void _cfg_cache_reap( cache_t *cache );

// mark object as allocated and increment reference number if the case
extern void *_cfg_object_alloc( cache_t *cache );

// increment reference number if the case
extern void *_cfg_object_get( cache_t *cache, void *obj );

// decrement reference number if the case; when number approaches zero then
// object will be marked as free
extern void *_cfg_object_put( cache_t *cache, void *obj );

#endif
