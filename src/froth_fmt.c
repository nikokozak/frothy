#include "froth_fmt.h"
#include "froth_console.h"
#include <stdio.h>

froth_error_t emit_string(const char* str) {
  for (const char* p = str; *p != '\0'; p++) {
    FROTH_TRY(froth_console_emit((uint8_t)*p));
  }
  return FROTH_OK;
}

char* format_number(froth_cell_t number) {
  static char buf[32];
  snprintf(buf, sizeof(buf), "%" FROTH_CELL_FORMAT, number);
  return buf;
}
