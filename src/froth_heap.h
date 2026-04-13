#pragma once
#include "froth_types.h"

#ifndef FROTH_HEAP_SIZE
  #error "FROTH_HEAP_SIZE is not defined. Please define it to the desired size of the heap in bytes."
#endif

typedef struct {
  uint8_t *data;
  froth_cell_u_t pointer;
} froth_heap_t;


/* Return a froth_cell_t pointer into the heap at the given byte offset.
 * Useful when you have a raw byte offset (e.g. from a tagged QuoteRef payload)
 * and need to read cell-sized data. */
static inline froth_cell_t* froth_heap_cell_ptr(froth_heap_t* heap, froth_cell_u_t byte_offset) {
  return (froth_cell_t*)&heap->data[byte_offset];
}

froth_error_t froth_heap_allocate_bytes(froth_cell_u_t size, froth_heap_t* froth_heap, froth_cell_u_t* assigned_heap_location);

/* Allocate space for `count` cells on the heap. Returns a typed pointer
 * to the first cell. The byte offset (needed for QuoteRef payloads) is
 * written to `byte_offset_out` if non-NULL. */
froth_error_t froth_heap_allocate_cells(froth_cell_u_t count, froth_heap_t* froth_heap, froth_cell_t** cells_out, froth_cell_u_t* byte_offset_out);
