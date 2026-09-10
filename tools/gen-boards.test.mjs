import assert from "node:assert/strict";
import fs from "node:fs";
import test from "node:test";

import { renderBoard, validateBoard } from "./gen-boards.mjs";

const readBoard = (name) => JSON.parse(
  fs.readFileSync(new URL(`../boards/${name}/board.json`, import.meta.url)),
);

test("board generation protects Make values and stable board slots", () => {
  const esp32 = readBoard("esp32_devkit_v1");
  esp32.target_options.ESP_IDF_TARGET = "$(shell false)";
  assert.throws(
    () => validateBoard("esp32_devkit_v1", esp32),
    /target option ESP_IDF_TARGET has an invalid value/,
  );

  const missingScl = readBoard("seeed_xiao_rp2040");
  delete missingScl.pins.$scl;
  assert.throws(
    () => validateBoard("seeed_xiao_rp2040", missingScl),
    /pins \$sda and \$scl must be declared together/,
  );

  const missingUart = readBoard("esp32_devkit_v1");
  delete missingUart.pins.uart_tx;
  assert.throws(
    () => validateBoard("esp32_devkit_v1", missingUart),
    /uart tx, rx, and baud values must be declared together/,
  );

  const nano = renderBoard(
    "arduino_nano_rp2040_connect",
    readBoard("arduino_nano_rp2040_connect"),
  );
  assert.match(nano.get("board.h"), /FR_BOARD_CONSOLE_USB_CDC 1/);
  assert.match(nano.get("board.mk"), /BOARD_FLASH_BYTES := 16777216/);
  assert.match(
    nano.get("board_defs.c"),
    /FR_SLOT_SDA = FR_SLOT_BOARD_LOCAL_BASE \+ 1/,
  );

  const xiao = renderBoard(
    "seeed_xiao_rp2040",
    readBoard("seeed_xiao_rp2040"),
  );
  assert.match(
    xiao.get("board_defs.c"),
    /FR_SLOT_SDA = FR_SLOT_BOARD_LOCAL_BASE \+ 1/,
  );
});

test("the XIAO ESP32C3 uses USB serial and has no programmable LED", () => {
  const board = readBoard("seeed_xiao_esp32c3");
  assert.equal(board.chip, "esp32c3");
  assert.equal(board.cores, 1);
  assert.deepEqual(board.pins, {
    $boot_button: 9, $a0: 2, $sda: 6, $scl: 7,
    uart_tx: 21, uart_rx: 20, uart_baud: 115200,
  });
  const generated = renderBoard("seeed_xiao_esp32c3", board);
  assert.match(generated.get("board.h"), /FR_BOARD_CONSOLE_USB_SERIAL_JTAG 1/);
  assert.doesNotMatch(generated.get("board.h"), /FR_BOARD_LED/);
  assert.equal(generated.get("board_defs.c").match(/FR_TAGGED_NIL/g).length, 2);
  assert.match(generated.get("board.mk"), /BOARD_PROFILE := esp32c3_plain/);
  assert.match(generated.get("board.mk"), /BOARD_ESP_IDF_TARGET := esp32c3/);

  board.pins.$led_builtin = 0;
  assert.throws(() => validateBoard("seeed_xiao_esp32c3", board),
    /pins \$led_builtin and \$led_active_level must be declared together/);
});
