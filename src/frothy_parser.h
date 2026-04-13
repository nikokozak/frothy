#pragma once

#include "frothy_ir.h"

froth_error_t frothy_parse_top_level(const char *source,
                                     frothy_ir_program_t *program);
