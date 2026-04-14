#include "frothy_eval.h"

#include "froth_slot_table.h"
#include "froth_vm.h"
#include "frothy_value.h"
#include "platform.h"

#include <stdint.h>
#include <string.h>

typedef struct {
  frothy_value_t *values;
  size_t count;
  size_t arena_base;
  uint32_t reset_epoch;
} frothy_eval_buffer_t;

typedef struct {
  frothy_value_t *locals;
  size_t local_count;
  size_t arena_base;
  uint32_t reset_epoch;
} frothy_frame_t;

static frothy_runtime_t *frothy_runtime(void) {
  return &froth_vm.frothy_runtime;
}

static bool frothy_reset_epoch_matches(uint32_t reset_epoch) {
  return frothy_runtime()->reset_epoch == reset_epoch;
}

static froth_error_t frothy_eval_reset_sentinel(frothy_value_t *out) {
  *out = frothy_value_make_nil();
  return FROTH_ERROR_RESET;
}

static froth_error_t frothy_poll_interrupt(void) {
  platform_check_interrupt(&froth_vm);
  if (!froth_vm.interrupted) {
    return FROTH_OK;
  }

  froth_vm.interrupted = 0;
  return FROTH_ERROR_PROGRAM_INTERRUPTED;
}

static void frothy_release_ignored(frothy_runtime_t *runtime,
                                   frothy_value_t value) {
  (void)frothy_value_release(runtime, value);
}

static void frothy_release_array_ignored(frothy_runtime_t *runtime,
                                         frothy_value_t *values,
                                         size_t count) {
  size_t i;

  if (values == NULL) {
    return;
  }

  for (i = 0; i < count; i++) {
    frothy_release_ignored(runtime, values[i]);
  }
}

static void frothy_nil_array(frothy_value_t *values, size_t count) {
  size_t i;

  for (i = 0; i < count; i++) {
    values[i] = frothy_value_make_nil();
  }
}

static int32_t frothy_wrap_int30(int64_t raw) {
  const uint32_t mask = ((uint32_t)1u << 30) - 1u;
  const uint32_t sign = (uint32_t)1u << 29;
  uint32_t bits = (uint32_t)raw & mask;

  if ((bits & sign) != 0u) {
    bits |= ~mask;
  }
  return (int32_t)bits;
}

static froth_error_t frothy_make_wrapped_int(int64_t raw,
                                             frothy_value_t *out) {
  return frothy_value_make_int(frothy_wrap_int30(raw), out);
}

static froth_error_t frothy_eval_buffer_init(frothy_eval_buffer_t *buffer,
                                             size_t count) {
  frothy_runtime_t *runtime = frothy_runtime();
  size_t limit = runtime->eval_value_limit;
  size_t base = runtime->eval_value_used;
  size_t used;

  memset(buffer, 0, sizeof(*buffer));
  buffer->count = count;
  buffer->arena_base = base;
  buffer->reset_epoch = runtime->reset_epoch;
  if (count == 0) {
    return FROTH_OK;
  }
  if (runtime->eval_values == NULL || count > limit || base > limit - count) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  buffer->values = runtime->eval_values + base;
  frothy_nil_array(buffer->values, count);
  used = base + count;
  runtime->eval_value_used = used;
  if (used > runtime->eval_value_high_water) {
    runtime->eval_value_high_water = used;
  }
  return FROTH_OK;
}

static void frothy_eval_buffer_free(frothy_runtime_t *runtime,
                                    frothy_eval_buffer_t *buffer) {
  if (buffer->values == NULL) {
    memset(buffer, 0, sizeof(*buffer));
    return;
  }

  if (runtime->reset_epoch == buffer->reset_epoch) {
    frothy_release_array_ignored(runtime, buffer->values, buffer->count);
    runtime->eval_value_used = buffer->arena_base;
  } else {
    frothy_nil_array(buffer->values, buffer->count);
  }
  memset(buffer, 0, sizeof(*buffer));
}

static froth_error_t frothy_frame_init(frothy_frame_t *frame,
                                       size_t local_count) {
  frothy_eval_buffer_t buffer;

  memset(frame, 0, sizeof(*frame));
  FROTH_TRY(frothy_eval_buffer_init(&buffer, local_count));
  frame->locals = buffer.values;
  frame->local_count = buffer.count;
  frame->arena_base = buffer.arena_base;
  frame->reset_epoch = buffer.reset_epoch;
  return FROTH_OK;
}

static void frothy_frame_free(frothy_runtime_t *runtime, frothy_frame_t *frame) {
  frothy_eval_buffer_t buffer = {
      .values = frame->locals,
      .count = frame->local_count,
      .arena_base = frame->arena_base,
      .reset_epoch = frame->reset_epoch,
  };

  frothy_eval_buffer_free(runtime, &buffer);
  memset(frame, 0, sizeof(*frame));
}

static froth_error_t frothy_eval_node(const frothy_ir_program_t *program,
                                      frothy_value_t *locals,
                                      size_t local_count,
                                      frothy_ir_node_id_t node_id,
                                      frothy_value_t *out);

static froth_error_t frothy_eval_children(const frothy_ir_program_t *program,
                                          frothy_value_t *locals,
                                          size_t local_count,
                                          size_t first_arg,
                                          size_t arg_count,
                                          frothy_value_t *values_out) {
  size_t i;

  for (i = 0; i < arg_count; i++) {
    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               program->links[first_arg + i],
                               &values_out[i]));
  }

  return FROTH_OK;
}

static froth_error_t frothy_slot_read_owned(const char *slot_name,
                                            frothy_value_t *out) {
  froth_cell_u_t slot_index;
  froth_cell_t impl;

  FROTH_TRY(froth_slot_find_name(slot_name, &slot_index));
  FROTH_TRY(froth_slot_get_impl(slot_index, &impl));
  *out = frothy_value_from_cell(impl);
  return frothy_value_retain(frothy_runtime(), *out);
}

static froth_error_t frothy_slot_update_arity(froth_cell_u_t slot_index,
                                              frothy_value_t value) {
  const char *record_name = NULL;
  size_t arity = 0;

  if (frothy_runtime_get_code(frothy_runtime(), value, NULL, NULL, &arity,
                              NULL) == FROTH_OK &&
      arity < FROTH_SLOT_ARITY_UNKNOWN) {
    return froth_slot_set_arity(slot_index, (uint8_t)arity, 1);
  }
  if (frothy_runtime_get_native(frothy_runtime(), value, NULL, NULL, NULL,
                                &arity) == FROTH_OK &&
      arity < FROTH_SLOT_ARITY_UNKNOWN) {
    return froth_slot_set_arity(slot_index, (uint8_t)arity, 1);
  }
  if (frothy_runtime_get_record_def(frothy_runtime(), value, &record_name,
                                    &arity) == FROTH_OK &&
      arity < FROTH_SLOT_ARITY_UNKNOWN) {
    return froth_slot_set_arity(slot_index, (uint8_t)arity, 1);
  }

  return froth_slot_clear_arity(slot_index);
}

static froth_error_t frothy_slot_write_owned(const char *slot_name,
                                             frothy_value_t value,
                                             bool require_existing) {
  froth_cell_u_t slot_index;
  froth_cell_t old_impl = 0;
  froth_error_t err;
  bool created = false;

  err = froth_slot_find_name(slot_name, &slot_index);
  if (frothy_value_is_slot_designator(value)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }
  if (err == FROTH_ERROR_UNDEFINED_WORD) {
    if (require_existing) {
      return err;
    }
    FROTH_TRY(froth_slot_create(slot_name, &froth_vm.heap, &slot_index));
    created = true;
  } else {
    FROTH_TRY(err);
  }

  err = froth_slot_get_impl(slot_index, &old_impl);
  if (err != FROTH_OK && err != FROTH_ERROR_UNDEFINED_WORD) {
    return err;
  }

  if (err == FROTH_OK) {
    FROTH_TRY(frothy_value_release(frothy_runtime(),
                                   frothy_value_from_cell(old_impl)));
  }
  (void)created;
  /* Base-image seeding runs before boot_complete so those definitions survive
   * wipe/reset as part of the preflashed image instead of the overlay. */
  FROTH_TRY(froth_slot_set_overlay(slot_index, froth_vm.boot_complete ? 1u : 0u));
  FROTH_TRY(froth_slot_set_impl(slot_index, frothy_value_to_cell(value)));
  FROTH_TRY(frothy_slot_update_arity(slot_index, value));

  return FROTH_OK;
}

static froth_error_t frothy_slot_read_fallback_owned(const char *primary_slot_name,
                                                     const char *fallback_slot_name,
                                                     frothy_value_t *out) {
  froth_cell_u_t slot_index;
  froth_error_t err;

  err = froth_slot_find_name(primary_slot_name, &slot_index);
  if (err == FROTH_OK) {
    return frothy_slot_read_owned(primary_slot_name, out);
  }
  if (err != FROTH_ERROR_UNDEFINED_WORD) {
    return err;
  }

  return frothy_slot_read_owned(fallback_slot_name, out);
}

static froth_error_t frothy_slot_write_fallback_owned(
    const char *primary_slot_name, const char *fallback_slot_name,
    frothy_value_t value, bool require_existing) {
  froth_error_t err =
      frothy_slot_write_owned(primary_slot_name, value, require_existing);

  if (err != FROTH_ERROR_UNDEFINED_WORD) {
    return err;
  }
  return frothy_slot_write_owned(fallback_slot_name, value, require_existing);
}

static froth_error_t frothy_index_offset(frothy_value_t index_value,
                                         size_t length,
                                         froth_cell_t *offset_out) {
  int32_t index;

  if (!frothy_value_is_int(index_value)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  index = frothy_value_as_int(index_value);
  if (index < 0 || (size_t)index >= length) {
    return FROTH_ERROR_BOUNDS;
  }

  *offset_out = (froth_cell_t)index;
  return FROTH_OK;
}

static froth_error_t frothy_cells_value_allowed(frothy_value_t value) {
  frothy_value_class_t value_class;

  FROTH_TRY(frothy_value_class(frothy_runtime(), value, &value_class));
  switch (value_class) {
  case FROTHY_VALUE_CLASS_INT:
  case FROTHY_VALUE_CLASS_BOOL:
  case FROTHY_VALUE_CLASS_NIL:
  case FROTHY_VALUE_CLASS_TEXT:
  case FROTHY_VALUE_CLASS_RECORD:
    return FROTH_OK;
  case FROTHY_VALUE_CLASS_CELLS:
  case FROTHY_VALUE_CLASS_CODE:
  case FROTHY_VALUE_CLASS_NATIVE:
  case FROTHY_VALUE_CLASS_RECORD_DEF:
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  return FROTH_ERROR_TYPE_MISMATCH;
}

static froth_error_t frothy_read_index_owned(frothy_value_t base_value,
                                             frothy_value_t index_value,
                                             frothy_value_t *out) {
  frothy_runtime_t *runtime = frothy_runtime();
  froth_cell_t base = 0;
  froth_cell_t offset;
  size_t length = 0;

  FROTH_TRY(frothy_runtime_get_cells(runtime, base_value, &length, &base));
  FROTH_TRY(frothy_index_offset(index_value, length, &offset));
  *out = frothy_value_from_cell(runtime->cellspace->data[base + offset]);
  return frothy_value_retain(runtime, *out);
}

static froth_error_t frothy_write_index_owned(frothy_value_t base_value,
                                              frothy_value_t index_value,
                                              frothy_value_t stored_value) {
  frothy_runtime_t *runtime = frothy_runtime();
  froth_cell_t base = 0;
  froth_cell_t offset;
  size_t length = 0;
  frothy_value_t old_value;

  FROTH_TRY(frothy_runtime_get_cells(runtime, base_value, &length, &base));
  FROTH_TRY(frothy_index_offset(index_value, length, &offset));
  FROTH_TRY(frothy_cells_value_allowed(stored_value));

  old_value = frothy_value_from_cell(runtime->cellspace->data[base + offset]);
  FROTH_TRY(frothy_value_release(runtime, old_value));
  runtime->cellspace->data[base + offset] = frothy_value_to_cell(stored_value);
  return FROTH_OK;
}

static froth_error_t frothy_eval_record_def_node(
    const frothy_ir_program_t *program, const frothy_ir_node_t *node,
    frothy_value_t *out) {
  return frothy_runtime_alloc_record_def_from_ir(
      frothy_runtime(), node->as.record_def.record_name, program,
      node->as.record_def.first_field, node->as.record_def.field_count, out);
}

static froth_error_t frothy_read_field_owned(frothy_value_t base_value,
                                             const char *field_name,
                                             frothy_value_t *out) {
  return frothy_runtime_record_read_field(frothy_runtime(), base_value,
                                          field_name, out);
}

static froth_error_t frothy_write_field_owned(frothy_value_t base_value,
                                              const char *field_name,
                                              frothy_value_t stored_value) {
  return frothy_runtime_record_write_field(frothy_runtime(), base_value,
                                           field_name, stored_value);
}

static froth_error_t frothy_eval_builtin(frothy_ir_builtin_kind_t builtin,
                                         frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  froth_error_t err = FROTH_OK;
  bool equal = false;
  int32_t lhs;
  int32_t rhs;

  switch (builtin) {
  case FROTHY_IR_BUILTIN_CELLS:
    if (arg_count != 1 || !frothy_value_is_int(args[0]) ||
        frothy_value_as_int(args[0]) <= 0) {
      err = FROTH_ERROR_BOUNDS;
      break;
    }
    err = frothy_runtime_alloc_cells(frothy_runtime(),
                                     (size_t)frothy_value_as_int(args[0]), out);
    break;
  case FROTHY_IR_BUILTIN_NOT:
    if (arg_count != 1 || !frothy_value_is_bool(args[0])) {
      err = FROTH_ERROR_TYPE_MISMATCH;
      break;
    }
    *out = frothy_value_make_bool(!frothy_value_as_bool(args[0]));
    break;
  case FROTHY_IR_BUILTIN_NEGATE:
    if (arg_count != 1 || !frothy_value_is_int(args[0])) {
      err = FROTH_ERROR_TYPE_MISMATCH;
      break;
    }
    err = frothy_make_wrapped_int(-(int64_t)frothy_value_as_int(args[0]), out);
    break;
  case FROTHY_IR_BUILTIN_ADD:
  case FROTHY_IR_BUILTIN_SUB:
  case FROTHY_IR_BUILTIN_MUL:
  case FROTHY_IR_BUILTIN_DIV:
  case FROTHY_IR_BUILTIN_REM:
  case FROTHY_IR_BUILTIN_LT:
  case FROTHY_IR_BUILTIN_LE:
  case FROTHY_IR_BUILTIN_GT:
  case FROTHY_IR_BUILTIN_GE:
    if (arg_count != 2 || !frothy_value_is_int(args[0]) ||
        !frothy_value_is_int(args[1])) {
      err = FROTH_ERROR_TYPE_MISMATCH;
      break;
    }

    lhs = frothy_value_as_int(args[0]);
    rhs = frothy_value_as_int(args[1]);
    switch (builtin) {
    case FROTHY_IR_BUILTIN_ADD:
      err = frothy_make_wrapped_int((int64_t)lhs + (int64_t)rhs, out);
      break;
    case FROTHY_IR_BUILTIN_SUB:
      err = frothy_make_wrapped_int((int64_t)lhs - (int64_t)rhs, out);
      break;
    case FROTHY_IR_BUILTIN_MUL:
      err = frothy_make_wrapped_int((int64_t)lhs * (int64_t)rhs, out);
      break;
    case FROTHY_IR_BUILTIN_DIV:
      if (rhs == 0) {
        err = FROTH_ERROR_DIVISION_BY_ZERO;
        break;
      }
      err = frothy_make_wrapped_int(lhs / rhs, out);
      break;
    case FROTHY_IR_BUILTIN_REM:
      if (rhs == 0) {
        err = FROTH_ERROR_DIVISION_BY_ZERO;
        break;
      }
      err = frothy_make_wrapped_int(lhs % rhs, out);
      break;
    case FROTHY_IR_BUILTIN_LT:
      *out = frothy_value_make_bool(lhs < rhs);
      break;
    case FROTHY_IR_BUILTIN_LE:
      *out = frothy_value_make_bool(lhs <= rhs);
      break;
    case FROTHY_IR_BUILTIN_GT:
      *out = frothy_value_make_bool(lhs > rhs);
      break;
    case FROTHY_IR_BUILTIN_GE:
      *out = frothy_value_make_bool(lhs >= rhs);
      break;
    default:
      break;
    }
    break;
  case FROTHY_IR_BUILTIN_EQ:
  case FROTHY_IR_BUILTIN_NEQ:
    if (arg_count != 2) {
      err = FROTH_ERROR_SIGNATURE;
      break;
    }
    err = frothy_value_equals(frothy_runtime(), args[0], args[1], &equal);
    if (err == FROTH_OK) {
      *out = frothy_value_make_bool(builtin == FROTHY_IR_BUILTIN_EQ ? equal
                                                                    : !equal);
    }
    break;
  case FROTHY_IR_BUILTIN_NONE:
    err = FROTH_ERROR_SIGNATURE;
    break;
  }

  return err;
}

static froth_error_t frothy_eval_call(const frothy_ir_program_t *program,
                                      frothy_value_t *locals,
                                      size_t local_count,
                                      const frothy_ir_node_t *node,
                                      frothy_value_t *out) {
  frothy_value_t callee = frothy_value_make_nil();
  frothy_eval_buffer_t args;
  froth_error_t err;

  memset(&args, 0, sizeof(args));

  if (node->as.call.builtin != FROTHY_IR_BUILTIN_NONE) {
    FROTH_TRY(frothy_eval_buffer_init(&args, node->as.call.arg_count));
    err = frothy_eval_children(program, locals, local_count,
                               node->as.call.first_arg, node->as.call.arg_count,
                               args.values);
    if (err != FROTH_OK) {
      frothy_eval_buffer_free(frothy_runtime(), &args);
      return err;
    }
    if (!frothy_reset_epoch_matches(args.reset_epoch)) {
      frothy_eval_buffer_free(frothy_runtime(), &args);
      return frothy_eval_reset_sentinel(out);
    }
    err = frothy_eval_builtin(node->as.call.builtin, args.values,
                              node->as.call.arg_count, out);
    frothy_eval_buffer_free(frothy_runtime(), &args);
    return err;
  }

  FROTH_TRY(frothy_eval_node(program, locals, local_count, node->as.call.callee,
                             &callee));

  {
    const frothy_ir_program_t *callee_program = NULL;
    frothy_native_fn_t native_fn = NULL;
    const void *native_context = NULL;
    const char *record_name = NULL;
    frothy_ir_node_id_t body = FROTHY_IR_NODE_INVALID;
    size_t arity = 0;
    size_t callee_local_count = 0;
    frothy_frame_t frame;
    uint32_t reset_epoch = frothy_runtime()->reset_epoch;
    bool is_record_def = false;

    memset(&frame, 0, sizeof(frame));
    err = frothy_runtime_get_code(frothy_runtime(), callee, &callee_program, &body,
                                  &arity, &callee_local_count);
    if (err == FROTH_ERROR_TYPE_MISMATCH) {
      err = frothy_runtime_get_native(frothy_runtime(), callee, &native_fn,
                                      &native_context,
                                      NULL, &arity);
      if (err == FROTH_ERROR_TYPE_MISMATCH) {
        err = frothy_runtime_get_record_def(frothy_runtime(), callee,
                                            &record_name, &arity);
        if (err == FROTH_OK) {
          is_record_def = true;
        }
      }
    }
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), callee);
      return err;
    }
    if (arity != node->as.call.arg_count) {
      frothy_release_ignored(frothy_runtime(), callee);
      return FROTH_ERROR_SIGNATURE;
    }

    if (native_fn != NULL) {
      err = frothy_eval_buffer_init(&args, node->as.call.arg_count);
      if (err != FROTH_OK) {
        frothy_release_ignored(frothy_runtime(), callee);
        return err;
      }
      err = frothy_eval_children(program, locals, local_count,
                                 node->as.call.first_arg,
                                 node->as.call.arg_count, args.values);
      if (err != FROTH_OK) {
        frothy_eval_buffer_free(frothy_runtime(), &args);
        if (frothy_reset_epoch_matches(reset_epoch)) {
          frothy_release_ignored(frothy_runtime(), callee);
        }
        return err;
      }
      if (!frothy_reset_epoch_matches(reset_epoch)) {
        frothy_eval_buffer_free(frothy_runtime(), &args);
        return frothy_eval_reset_sentinel(out);
      }

      err = native_fn(frothy_runtime(), native_context, args.values,
                      node->as.call.arg_count, out);
      if (frothy_reset_epoch_matches(reset_epoch)) {
        frothy_release_ignored(frothy_runtime(), callee);
      }
      frothy_eval_buffer_free(frothy_runtime(), &args);
      if (!frothy_reset_epoch_matches(reset_epoch) && err == FROTH_OK) {
        return frothy_eval_reset_sentinel(out);
      }
      return err;
    }

    if (is_record_def) {
      err = frothy_eval_buffer_init(&args, node->as.call.arg_count);
      if (err != FROTH_OK) {
        frothy_release_ignored(frothy_runtime(), callee);
        return err;
      }
      err = frothy_eval_children(program, locals, local_count,
                                 node->as.call.first_arg,
                                 node->as.call.arg_count, args.values);
      if (err != FROTH_OK) {
        frothy_eval_buffer_free(frothy_runtime(), &args);
        if (frothy_reset_epoch_matches(reset_epoch)) {
          frothy_release_ignored(frothy_runtime(), callee);
        }
        return err;
      }
      if (!frothy_reset_epoch_matches(reset_epoch)) {
        frothy_eval_buffer_free(frothy_runtime(), &args);
        return frothy_eval_reset_sentinel(out);
      }

      (void)record_name;
      err = frothy_runtime_alloc_record(frothy_runtime(), callee, args.values,
                                        node->as.call.arg_count, out);
      if (frothy_reset_epoch_matches(reset_epoch)) {
        frothy_release_ignored(frothy_runtime(), callee);
      }
      frothy_eval_buffer_free(frothy_runtime(), &args);
      if (!frothy_reset_epoch_matches(reset_epoch) && err == FROTH_OK) {
        return frothy_eval_reset_sentinel(out);
      }
      return err;
    }

    if (callee_local_count < arity) {
      frothy_release_ignored(frothy_runtime(), callee);
      return FROTH_ERROR_SIGNATURE;
    }

    err = frothy_frame_init(&frame, callee_local_count);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), callee);
      return err;
    }

    err = frothy_eval_children(program, locals, local_count,
                               node->as.call.first_arg, arity, frame.locals);
    if (err != FROTH_OK) {
      frothy_frame_free(frothy_runtime(), &frame);
      if (frothy_reset_epoch_matches(reset_epoch)) {
        frothy_release_ignored(frothy_runtime(), callee);
      }
      return err;
    }
    if (!frothy_reset_epoch_matches(reset_epoch)) {
      frothy_frame_free(frothy_runtime(), &frame);
      return frothy_eval_reset_sentinel(out);
    }

    err = frothy_eval_node(callee_program, frame.locals, frame.local_count, body,
                           out);
    frothy_frame_free(frothy_runtime(), &frame);
    if (!frothy_reset_epoch_matches(reset_epoch) && err == FROTH_OK) {
      return frothy_eval_reset_sentinel(out);
    }
    if (frothy_reset_epoch_matches(reset_epoch)) {
      frothy_release_ignored(frothy_runtime(), callee);
    }
    return err;
  }
}

static froth_error_t frothy_eval_if(const frothy_ir_program_t *program,
                                    frothy_value_t *locals,
                                    size_t local_count,
                                    const frothy_ir_node_t *node,
                                    frothy_value_t *out) {
  frothy_value_t condition;
  bool take_then;
  froth_error_t err;

  FROTH_TRY(frothy_eval_node(program, locals, local_count,
                             node->as.if_expr.condition, &condition));
  if (!frothy_value_is_bool(condition)) {
    frothy_release_ignored(frothy_runtime(), condition);
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  take_then = frothy_value_as_bool(condition);
  frothy_release_ignored(frothy_runtime(), condition);

  if (take_then) {
    return frothy_eval_node(program, locals, local_count,
                            node->as.if_expr.then_branch, out);
  }
  if (!node->as.if_expr.has_else_branch) {
    *out = frothy_value_make_nil();
    return FROTH_OK;
  }

  err = frothy_eval_node(program, locals, local_count,
                         node->as.if_expr.else_branch, out);
  return err;
}

static froth_error_t frothy_eval_while(const frothy_ir_program_t *program,
                                       frothy_value_t *locals,
                                       size_t local_count,
                                       const frothy_ir_node_t *node,
                                       frothy_value_t *out) {
  while (1) {
    frothy_value_t condition;
    frothy_value_t body_value;

    FROTH_TRY(frothy_poll_interrupt());
    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               node->as.while_expr.condition, &condition));
    if (!frothy_value_is_bool(condition)) {
      frothy_release_ignored(frothy_runtime(), condition);
      return FROTH_ERROR_TYPE_MISMATCH;
    }
    if (!frothy_value_as_bool(condition)) {
      frothy_release_ignored(frothy_runtime(), condition);
      *out = frothy_value_make_nil();
      return FROTH_OK;
    }

    frothy_release_ignored(frothy_runtime(), condition);
    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               node->as.while_expr.body, &body_value));
    frothy_release_ignored(frothy_runtime(), body_value);
  }
}

static froth_error_t frothy_eval_seq(const frothy_ir_program_t *program,
                                     frothy_value_t *locals,
                                     size_t local_count,
                                     const frothy_ir_node_t *node,
                                     frothy_value_t *out) {
  size_t i;

  if (node->as.seq.item_count == 0) {
    *out = frothy_value_make_nil();
    return FROTH_OK;
  }

  for (i = 0; i < node->as.seq.item_count; i++) {
    frothy_value_t item_value;

    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               program->links[node->as.seq.first_item + i],
                               &item_value));
    if (i + 1 == node->as.seq.item_count) {
      *out = item_value;
      return FROTH_OK;
    }
    frothy_release_ignored(frothy_runtime(), item_value);
  }

  *out = frothy_value_make_nil();
  return FROTH_OK;
}

static froth_error_t frothy_eval_node(const frothy_ir_program_t *program,
                                      frothy_value_t *locals,
                                      size_t local_count,
                                      frothy_ir_node_id_t node_id,
                                      frothy_value_t *out) {
  const frothy_ir_node_t *node;
  froth_error_t err;

  FROTH_TRY(frothy_poll_interrupt());
  node = &program->nodes[node_id];

  switch (node->kind) {
  case FROTHY_IR_NODE_LIT:
    return frothy_value_from_literal(frothy_runtime(),
                                     &program->literals[node->as.lit.literal_id],
                                     out);
  case FROTHY_IR_NODE_READ_LOCAL:
    if (node->as.read_local.local_index >= local_count) {
      return FROTH_ERROR_BOUNDS;
    }
    *out = locals[node->as.read_local.local_index];
    return frothy_value_retain(frothy_runtime(), *out);
  case FROTHY_IR_NODE_WRITE_LOCAL: {
    size_t local_index = node->as.write_local.local_index;
    frothy_value_t value;

    if (local_index >= local_count) {
      return FROTH_ERROR_BOUNDS;
    }

    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               node->as.write_local.value, &value));
    err = frothy_value_release(frothy_runtime(), locals[local_index]);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), value);
      return err;
    }
    locals[local_index] = value;
    *out = frothy_value_make_nil();
    return FROTH_OK;
  }
  case FROTHY_IR_NODE_READ_SLOT:
    return frothy_slot_read_owned(node->as.read_slot.slot_name, out);
  case FROTHY_IR_NODE_READ_SLOT_FALLBACK:
    return frothy_slot_read_fallback_owned(
        node->as.read_slot_fallback.primary_slot_name,
        node->as.read_slot_fallback.fallback_slot_name, out);
  case FROTHY_IR_NODE_WRITE_SLOT: {
    frothy_value_t value;

    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               node->as.write_slot.value, &value));
    err = frothy_slot_write_owned(node->as.write_slot.slot_name, value,
                                  node->as.write_slot.require_existing);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), value);
      return err;
    }
    *out = frothy_value_make_nil();
    return FROTH_OK;
  }
  case FROTHY_IR_NODE_WRITE_SLOT_FALLBACK: {
    frothy_value_t value;

    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               node->as.write_slot_fallback.value, &value));
    err = frothy_slot_write_fallback_owned(
        node->as.write_slot_fallback.primary_slot_name,
        node->as.write_slot_fallback.fallback_slot_name, value,
        node->as.write_slot_fallback.require_existing);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), value);
      return err;
    }
    *out = frothy_value_make_nil();
    return FROTH_OK;
  }
  case FROTHY_IR_NODE_SLOT_DESIGNATOR:
    return frothy_value_make_slot_designator(node->as.slot_designator.slot_name,
                                             out);
  case FROTHY_IR_NODE_RECORD_DEF:
    return frothy_eval_record_def_node(program, node, out);
  case FROTHY_IR_NODE_READ_INDEX: {
    frothy_value_t base_value;
    frothy_value_t index_value;

    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               node->as.read_index.base, &base_value));
    err = frothy_eval_node(program, locals, local_count,
                           node->as.read_index.index, &index_value);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), base_value);
      return err;
    }

    err = frothy_read_index_owned(base_value, index_value, out);
    frothy_release_ignored(frothy_runtime(), index_value);
    frothy_release_ignored(frothy_runtime(), base_value);
    return err;
  }
  case FROTHY_IR_NODE_WRITE_INDEX: {
    frothy_value_t base_value;
    frothy_value_t index_value;
    frothy_value_t stored_value;

    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               node->as.write_index.base, &base_value));
    err = frothy_eval_node(program, locals, local_count,
                           node->as.write_index.index, &index_value);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), base_value);
      return err;
    }
    err = frothy_eval_node(program, locals, local_count,
                           node->as.write_index.value, &stored_value);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), index_value);
      frothy_release_ignored(frothy_runtime(), base_value);
      return err;
    }

    err = frothy_write_index_owned(base_value, index_value, stored_value);
    frothy_release_ignored(frothy_runtime(), index_value);
    frothy_release_ignored(frothy_runtime(), base_value);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), stored_value);
      return err;
    }
    *out = frothy_value_make_nil();
    return FROTH_OK;
  }
  case FROTHY_IR_NODE_READ_FIELD: {
    frothy_value_t base_value;

    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               node->as.read_field.base, &base_value));
    err = frothy_read_field_owned(base_value, node->as.read_field.field_name,
                                  out);
    frothy_release_ignored(frothy_runtime(), base_value);
    return err;
  }
  case FROTHY_IR_NODE_WRITE_FIELD: {
    frothy_value_t base_value;
    frothy_value_t stored_value;

    FROTH_TRY(frothy_eval_node(program, locals, local_count,
                               node->as.write_field.base, &base_value));
    err = frothy_eval_node(program, locals, local_count,
                           node->as.write_field.value, &stored_value);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), base_value);
      return err;
    }

    err = frothy_write_field_owned(base_value, node->as.write_field.field_name,
                                   stored_value);
    frothy_release_ignored(frothy_runtime(), base_value);
    if (err != FROTH_OK) {
      frothy_release_ignored(frothy_runtime(), stored_value);
      return err;
    }
    *out = frothy_value_make_nil();
    return FROTH_OK;
  }
  case FROTHY_IR_NODE_FN:
    return frothy_runtime_alloc_code(frothy_runtime(), program, node->as.fn.body,
                                     node->as.fn.arity, node->as.fn.local_count,
                                     out);
  case FROTHY_IR_NODE_CALL:
    return frothy_eval_call(program, locals, local_count, node, out);
  case FROTHY_IR_NODE_IF:
    return frothy_eval_if(program, locals, local_count, node, out);
  case FROTHY_IR_NODE_WHILE:
    return frothy_eval_while(program, locals, local_count, node, out);
  case FROTHY_IR_NODE_SEQ:
    return frothy_eval_seq(program, locals, local_count, node, out);
  }

  return FROTH_ERROR_SIGNATURE;
}

froth_error_t frothy_eval_program(const frothy_ir_program_t *program,
                                  frothy_value_t *out) {
  frothy_frame_t frame;
  froth_error_t err;

  FROTH_TRY(frothy_frame_init(&frame, program->root_local_count));
  err = frothy_eval_node(program, frame.locals, frame.local_count, program->root,
                         out);
  frothy_frame_free(frothy_runtime(), &frame);
  if (err == FROTH_ERROR_RESET) {
    *out = frothy_value_make_nil();
    return FROTH_OK;
  }
  return err;
}
