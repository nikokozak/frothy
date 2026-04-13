#include "froth_slot_table.h"
#include "froth_types.h"
#include <string.h>

froth_slot_t slot_table[FROTH_SLOT_TABLE_SIZE];
static froth_cell_u_t slot_pointer = 0;

static char index_has_slot_assigned(froth_cell_u_t index) {
  return slot_table[index].name != NULL;
}

froth_error_t froth_slot_find_name(const char *name,
                                   froth_cell_u_t *found_slot_index) {
  for (froth_cell_u_t ip = 0; ip < slot_pointer; ip++) {
    if (strcmp(slot_table[ip].name, name) == 0) {
      *found_slot_index = ip;
      return FROTH_OK;
    }
  }
  return FROTH_ERROR_UNDEFINED_WORD;
}

froth_error_t froth_slot_create(const char *name, froth_heap_t *heap,
                                froth_cell_u_t *created_slot_index) {
  if (slot_pointer >= FROTH_SLOT_TABLE_SIZE) {
    return FROTH_ERROR_SLOT_TABLE_FULL;
  }

  froth_cell_u_t name_heap_location;

  if (froth_heap_allocate_bytes(strlen(name) + 1, heap, &name_heap_location) ==
      FROTH_ERROR_HEAP_OUT_OF_MEMORY) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  char *name_in_heap = (char *)(heap->data + name_heap_location);
  strcpy(name_in_heap, name);

  *created_slot_index = slot_pointer;
  slot_table[slot_pointer++] = (froth_slot_t){.name = name_in_heap,
                                              .impl = 0,
                                              .prim = NULL,
                                              .overlay = 0,
                                              .impl_bound = 0,
                                              .in_arity = FROTH_SLOT_ARITY_UNKNOWN,
                                              .out_arity = FROTH_SLOT_ARITY_UNKNOWN};

  return FROTH_OK;
}

froth_error_t froth_slot_find_name_or_create(froth_heap_t *froth_heap,
                                             const char *name,
                                             froth_cell_u_t *slot_index) {

  if (froth_slot_find_name(name, slot_index) == FROTH_ERROR_UNDEFINED_WORD) {
    FROTH_TRY(froth_slot_create(name, froth_heap, slot_index));
    return FROTH_OK;
  } else {
    return FROTH_OK;
  }
}

froth_error_t froth_slot_get_impl(froth_cell_u_t slot_index,
                                  froth_cell_t *impl) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }
  if (slot_table[slot_index].impl_bound == 0) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }
  *impl = slot_table[slot_index].impl;
  return FROTH_OK;
}

froth_error_t froth_slot_get_prim(froth_cell_u_t slot_index,
                                  froth_native_word_t *prim) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }
  *prim = slot_table[slot_index].prim;
  if (*prim == NULL) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }
  return FROTH_OK;
}

froth_error_t froth_slot_set_impl(froth_cell_u_t slot_index,
                                  froth_cell_t impl) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }
  slot_table[slot_index].impl = impl;
  slot_table[slot_index].impl_bound = 1;
  return FROTH_OK;
}
froth_error_t froth_slot_set_prim(froth_cell_u_t slot_index,
                                  froth_native_word_t prim) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }
  slot_table[slot_index].prim = prim;
  return FROTH_OK;
}

froth_error_t froth_slot_clear_binding(froth_cell_u_t slot_index) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }

  slot_table[slot_index].impl = 0;
  slot_table[slot_index].prim = NULL;
  slot_table[slot_index].impl_bound = 0;
  slot_table[slot_index].in_arity = FROTH_SLOT_ARITY_UNKNOWN;
  slot_table[slot_index].out_arity = FROTH_SLOT_ARITY_UNKNOWN;
  return FROTH_OK;
}

froth_error_t froth_slot_get_name(froth_cell_u_t slot_index,
                                  const char **name) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }
  *name = slot_table[slot_index].name;
  return FROTH_OK;
}

froth_cell_u_t froth_slot_count(void) { return slot_pointer; }

froth_error_t froth_slot_set_overlay(froth_cell_u_t slot_index,
                                     uint8_t overlay) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }
  slot_table[slot_index].overlay = overlay;
  return FROTH_OK;
}

froth_error_t froth_slot_get_arity(froth_cell_u_t slot_index, uint8_t *in_arity,
                                   uint8_t *out_arity) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }
  *in_arity = slot_table[slot_index].in_arity;
  *out_arity = slot_table[slot_index].out_arity;
  return FROTH_OK;
}

froth_error_t froth_slot_set_arity(froth_cell_u_t slot_index, uint8_t in_arity,
                                   uint8_t out_arity) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }

  if (in_arity == FROTH_SLOT_ARITY_UNKNOWN ||
      out_arity == FROTH_SLOT_ARITY_UNKNOWN) {
    return FROTH_ERROR_BOUNDS;
  }

  slot_table[slot_index].in_arity = in_arity;
  slot_table[slot_index].out_arity = out_arity;

  return FROTH_OK;
}

froth_error_t froth_slot_clear_arity(froth_cell_u_t slot_index) {
  if (!index_has_slot_assigned(slot_index)) {
    return FROTH_ERROR_UNDEFINED_WORD;
  }

  slot_table[slot_index].in_arity = FROTH_SLOT_ARITY_UNKNOWN;
  slot_table[slot_index].out_arity = FROTH_SLOT_ARITY_UNKNOWN;

  return FROTH_OK;
}

bool froth_slot_is_overlay(froth_cell_u_t slot_index) {
  if (!index_has_slot_assigned(slot_index)) {
    return false;
  }
  return slot_table[slot_index].overlay != 0;
}

froth_error_t froth_slot_reset_overlay(void) {
  froth_cell_u_t new_pointer = slot_pointer;
  for (froth_cell_u_t i = 0; i < slot_pointer; i++) {
    if (slot_table[i].overlay) {
      if (i < new_pointer)
        new_pointer = i;
      slot_table[i].name = NULL;
      slot_table[i].impl = 0;
      slot_table[i].prim = NULL;
      slot_table[i].overlay = 0;
      slot_table[i].impl_bound = 0;
      slot_table[i].in_arity = FROTH_SLOT_ARITY_UNKNOWN;
      slot_table[i].out_arity = FROTH_SLOT_ARITY_UNKNOWN;
    }
  }
  slot_pointer = new_pointer;
  return FROTH_OK;
}
