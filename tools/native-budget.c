/*
 * Prints how many native table rows the base uses, how many the profile
 * allows, and how many library natives the firmware can name. `frothy build`
 * reads this to refuse a library set that cannot fit, because the firmware
 * stops at boot when either table is full. The library table is the empty
 * default here, so the row count is the base alone.
 */
#include "froth.h"
#include "lib_native.h"

#include <stdio.h>

int main(void) {
  static fr_runtime_t runtime;

  if (fr_base_image_install(&runtime) != FR_OK) {
    return 1;
  }
  printf("NATIVE_BASE_ROWS=%u\nNATIVE_TABLE_SIZE=%u\nNATIVE_LIBRARY_NAMES=%u\n",
         (unsigned)runtime.natives.count,
         (unsigned)FR_PROFILE_NATIVE_TABLE_SIZE,
         (unsigned)FR_LIB_NATIVE_RECORD_MAX);
  return 0;
}
