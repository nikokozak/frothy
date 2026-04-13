#include "froth_tbuf.h"
#include "froth_heap.h"
#include "froth_vm.h"
#include <string.h>

void froth_tbuf_init(froth_vm_t *vm) {
  memset(&vm->tbuf, 0, sizeof(froth_tbuf_t));
  vm->tbuf.generation = 1;
}

/* Ring layout per string: [len_lo] [len_hi] [bytes...] [\0] */
#define TBUF_OVERHEAD 3 /* 2-byte length header + 1-byte null terminator */

static bool ranges_overlap(uint16_t a_start, uint16_t a_end,
                           uint16_t b_start, uint16_t b_end) {
  return a_start < b_end && b_start < a_end;
}

static void reclaim_overlapping(froth_tbuf_t *tbuf, uint16_t write_start,
                                uint16_t write_end) {
  for (int i = 0; i < FROTH_TDESC_MAX; i++) {
    froth_tdesc_t *d = &tbuf->descriptors[i];
    if (d->kind == FROTH_TDESC_FREE)
      continue;
    uint16_t desc_start = d->ring_offset - 2;
    uint16_t desc_end = d->ring_offset + d->len + 1;
    if (ranges_overlap(write_start, write_end, desc_start, desc_end))
      d->kind = FROTH_TDESC_FREE;
  }
}

static int claim_free_descriptor(froth_tbuf_t *tbuf) {
  for (int i = 0; i < FROTH_TDESC_MAX; i++) {
    if (tbuf->descriptors[i].kind == FROTH_TDESC_FREE)
      return i;
  }
  return -1;
}

static bool is_stale(froth_tdesc_t *desc, froth_cell_u_t cell_gen) {
  return desc->kind == FROTH_TDESC_FREE ||
         (desc->generation & FROTH_BSTRING_GEN_MASK) != cell_gen;
}

froth_error_t froth_tbuf_alloc(froth_vm_t *vm, const uint8_t *data,
                               froth_cell_t len, froth_cell_t *out_cell) {
  if (len < 0 || len > FROTH_TBUF_SIZE - TBUF_OVERHEAD)
    return FROTH_ERROR_TRANSIENT_FULL;

  froth_tbuf_t *tbuf = &vm->tbuf;
  uint16_t total = (uint16_t)(len + TBUF_OVERHEAD);

  if (tbuf->write_cursor + total > FROTH_TBUF_SIZE) {
    /* Reclaim descriptors in the abandoned tail before wrapping. */
    reclaim_overlapping(tbuf, tbuf->write_cursor, FROTH_TBUF_SIZE);
    tbuf->write_cursor = 0;
  }

  reclaim_overlapping(tbuf, tbuf->write_cursor, tbuf->write_cursor + total);

  int idx = claim_free_descriptor(tbuf);
  if (idx < 0)
    return FROTH_ERROR_TRANSIENT_FULL;

  /* Write length header (little-endian u16) */
  tbuf->ring[tbuf->write_cursor++] = (uint8_t)(len & 0xFF);
  tbuf->ring[tbuf->write_cursor++] = (uint8_t)((len >> 8) & 0xFF);

  uint16_t payload_offset = tbuf->write_cursor;
  memcpy(&tbuf->ring[tbuf->write_cursor], data, len);
  tbuf->write_cursor += len;
  tbuf->ring[tbuf->write_cursor++] = '\0';

  froth_tdesc_t *desc = &tbuf->descriptors[idx];
  desc->ring_offset = payload_offset;
  desc->len = (uint16_t)len;
  desc->generation = tbuf->generation;
  desc->kind = FROTH_TDESC_SCRATCH_RING;

  froth_cell_u_t payload =
      FROTH_BSTRING_TRANSIENT_PAYLOAD((froth_cell_u_t)idx, tbuf->generation);
  tbuf->generation++;

  return froth_make_cell(payload, FROTH_BSTRING, out_cell);
}

froth_error_t froth_bstring_resolve(froth_vm_t *vm, froth_cell_t cell,
                                    froth_bstring_view_t *view) {
  if (!FROTH_CELL_IS_BSTRING(cell))
    return FROTH_ERROR_TYPE_MISMATCH;

  froth_cell_u_t payload = FROTH_CELL_STRIP_TAG(cell);

  if (!FROTH_BSTRING_IS_TRANSIENT(payload)) {
    uint8_t *base = &vm->heap.data[payload];
    memcpy(&view->len, base, sizeof(froth_cell_t));
    view->data = base + sizeof(froth_cell_t);
    return FROTH_OK;
  }

  froth_tdesc_t *desc =
      &vm->tbuf.descriptors[FROTH_BSTRING_DESC_INDEX(payload)];
  if (is_stale(desc, FROTH_BSTRING_CELL_GEN(payload)))
    return FROTH_ERROR_TRANSIENT_EXPIRED;

  view->data = &vm->tbuf.ring[desc->ring_offset];
  view->len = desc->len;
  return FROTH_OK;
}

bool froth_bstring_is_transient(froth_cell_t cell) {
  return FROTH_BSTRING_IS_TRANSIENT(FROTH_CELL_STRIP_TAG(cell));
}

froth_error_t froth_bstring_promote(froth_vm_t *vm, froth_cell_t cell,
                                    froth_cell_t *out_cell) {
  froth_bstring_view_t view;
  FROTH_TRY(froth_bstring_resolve(vm, cell, &view));

  froth_cell_u_t heap_offset;
  froth_cell_t len_cell = view.len;
  FROTH_TRY(froth_heap_allocate_bytes(sizeof(froth_cell_t) + view.len + 1,
                                      &vm->heap, &heap_offset));

  uint8_t *dest = &vm->heap.data[heap_offset];
  memcpy(dest, &len_cell, sizeof(froth_cell_t));
  memcpy(dest + sizeof(froth_cell_t), view.data, view.len);
  dest[sizeof(froth_cell_t) + view.len] = '\0';

  return froth_make_cell(heap_offset, FROTH_BSTRING, out_cell);
}

bool froth_bstring_try_resolve(froth_vm_t *vm, froth_cell_t cell,
                               froth_bstring_view_t *view) {
  if (!FROTH_CELL_IS_BSTRING(cell))
    return false;

  froth_cell_u_t payload = FROTH_CELL_STRIP_TAG(cell);

  if (!FROTH_BSTRING_IS_TRANSIENT(payload)) {
    uint8_t *base = &vm->heap.data[payload];
    memcpy(&view->len, base, sizeof(froth_cell_t));
    view->data = base + sizeof(froth_cell_t);
    return true;
  }

  froth_tdesc_t *desc =
      &vm->tbuf.descriptors[FROTH_BSTRING_DESC_INDEX(payload)];
  if (is_stale(desc, FROTH_BSTRING_CELL_GEN(payload)))
    return false;

  view->data = &vm->tbuf.ring[desc->ring_offset];
  view->len = desc->len;
  return true;
}
