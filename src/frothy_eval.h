#pragma once

#include "frothy_ir.h"
#include "frothy_value.h"

froth_error_t frothy_eval_program(const frothy_ir_program_t *program,
                                  frothy_value_t *out);
