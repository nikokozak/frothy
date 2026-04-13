#include "froth_console.h"

froth_error_t froth_console_emit(uint8_t byte) { return platform_emit(byte); }

froth_error_t froth_console_flush_output(void) { return FROTH_OK; }

froth_error_t froth_console_key(froth_vm_t *vm, uint8_t *byte) {
  (void)vm;
  return platform_key(byte);
}

bool froth_console_key_ready(void) { return platform_key_ready(); }

void froth_console_poll(froth_vm_t *vm) { platform_check_interrupt(vm); }

bool froth_console_live_active(void) { return false; }
