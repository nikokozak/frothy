#pragma once

#include "esp32_plain.h"

/* The ESP32-C3 has no MCPWM capture peripheral. */
#undef FR_FEATURE_TRACE
#define FR_FEATURE_TRACE 0

/* The XIAO ESP32C3 has about 7 KB of free heap with Wi-Fi and BLE on, so it
   keeps the RAM code area of ADR 0084 (ADR 0083). */
#undef FR_PROFILE_RAM_CODE_BYTES
#define FR_PROFILE_RAM_CODE_BYTES 1544

/* 163 rows hold the base and stepper, the one library with natives that the
   C3 supports, and the heap does not grow. */
#undef FR_PROFILE_NATIVE_TABLE_SIZE
#define FR_PROFILE_NATIVE_TABLE_SIZE 163
