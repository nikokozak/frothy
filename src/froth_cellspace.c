#include "froth_cellspace.h"
#include <string.h>

void froth_cellspace_init(froth_cellspace_t *cellspace) {
  cellspace->used = 0;
  cellspace->base_mark = 0;
  memset(cellspace->data, 0, sizeof(froth_cell_t) * cellspace->capacity);
  memset(cellspace->base_seed, 0,
         sizeof(froth_cell_t) * cellspace->capacity);
}

froth_error_t froth_cellspace_allot(froth_cellspace_t *cellspace,
                                    froth_cell_t count,
                                    froth_cell_t *base_addr_out) {
  if (count < 0) {
    return FROTH_ERROR_BOUNDS;
  }

  if (cellspace->used + count > cellspace->capacity) {
    return FROTH_ERROR_CELLSPACE_FULL;
  }

  *base_addr_out = cellspace->used;
  memset(cellspace->data + cellspace->used, 0, sizeof(froth_cell_t) * count);
  cellspace->used += count;

  return FROTH_OK;
};

froth_error_t froth_cellspace_fetch(const froth_cellspace_t *cellspace,
                                    froth_cell_t index, froth_cell_t *result) {
  if (index < 0 || index >= cellspace->used) {
    return FROTH_ERROR_BOUNDS;
  }

  *result = cellspace->data[index];

  return FROTH_OK;
}

froth_error_t froth_cellspace_store(froth_cellspace_t *cellspace,
                                    froth_cell_t index, froth_cell_t value) {
  if (index < 0 || index >= cellspace->used) {
    return FROTH_ERROR_BOUNDS;
  }

  cellspace->data[index] = value;

  return FROTH_OK;
}

void froth_cellspace_capture_base_seed(froth_cellspace_t *cellspace) {
  memcpy(cellspace->base_seed, cellspace->data,
         sizeof(froth_cell_t) * cellspace->used);

  cellspace->base_mark = cellspace->used;
}

void froth_cellspace_reset_to_base(froth_cellspace_t *cellspace) {
  memcpy(cellspace->data, cellspace->base_seed,
         sizeof(froth_cell_t) * cellspace->base_mark);

  cellspace->used = cellspace->base_mark;
}
