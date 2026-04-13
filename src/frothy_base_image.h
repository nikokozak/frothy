#pragma once

#include "froth_types.h"

#include <stdbool.h>

froth_error_t frothy_base_image_install(void);
froth_error_t frothy_base_image_reset(void);
bool frothy_base_image_has_slot(const char *name);
bool frothy_base_image_builtin_emits_output(const char *name);
bool frothy_base_image_shell_suppresses_raw_output(const char *name);
