#pragma once

#include "frothy_ir.h"

#ifndef FROTHY_PARSER_BINDING_CAPACITY
#define FROTHY_PARSER_BINDING_CAPACITY 128
#endif

#ifndef FROTHY_PARSER_SCOPE_CAPACITY
#define FROTHY_PARSER_SCOPE_CAPACITY 32
#endif

#ifndef FROTHY_PARSER_FRAME_CAPACITY
#define FROTHY_PARSER_FRAME_CAPACITY 16
#endif

froth_error_t frothy_parse_top_level(const char *source,
                                     frothy_ir_program_t *program);
froth_error_t frothy_parse_top_level_prefix(const char *source,
                                            size_t *consumed_out,
                                            frothy_ir_program_t *program);
