/*
 * Prints how many native table rows the base uses and how many the profile
 * allows. `frothy build` reads this to refuse a library set that cannot fit,
 * because the firmware stops at boot when the table is full. The library
 * table is the weak empty default here, so the count is the base alone.
 */
#include "froth.h"

#include <stdio.h>

int main(void) {
  static fr_runtime_t runtime;

  if (fr_base_image_install(&runtime) != FR_OK) {
    return 1;
  }
  printf("NATIVE_BASE_ROWS=%u\nNATIVE_TABLE_SIZE=%u\n",
         (unsigned)runtime.natives.count,
         (unsigned)FR_PROFILE_NATIVE_TABLE_SIZE);
  return 0;
}
