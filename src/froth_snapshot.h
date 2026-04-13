#pragma once
#include "froth_types.h"

#define FROTH_SNAPSHOT_MAGIC "FRTHSNAP"
#define FROTH_SNAPSHOT_VERSION 0x0005
#define FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES                                       \
  (FROTH_SNAPSHOT_BLOCK_SIZE - FROTH_SNAPSHOT_HEADER_SIZE)
#define FROTH_SNAPSHOT_MAX_OBJECTS 50
#define FROTH_SNAPSHOT_MAX_QUOTE_DEPTH 10
#define FROTH_SNAPSHOT_MAX_NAME_LEN 63
#define FROTH_SNAPSHOT_HEADER_SIZE 50 // bytes

/* Lower-bound sanity check:
 * this proves the payload area can hold an empty names/objects/bindings
 * prefix plus a fully allocated numeric CellSpace prefix.
 * It is necessary, not sufficient, for all snapshot shapes. */
#define FROTH_SNAPSHOT_MIN_CELLSPACE_PAYLOAD_BYTES                             \
  (2u + 4u + 4u + 4u + (FROTH_DATA_SPACE_SIZE * (1u + sizeof(froth_cell_t))))
_Static_assert(
    FROTH_SNAPSHOT_BLOCK_SIZE > FROTH_SNAPSHOT_HEADER_SIZE,
    "Snapshot block size cannot be smaller than the Snapshot header size.");
_Static_assert(FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES >=
                   FROTH_SNAPSHOT_MIN_CELLSPACE_PAYLOAD_BYTES,
               "Snapshot payload area is too small for the minimum full "
               "CellSpace prefix lower bound");

// HEADER OFFSET CONSTANTS

#define FROTH_SNAPSHOT_MAGIC_OFFSET 0
#define FROTH_SNAPSHOT_VERSION_OFFSET 8
#define FROTH_SNAPSHOT_FLAGS_OFFSET 10
#define FROTH_SNAPSHOT_CELL_BITS_OFFSET 12
#define FROTH_SNAPSHOT_ENDIAN_OFFSET 13
#define FROTH_SNAPSHOT_ABI_HASH_OFFSET 14
#define FROTH_SNAPSHOT_GENERATION_OFFSET 18
#define FROTH_SNAPSHOT_PAYLOAD_LEN_OFFSET 22
#define FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET 26
#define FROTH_SNAPSHOT_HEADER_CRC32_OFFSET 30
#define FROTH_SNAPSHOT_RESERVED_OFFSET 34

typedef struct {
  uint8_t *data;
  froth_cell_u_t position;
} froth_snapshot_buffer_t;

/* --- Workspace types (used by writer and reader, kept off the stack) --- */

typedef struct {
  const char *name;
  froth_cell_u_t slot_index;
} froth_snapshot_name_item_t;

typedef struct {
  froth_snapshot_name_item_t items[FROTH_SLOT_TABLE_SIZE];
  froth_cell_u_t count;
} froth_snapshot_name_table_t;

typedef struct {
  froth_cell_u_t object_id;
  froth_cell_u_t heap_offset;
  froth_cell_tag_t type;
} froth_snapshot_object_item_t;

typedef struct {
  froth_snapshot_object_item_t items[FROTH_SNAPSHOT_MAX_OBJECTS];
  froth_cell_u_t count;
} froth_snapshot_object_table_t;

typedef struct {
  froth_cell_u_t quote_heap_offset;
  froth_cell_u_t next_token_index;
} froth_snapshot_walk_frame_t;

typedef struct {
  froth_snapshot_walk_frame_t frames[FROTH_SNAPSHOT_MAX_QUOTE_DEPTH];
  froth_cell_u_t depth;
} froth_snapshot_walk_stack_t;

/* Single workspace for save/restore. Lives in BSS, not on the call stack.
 * Gated behind FROTH_HAS_SNAPSHOTS so non-snapshot targets pay nothing. */
typedef struct {
  uint8_t ram_buffer[FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES];
  uint8_t header[FROTH_SNAPSHOT_HEADER_SIZE];
  froth_snapshot_name_table_t names;
  froth_snapshot_object_table_t objects;
  froth_snapshot_walk_stack_t walk;
  froth_cell_u_t reader_names[FROTH_SLOT_TABLE_SIZE];
  froth_cell_t reader_objects[FROTH_SNAPSHOT_MAX_OBJECTS];
} froth_snapshot_workspace_t;

typedef struct {
  uint32_t payload_len;
  uint32_t generation;
  uint16_t flags;
} froth_snapshot_header_info_t;

froth_error_t froth_snapshot_save(froth_vm_t *froth_vm,
                                  froth_snapshot_buffer_t *snapshot_buffer,
                                  froth_snapshot_workspace_t *ws);
froth_error_t froth_snapshot_load(froth_vm_t *froth_vm,
                                  froth_snapshot_buffer_t *snapshot_buffer,
                                  froth_snapshot_workspace_t *ws);
froth_error_t reset_overlay_to_base(froth_vm_t *froth_vm);

froth_error_t froth_snapshot_build_header(uint8_t *header, uint32_t payload_len,
                                          const uint8_t *payload,
                                          uint32_t generation);
froth_error_t
froth_snapshot_parse_header(const uint8_t *header,
                            froth_snapshot_header_info_t *parse_out);

uint32_t froth_snapshot_abi_hash(void);

#ifdef FROTH_HAS_SNAPSHOTS
#include "froth_ffi.h"
extern const froth_ffi_entry_t froth_snapshot_prims[];
#endif

/* A/B slot selection.
 * slot_out receives 0 or 1. generation_out receives the winning generation.
 * Returns FROTH_OK if a valid slot was found, FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT
 * if neither slot contains a valid snapshot (first boot). */
froth_error_t froth_snapshot_pick_active(uint8_t *slot_out,
                                         uint32_t *generation_out);

/* Returns the inactive slot (for save) and the next generation to use.
 * Always succeeds — if neither slot is valid, picks slot 0 with generation 1.
 */
froth_error_t froth_snapshot_pick_inactive(uint8_t *slot_out,
                                           uint32_t *next_generation_out);
