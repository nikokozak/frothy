extern "C" {
#include "froth.h"
#include "repl.h"

fr_err_t fr_rp2040_platform_init(void);
}

static fr_runtime_t runtime;

// Repeats "<label>: <name> (<code>)" once per second, so that a serial
// monitor that opens late still sees why the board stopped.
static void halt(const char *label, fr_err_t err) {
  for (;;) {
    if (Serial) {
      Serial.print(label);
      Serial.print(": ");
      Serial.print(fr_err_name(err));
      Serial.print(" (");
      Serial.print((unsigned)err);
      Serial.println(")");
    }
    delay(1000);
  }
}

void setup() {
  fr_err_t err = fr_rp2040_platform_init();
  if (err != FR_OK) {
    halt("startup err", err);
  }

  err = fr_base_image_install(&runtime);
  if (err != FR_OK) {
    halt("startup err", err);
  }

  err = fr_repl_startup_restore_and_boot(&runtime);
  if (err != FR_OK) {
    halt("startup err", err);
  }
}

void loop() {
  fr_err_t err = fr_repl_run_platform(&runtime);
  if (err != FR_OK) {
    halt("repl err", err);
  }
}
