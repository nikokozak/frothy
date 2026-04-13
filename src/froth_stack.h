#pragma once

#include "froth_types.h"

#ifndef FROTH_DS_CAPACITY
#error                                                                         \
    "FROTH_DS_CAPACITY is not defined. Please define it to the desired capacity of the data stack."
#endif
#ifndef FROTH_RS_CAPACITY
#error                                                                         \
    "FROTH_RS_CAPACITY is not defined. Please define it to the desired capacity of the return stack."
#endif
#ifndef FROTH_CS_CAPACITY
#error                                                                         \
    "FROTH_CS_CAPACITY is not defined. Please define it to the desired capacity of the call stack."
#endif

/* Stack: simple fixed-capacity array with a top-of-stack pointer. */
typedef struct froth_stack_t {
  froth_cell_u_t pointer;  // Points to the *next free cell*
  froth_cell_u_t capacity; // Essentially the size of the data array
  froth_cell_t *data;      // An array that holds our cells
} froth_stack_t;

/* CS frame: continuation state for the trampoline executor. */
typedef struct froth_cs_frame_t {
  froth_cell_u_t quote_offset; /* heap byte offset of the quotation */
  froth_cell_u_t ip;           /* next cell index to execute (1-based) */
} froth_cs_frame_t;

typedef struct froth_cs_t {
  froth_cell_u_t pointer;
  froth_cell_u_t capacity;
  froth_cs_frame_t *data;
} froth_cs_t;

froth_error_t froth_stack_push(froth_stack_t *stack, froth_cell_t value);
froth_error_t froth_stack_pop(froth_stack_t *stack, froth_cell_t *return_value);
froth_error_t froth_stack_peek(froth_stack_t *stack,
                               froth_cell_t *return_value);
froth_cell_u_t froth_stack_depth(froth_stack_t *stack);
