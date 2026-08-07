/*
 * RP2040 profile for boards with a u-blox NINA radio.
 *
 * The language and memory budgets stay owned by rp2040_plain. This profile
 * only opts the Nano RP2040 Connect into the portable network contract and
 * the BLE roles ArduinoBLE 2.1.0 can implement without private object layout:
 * observer and broadcaster.
 */

#pragma once

#ifndef FR_FEATURE_NET
#define FR_FEATURE_NET 1
#endif
#ifndef FR_FEATURE_BLE
#define FR_FEATURE_BLE 1
#endif

#include "rp2040_plain.h"

#if FR_FEATURE_BLE
#define FR_BLE_ENABLE_OBSERVER 1
#define FR_BLE_ENABLE_BROADCASTER 1
#define FR_BLE_SCAN_QUEUE_COUNT 8
#define FR_BLE_SCAN_DATA_BYTES 31
#define FR_BLE_ADVERTISEMENT_DATA_BYTES 31
#define FR_BLE_START_TIMEOUT_MS 5000
#define FR_BLE_STOP_TIMEOUT_MS 1000
#endif

#if FR_FEATURE_NET
#undef FR_PROFILE_NATIVE_TABLE_SIZE
#define FR_PROFILE_NATIVE_TABLE_SIZE 152
#define FR_HTTP_MAX_BODY 4096
#define FR_TCP_HANDLE_COUNT 2
#define FR_TCP_LISTENER_COUNT 1
#endif
