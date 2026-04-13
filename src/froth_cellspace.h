#pragma once
#include "froth_types.h"

typedef struct froth_cellspace_t {
  froth_cell_t *data;
  froth_cell_t *base_seed;
  froth_cell_u_t used;
  froth_cell_u_t capacity;
  froth_cell_u_t base_mark;
} froth_cellspace_t;

void froth_cellspace_init(froth_cellspace_t *cellspace);
froth_error_t froth_cellspace_allot(froth_cellspace_t *cellspace,
                                    froth_cell_t count,
                                    froth_cell_t *base_addr_out);
froth_error_t froth_cellspace_fetch(const froth_cellspace_t *cellspace,
                                    froth_cell_t index, froth_cell_t *result);
froth_error_t froth_cellspace_store(froth_cellspace_t *cellspace,
                                    froth_cell_t index, froth_cell_t value);
void froth_cellspace_capture_base_seed(froth_cellspace_t *cellspace);
void froth_cellspace_reset_to_base(froth_cellspace_t *cellspace);
