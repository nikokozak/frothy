#pragma once

#include "froth_cellspace.h"
#include "froth_heap.h"
#include "froth_stack.h"
#include "froth_tbuf.h"
#include "frothy_value.h"
#include <stdbool.h>

struct froth_vm_t {
  froth_stack_t ds;
  froth_stack_t rs;
  froth_cs_t cs;
  froth_heap_t heap;
  froth_cellspace_t cellspace;
  froth_tbuf_t tbuf;
  froth_cell_t thrown;
  froth_cell_t last_error_slot; /* slot index at point of error, or -1 */
  volatile int interrupted;
  uint8_t boot_complete;
  froth_cell_u_t
      trampoline_depth; /* C-level re-entry count for froth_execute_quote */
  froth_cell_u_t watermark_heap_offset;
  froth_cell_u_t mark_offset;
  frothy_runtime_t frothy_runtime;
};

extern froth_vm_t froth_vm;
