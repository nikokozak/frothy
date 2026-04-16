#pragma once

#include "frothy_ir.h"
#include "frothy_value.h"

froth_error_t frothy_eval_program(const frothy_ir_program_t *program,
                                  frothy_value_t *out);
size_t frothy_eval_frame_high_water(void);
void frothy_eval_debug_reset_frame_high_water(void);
