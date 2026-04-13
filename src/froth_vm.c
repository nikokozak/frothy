#include "froth_vm.h"
#include "froth_stack.h"
#include "froth_tbuf.h"
#include "froth_types.h"

static froth_cell_t ds_memory[FROTH_DS_CAPACITY];
static froth_cell_t rs_memory[FROTH_RS_CAPACITY];
static froth_cs_frame_t cs_memory[FROTH_CS_CAPACITY];
static uint8_t heap_memory[FROTH_HEAP_SIZE];
static froth_cell_t cellspace_memory[FROTH_DATA_SPACE_SIZE];
static froth_cell_t cellspace_base_seed_memory[FROTH_DATA_SPACE_SIZE];

froth_vm_t froth_vm = {
    .ds = {.pointer = 0, .capacity = FROTH_DS_CAPACITY, .data = ds_memory},
    .rs = {.pointer = 0, .capacity = FROTH_RS_CAPACITY, .data = rs_memory},
    .cs = {.pointer = 0, .capacity = FROTH_CS_CAPACITY, .data = cs_memory},
    .heap = {.data = heap_memory, .pointer = 0},
    .cellspace = {.data = cellspace_memory,
                  .base_seed = cellspace_base_seed_memory,
                  .capacity = FROTH_DATA_SPACE_SIZE},
    .tbuf = {.generation = 1, .write_cursor = 0},
    .thrown = FROTH_OK,
    .last_error_slot = -1,
    .interrupted = 0,
    .boot_complete = 0,
    .watermark_heap_offset = 0,
    .mark_offset = (froth_cell_u_t)-1,
};
