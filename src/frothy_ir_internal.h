#pragma once

#include "frothy_ir.h"

/* Internal packed-storage helpers for runtime/snapshot ownership paths. */
froth_error_t frothy_ir_program_clone_packed_size(
    const frothy_ir_program_t *source, size_t *size_out);
froth_error_t frothy_ir_program_packed_size(size_t literal_count,
                                            size_t node_count,
                                            size_t link_count,
                                            size_t string_bytes,
                                            size_t *size_out);
froth_error_t frothy_ir_program_init_packed_view(
    frothy_ir_program_t *program, void *storage, size_t storage_size,
    size_t literal_count, size_t node_count, size_t link_count,
    uint8_t **string_cursor_out, uint8_t **storage_end_out);
froth_error_t frothy_ir_program_clone_packed(const frothy_ir_program_t *source,
                                             void *storage,
                                             size_t storage_size,
                                             frothy_ir_program_t *dest);
