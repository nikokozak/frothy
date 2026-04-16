#pragma once

#include "froth_heap.h"
#include "froth_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifndef FROTH_SLOT_TABLE_SIZE
#error                                                                         \
    "FROTH_SLOT_TABLE_SIZE is not defined. Please define it to the desired size of the slot table."
#endif

#define FROTH_SLOT_ARITY_UNKNOWN FROTH_ARITY_UNKNOWN

typedef struct {
  const char *name;
  froth_cell_t impl; // Current Froth value binding; valid when impl_bound != 0
  froth_native_word_t prim;
  uint8_t overlay;
  uint8_t impl_bound;
  uint8_t in_arity; // FROTH_SLOT_ARITY_UNKNOWN means "unknown"
  uint8_t out_arity;
} froth_slot_t;

// find_name should return an erorr if not found, otherwise write to the result
// pointer.
froth_error_t froth_slot_find_name_or_create(froth_heap_t *froth_heap,
                                             const char *name,
                                             froth_cell_u_t *slot_index);
froth_error_t froth_slot_find_name_or_create_n(froth_heap_t *froth_heap,
                                               const char *name, size_t length,
                                               froth_cell_u_t *slot_index);
froth_error_t froth_slot_find_name(const char *name,
                                   froth_cell_u_t *found_slot_index);
froth_error_t froth_slot_find_name_n(const char *name, size_t length,
                                     froth_cell_u_t *found_slot_index);
froth_error_t froth_slot_create(const char *name, froth_heap_t *froth_heap,
                                froth_cell_u_t *created_slot_index);
froth_error_t froth_slot_create_n(const char *name, size_t length,
                                  froth_heap_t *froth_heap,
                                  froth_cell_u_t *created_slot_index);
froth_error_t froth_slot_get_impl(froth_cell_u_t slot_index,
                                  froth_cell_t *impl);
froth_error_t froth_slot_get_prim(froth_cell_u_t slot_index,
                                  froth_native_word_t *prim);
froth_error_t froth_slot_set_impl(froth_cell_u_t slot_index, froth_cell_t impl);
froth_error_t froth_slot_set_prim(froth_cell_u_t slot_index,
                                  froth_native_word_t prim);
froth_error_t froth_slot_clear_binding(froth_cell_u_t slot_index);
froth_error_t froth_slot_get_name(froth_cell_u_t slot_index, const char **name);
froth_error_t froth_slot_set_overlay(froth_cell_u_t slot_index,
                                     uint8_t overlay);
froth_error_t froth_slot_get_arity(froth_cell_u_t slot_index, uint8_t *in_arity,
                                   uint8_t *out_arity);
froth_error_t froth_slot_set_arity(froth_cell_u_t slot_index, uint8_t in_arity,
                                   uint8_t out_arity);
froth_error_t froth_slot_clear_arity(froth_cell_u_t slot_index);
froth_cell_u_t froth_slot_count(void);
froth_cell_u_t froth_slot_high_water(void);
void froth_slot_debug_reset_high_water(void);
bool froth_slot_is_overlay(froth_cell_u_t slot_index);
froth_error_t froth_slot_reset_overlay(void);
