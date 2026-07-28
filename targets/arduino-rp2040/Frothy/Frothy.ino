extern "C" {
#include "froth.h"
#include "repl.h"

fr_err_t fr_rp2040_platform_init(void);
}

static fr_runtime_t runtime;

static void halt(fr_err_t err) {
  for (;;) {
    if (Serial) {
      Serial.print("frothy halt err ");
      Serial.println((unsigned)err);
    }
    delay(1000);
  }
}

void setup() {
  fr_err_t err = fr_rp2040_platform_init();
  if (err != FR_OK) {
    halt(err);
  }

  err = fr_base_image_install(&runtime);
  if (err != FR_OK) {
    halt(err);
  }

  err = fr_repl_startup_restore_and_boot(&runtime);
  if (err != FR_OK) {
    halt(err);
  }
}

void loop() {
  fr_err_t err = fr_repl_run_platform(&runtime);
  if (err != FR_OK) {
    halt(err);
  }
}
