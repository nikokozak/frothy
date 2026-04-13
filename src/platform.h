#pragma once

#include "froth_types.h"
#include <stdbool.h>

struct froth_vm_t;

typedef froth_error_t (*platform_emit_hook_t)(void *context, uint8_t byte);

typedef struct {
  froth_cell_t port;
  froth_cell_t tx;
  froth_cell_t rx;
  froth_cell_t baud;
} platform_console_uart_info_t;

froth_error_t platform_init(void);
froth_error_t platform_emit(uint8_t byte);
froth_error_t platform_emit_raw(uint8_t byte); /* no line-ending conversion */
void platform_set_emit_hook(platform_emit_hook_t hook, void *context);
void platform_clear_emit_hook(void);
froth_error_t platform_key(uint8_t *byte);
bool platform_input_closed(void);
bool platform_should_echo_input(void);
bool platform_key_ready(void);
void platform_check_interrupt(struct froth_vm_t *vm);
void platform_delay_ms(froth_cell_u_t ms);
uint32_t platform_uptime_ms(void);
void platform_reset_runtime_state(void);
froth_error_t platform_console_uart_bind(froth_cell_t port, froth_cell_t tx,
                                         froth_cell_t rx, froth_cell_t baud);
froth_error_t platform_console_uart_default(void);
froth_error_t platform_console_uart_info(platform_console_uart_info_t *info);

_Noreturn void platform_fatal(void);

#ifdef FROTH_HAS_SNAPSHOTS
froth_error_t platform_snapshot_read(uint8_t slot, uint32_t offset,
                                     uint8_t *buf, uint32_t len);
froth_error_t platform_snapshot_write(uint8_t slot, uint32_t offset,
                                      const uint8_t *buf, uint32_t len);
froth_error_t platform_snapshot_erase(uint8_t slot);
#endif
