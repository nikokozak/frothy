#pragma once

#include "froth_types.h"
#include <stdbool.h>
#include <stdint.h>

/* Transient string buffer (ADR-043).
 *
 * Runtime-produced strings (top-level literals, FFI, future constructors)
 * live in a circular scratch ring, not on the heap. A fixed descriptor
 * table tracks live transient strings. Permanent strings (quotation-body
 * literals, promoted via def/s.keep) stay as heap offsets per ADR-023.
 *
 * All string consumers go through froth_bstring_resolve(). */

/* --- Compile-time configuration --- */

#ifndef FROTH_TBUF_SIZE
#define FROTH_TBUF_SIZE 1024
#endif

#ifndef FROTH_TDESC_MAX
#define FROTH_TDESC_MAX 32
#endif

/* Validate: descriptor index must fit in FROTH_BSTRING_DESC_BITS (5 bits = max 32).
 * Cell width must leave room for the transient flag + desc bits + at least 1 gen bit. */
#if FROTH_TDESC_MAX > 32
#error "FROTH_TDESC_MAX exceeds 32 (5-bit descriptor index limit)"
#endif
#if FROTH_CELL_SIZE_BITS < 16
#error "Transient string buffer requires at least 16-bit cells"
#endif
#if FROTH_TBUF_SIZE > 65535
#error "FROTH_TBUF_SIZE exceeds uint16_t range (max 65535)"
#endif
#if FROTH_STRING_MAX_LEN > FROTH_TBUF_SIZE
#error "FROTH_STRING_MAX_LEN exceeds FROTH_TBUF_SIZE (string cannot exceed ring)"
#endif

/* --- Descriptor entry --- */

#define FROTH_TDESC_FREE 0
#define FROTH_TDESC_SCRATCH_RING 1

typedef struct {
  uint16_t ring_offset; /* offset into scratch ring (start of bytes) */
  uint16_t len;         /* byte count (excludes length header and null) */
  uint32_t generation;  /* allocation generation for ABA detection */
  uint8_t kind;         /* 0 = free, 1 = SCRATCH_RING */
} froth_tdesc_t;

/* --- Transient buffer state (lives on froth_vm_t) --- */

typedef struct {
  uint8_t ring[FROTH_TBUF_SIZE];
  froth_tdesc_t descriptors[FROTH_TDESC_MAX];
  uint32_t generation; /* monotonic, incremented per allocation */
  uint16_t write_cursor;
} froth_tbuf_t;

/* --- Resolved string view --- */

typedef struct {
  const uint8_t *data;
  froth_cell_t len;
} froth_bstring_view_t;

/* --- Payload encoding helpers --- */

/* Highest payload bit is the transient flag.
 * Payload = cell >> 3, so payload bits = FROTH_CELL_SIZE_BITS - 3.
 * Flag bit = payload bit (FROTH_CELL_SIZE_BITS - 4). */
#define FROTH_BSTRING_TRANSIENT_FLAG                                           \
  ((froth_cell_u_t)1 << (FROTH_CELL_SIZE_BITS - 4))

/* Descriptor index: low 5 bits of payload. */
#define FROTH_BSTRING_DESC_BITS 5
#define FROTH_BSTRING_DESC_MASK (((froth_cell_u_t)1 << FROTH_BSTRING_DESC_BITS) - 1)

/* Truncated generation: bits between descriptor index and transient flag. */
#define FROTH_BSTRING_GEN_SHIFT FROTH_BSTRING_DESC_BITS
#define FROTH_BSTRING_GEN_BITS                                                 \
  (FROTH_CELL_SIZE_BITS - 4 - FROTH_BSTRING_DESC_BITS)
#define FROTH_BSTRING_GEN_MASK (((froth_cell_u_t)1 << FROTH_BSTRING_GEN_BITS) - 1)

/* Check whether a BSTRING payload (already stripped of tag) is transient. */
#define FROTH_BSTRING_IS_TRANSIENT(payload)                                    \
  (((payload) & FROTH_BSTRING_TRANSIENT_FLAG) != 0)

/* Extract descriptor index from a transient payload. */
#define FROTH_BSTRING_DESC_INDEX(payload)                                      \
  ((payload) & FROTH_BSTRING_DESC_MASK)

/* Extract truncated generation from a transient payload. */
#define FROTH_BSTRING_CELL_GEN(payload)                                        \
  (((payload) >> FROTH_BSTRING_GEN_SHIFT) & FROTH_BSTRING_GEN_MASK)

/* Build a transient BSTRING payload from descriptor index and generation. */
#define FROTH_BSTRING_TRANSIENT_PAYLOAD(desc_idx, gen)                         \
  (FROTH_BSTRING_TRANSIENT_FLAG |                                              \
   (((gen) & FROTH_BSTRING_GEN_MASK) << FROTH_BSTRING_GEN_SHIFT) |            \
   ((desc_idx) & FROTH_BSTRING_DESC_MASK))

/* --- Forward declaration (vm defined in froth_vm.h) --- */

struct froth_vm_t;

/* --- Public API --- */

/* Initialize the transient buffer (call once at boot). */
void froth_tbuf_init(struct froth_vm_t *vm);

/* Allocate a transient string. Copies `len` bytes from `data` into the
 * scratch ring. Returns a tagged BSTRING cell via `out_cell`.
 * Errors: FROTH_ERROR_TRANSIENT_FULL if no free descriptor available. */
froth_error_t froth_tbuf_alloc(struct froth_vm_t *vm, const uint8_t *data,
                               froth_cell_t len, froth_cell_t *out_cell);

/* Resolve any BSTRING cell (permanent or transient) into a data/len view.
 * For permanent strings, resolves via heap offset (fast path).
 * For transient strings, resolves via descriptor with generation check.
 * Errors: FROTH_ERROR_TRANSIENT_EXPIRED if the string has been overwritten.
 *         FROTH_ERROR_TYPE_MISMATCH if cell is not a BSTRING. */
froth_error_t froth_bstring_resolve(struct froth_vm_t *vm, froth_cell_t cell,
                                    froth_bstring_view_t *view);

/* Check whether a BSTRING cell is transient (without resolving).
 * Returns true if transient, false if permanent.
 * Assumes the cell IS a BSTRING (caller must check tag first). */
bool froth_bstring_is_transient(froth_cell_t cell);

/* Promote a transient string to a permanent heap allocation.
 * Resolves the transient string, copies bytes to the heap (ADR-023 layout),
 * and returns the new permanent BSTRING cell via `out_cell`.
 * The transient descriptor is NOT freed (aliasing safety).
 * Errors: FROTH_ERROR_TRANSIENT_EXPIRED, FROTH_ERROR_HEAP_OUT_OF_MEMORY. */
froth_error_t froth_bstring_promote(struct froth_vm_t *vm, froth_cell_t cell,
                                    froth_cell_t *out_cell);

/* Resolve a transient string for diagnostic display (no throw on stale).
 * Returns true if successfully resolved, false if stale.
 * On false, `view` is not populated. Caller should display <str:stale>. */
bool froth_bstring_try_resolve(struct froth_vm_t *vm, froth_cell_t cell,
                               froth_bstring_view_t *view);
