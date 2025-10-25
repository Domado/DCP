#include "dcp_allocator.h"
#include <stdlib.h>

static void* dcp_default_malloc(size_t size) {
    return malloc(size);
}
static void dcp_default_free(void *ptr) {
    free(ptr);
}

static dcp_malloc_fn g_malloc_fn = dcp_default_malloc;
static dcp_free_fn g_free_fn = dcp_default_free;


void dcp_set_allocator(dcp_malloc_fn malloc_fn, dcp_free_fn free_fn) {
    g_malloc_fn = malloc_fn ? malloc_fn : dcp_default_malloc;
    g_free_fn = free_fn ? free_fn : dcp_default_free;
}

dcp_malloc_fn dcp_get_malloc(void) {
    return g_malloc_fn;
}

dcp_free_fn dcp_get_free(void) {
    return g_free_fn;
}
