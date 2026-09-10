#pragma once

#include "esp32_plain.h"

/* The ESP32-C3 has no MCPWM capture peripheral. */
#undef FR_FEATURE_TRACE
#define FR_FEATURE_TRACE 0
