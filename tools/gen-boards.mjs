#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const boardsDir = path.join(root, "boards");
const generatedNotice = "Generated from board.json by tools/gen-boards.mjs. Do not edit.";
const idPattern = /^[a-z0-9][a-z0-9_-]*$/;
const optionPattern = /^[A-Z][A-Z0-9_]*$/;
const optionValuePattern = /^[A-Za-z0-9_.,=+:/-]+$/;
const peripheralPattern = /^[a-z][a-z0-9_]*$/;
const generatedBoardOptions = new Set([
  "CFLAGS",
  "FLASH_BYTES",
  "PROFILE",
  "SOURCES",
  "TARGET",
]);

// This order defines persistent board-local slot IDs. Append new entries.
// Adding a pin to a board renumbers its later board-local slots.
const pinDefinitions = [
  {
    name: "$led_builtin",
    macro: "FR_BOARD_LED_BUILTIN",
    slot: "FR_SLOT_LED_BUILTIN",
  },
  {
    name: "$led_active_level",
    macro: "FR_BOARD_LED_ACTIVE_LEVEL",
    slot: "FR_SLOT_LED_ACTIVE_LEVEL",
  },
  { name: "$a0", macro: "FR_BOARD_A0" },
  { name: "$boot_button", macro: "FR_BOARD_BOOT_BUTTON" },
  {
    name: "$sda",
    macro: "FR_BOARD_I2C_SDA",
    feature: "FR_FEATURE_I2C",
  },
  {
    name: "$scl",
    macro: "FR_BOARD_I2C_SCL",
    feature: "FR_FEATURE_I2C",
  },
];
// Dollar names become Frothy base words. Bare names are target metadata.
const metadataPins = new Set(["uart_tx", "uart_rx", "uart_baud"]);
const knownPins = new Set([
  ...pinDefinitions.map(({ name }) => name),
  ...metadataPins,
]);
const consoleKinds = new Set(["stdio", "uart", "usb_cdc", "usb_serial_jtag"]);

// Peripherals are board-provided I/O or native-port surfaces. Runtime-only
// features such as trace and pulse stay with target/profile facts. The host
// board lists the surfaces its virtual platform simulates.
function fail(boardId, message) {
  throw new Error(`${boardId}: ${message}`);
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function requireText(boardId, object, key, pattern) {
  const value = object[key];
  if (typeof value !== "string" || value.length === 0 ||
      (pattern && !pattern.test(value))) {
    fail(boardId, `${key} is missing or invalid`);
  }
  return value;
}

function requireInteger(boardId, value, label) {
  if (!Number.isSafeInteger(value) || value < 0) {
    fail(boardId, `${label} must be a non-negative integer`);
  }
}

export function validateBoard(boardId, board) {
  if (!isObject(board)) fail(boardId, "board.json must contain an object");

  const allowedKeys = new Set([
    "name",
    "chip",
    "target",
    "profile",
    "bootsel",
    "cores",
    "peripherals",
    "pins",
    "console",
    "target_options",
    "flash_bytes",
  ]);
  for (const key of Object.keys(board)) {
    if (!allowedKeys.has(key)) fail(boardId, `unknown field ${key}`);
  }

  requireText(boardId, board, "name");
  requireText(boardId, board, "chip", idPattern);
  const target = requireText(boardId, board, "target", idPattern);
  const profile = requireText(boardId, board, "profile", idPattern);
  if (!fs.existsSync(path.join(root, "targets", target, "target.mk"))) {
    fail(boardId, `target ${target} does not exist`);
  }
  if (!fs.existsSync(path.join(root, "profiles", `${profile}.h`))) {
    fail(boardId, `profile ${profile} does not exist`);
  }

  if (board.bootsel !== undefined) {
    requireText(boardId, board, "bootsel");
  }

  requireInteger(boardId, board.cores, "cores");
  if (board.cores < 1) fail(boardId, "cores must be positive");

  if (!Array.isArray(board.peripherals)) {
    fail(boardId, "peripherals must be an array");
  }
  const peripherals = new Set();
  for (const peripheral of board.peripherals) {
    if (typeof peripheral !== "string" ||
        !peripheralPattern.test(peripheral)) {
      fail(boardId, "peripherals contains an invalid name");
    }
    if (peripherals.has(peripheral)) {
      fail(boardId, `peripherals contains ${peripheral} more than once`);
    }
    peripherals.add(peripheral);
  }

  if (!isObject(board.pins)) fail(boardId, "pins must be an object");
  for (const [name, value] of Object.entries(board.pins)) {
    if (!knownPins.has(name)) fail(boardId, `unknown pin ${name}`);
    requireInteger(boardId, value, `pin ${name}`);
  }
  for (const name of ["$led_builtin", "$led_active_level"]) {
    if (!(name in board.pins)) fail(boardId, `pin ${name} is required`);
  }
  if (![0, 1].includes(board.pins.$led_active_level)) {
    fail(boardId, "pin $led_active_level must be 0 or 1");
  }
  const i2cPinCount = ["$sda", "$scl"]
    .filter((name) => name in board.pins).length;
  if (i2cPinCount !== 0 && i2cPinCount !== 2) {
    fail(boardId, "pins $sda and $scl must be declared together");
  }
  const uartPins = ["uart_tx", "uart_rx", "uart_baud"];
  const uartPinCount = uartPins.filter((name) => name in board.pins).length;
  if (uartPinCount !== 0 && uartPinCount !== uartPins.length) {
    fail(boardId, "uart tx, rx, and baud values must be declared together");
  }
  const hasUartPins = uartPinCount === uartPins.length;

  if (!isObject(board.console)) fail(boardId, "console must be an object");
  for (const key of Object.keys(board.console)) {
    if (!["kind", "port"].includes(key)) {
      fail(boardId, `unknown console field ${key}`);
    }
  }
  const consoleKind = requireText(boardId, board.console, "kind");
  if (!consoleKinds.has(consoleKind)) {
    fail(boardId, `unknown console kind ${consoleKind}`);
  }
  if (consoleKind === "uart") {
    requireInteger(boardId, board.console.port, "console port");
    if (!hasUartPins) fail(boardId, "the uart console needs uart pin values");
  } else if (board.console.port !== undefined) {
    fail(boardId, "only a uart console can declare a port");
  }

  if (board.target_options !== undefined) {
    if (!isObject(board.target_options)) {
      fail(boardId, "target_options must be an object");
    }
    for (const [name, value] of Object.entries(board.target_options)) {
      if (!optionPattern.test(name)) {
        fail(boardId, `invalid target option ${name}`);
      }
      if (generatedBoardOptions.has(name)) {
        fail(boardId, `target option ${name} is generated`);
      }
      if ((typeof value !== "string" && !Number.isSafeInteger(value)) ||
          !optionValuePattern.test(String(value))) {
        fail(boardId, `target option ${name} has an invalid value`);
      }
    }
  }

  if (board.flash_bytes !== undefined) {
    requireInteger(boardId, board.flash_bytes, "flash_bytes");
    if (board.flash_bytes === 0) fail(boardId, "flash_bytes must be positive");
  }
}

function renderMake(boardId, board) {
  const lines = [
    `# ${generatedNotice}`,
    `BOARD_TARGET := ${board.target}`,
    `BOARD_PROFILE := ${board.profile}`,
  ];
  const targetOptions = Object.entries(board.target_options ?? {})
    .sort(([left], [right]) =>
      left < right ? -1 : left > right ? 1 : 0);
  for (const [name, value] of targetOptions) {
    lines.push(`BOARD_${name} := ${value}`);
  }
  if (board.flash_bytes !== undefined) {
    lines.push(`BOARD_FLASH_BYTES := ${board.flash_bytes}`);
    lines.push(
      `BOARD_CFLAGS += -DFR_PROFILE_TARGET_FLASH_BYTES=${board.flash_bytes}u`,
    );
  }
  lines.push(`BOARD_SOURCES += boards/${boardId}/board_defs.c`, "");
  return lines.join("\n");
}

function renderHeader(board) {
  const lines = [
    `/* ${generatedNotice} */`,
    "#pragma once",
    "",
  ];
  if (board.console.kind === "uart") {
    lines.push(
      `#define FR_BOARD_UART_PORT ${board.console.port}`,
      `#define FR_BOARD_UART_BAUD ${board.pins.uart_baud}`,
      `#define FR_BOARD_UART_TX ${board.pins.uart_tx}`,
      `#define FR_BOARD_UART_RX ${board.pins.uart_rx}`,
    );
  }
  lines.push(
    `#define FR_BOARD_CONSOLE_${board.console.kind.toUpperCase()} 1`,
    "",
  );
  for (const { name, macro } of pinDefinitions) {
    if (name in board.pins) {
      lines.push(`#define ${macro} ${board.pins[name]}u`);
    }
  }
  lines.push("");
  return lines.join("\n");
}

function appendGuarded(lines, items, render) {
  let feature;
  for (const item of items) {
    if (item.feature !== feature) {
      if (feature) lines.push("#endif");
      feature = item.feature;
      if (feature) lines.push(`#if ${feature}`);
    }
    lines.push(...render(item));
  }
  if (feature) lines.push("#endif");
}

function renderDefinition(pin) {
  return [
    "    {",
    `        .slot_id = ${pin.slot},`,
    "#if FR_BASE_IMAGE_INCLUDE_SYMBOLS",
    `        .name = "${pin.name}",`,
    "#endif",
    "        .kind = FR_BASE_DEF_LITERAL,",
    `        .literal_tagged = FR_TAGGED_INT_LITERAL(${pin.macro}),`,
    "    },",
  ];
}

function renderDefinitions(board) {
  const pins = pinDefinitions.filter(({ name }) => name in board.pins);
  const localPins = pins
    .filter(({ slot }) => slot === undefined)
    .map((pin, index) => ({
      ...pin,
      slot: `FR_SLOT_${pin.name.slice(1).toUpperCase()}`,
      slotValue: index === 0
        ? "FR_SLOT_BOARD_LOCAL_BASE"
        : `FR_SLOT_BOARD_LOCAL_BASE + ${index}`,
    }));
  const slotsByName = new Map(localPins.map((pin) => [pin.name, pin.slot]));
  const resolvedPins = pins.map((pin) => ({
    ...pin,
    slot: pin.slot ?? slotsByName.get(pin.name),
  }));

  const lines = [
    `/* ${generatedNotice} */`,
    '#include "base_defs.h"',
    "",
    '#include "board.h"',
    "",
  ];
  if (localPins.length > 0) {
    lines.push("enum {");
    appendGuarded(
      lines,
      localPins,
      (pin) => [`  ${pin.slot} = ${pin.slotValue},`],
    );
    lines.push("};", "");
  }
  lines.push("const fr_base_def_t fr_board_base_defs[] = {");
  appendGuarded(lines, resolvedPins, renderDefinition);
  lines.push(
    "};",
    "",
    "const uint16_t fr_board_base_def_count =",
    "    (uint16_t)(sizeof(fr_board_base_defs) / sizeof(fr_board_base_defs[0]));",
    "",
  );
  return lines.join("\n");
}

export function renderBoard(boardId, board) {
  validateBoard(boardId, board);
  return new Map([
    ["board.mk", renderMake(boardId, board)],
    ["board.h", renderHeader(board)],
    ["board_defs.c", renderDefinitions(board)],
  ]);
}

function boardIds(arguments_) {
  const requested = arguments_.filter((argument) => argument !== "--check");
  for (const argument of requested) {
    if (!idPattern.test(argument)) fail(argument, "invalid board id");
  }
  if (requested.length > 0) return [...new Set(requested)].sort();
  return fs.readdirSync(boardsDir, { withFileTypes: true })
    .filter((entry) =>
      entry.isDirectory() &&
      fs.existsSync(path.join(boardsDir, entry.name, "board.json")))
    .map((entry) => entry.name)
    .sort();
}

function main() {
  const arguments_ = process.argv.slice(2);
  const check = arguments_.includes("--check");
  const unknownOption = arguments_.find((argument) =>
    argument.startsWith("-") && argument !== "--check");
  if (unknownOption) throw new Error(`unknown option ${unknownOption}`);

  let stale = false;
  for (const boardId of boardIds(arguments_)) {
    const boardFile = path.join(boardsDir, boardId, "board.json");
    if (!fs.existsSync(boardFile)) fail(boardId, "board.json does not exist");
    let board;
    try {
      board = JSON.parse(fs.readFileSync(boardFile, "utf8"));
    } catch (error) {
      fail(boardId, error instanceof Error ? error.message : String(error));
    }
    for (const [name, expected] of renderBoard(boardId, board)) {
      const output = path.join(boardsDir, boardId, name);
      const current = fs.existsSync(output) ? fs.readFileSync(output, "utf8") : "";
      if (current === expected) continue;
      if (check) {
        console.error(`${path.relative(root, output)} is stale`);
        stale = true;
      } else {
        fs.writeFileSync(output, expected);
      }
    }
  }
  if (stale) process.exitCode = 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    main();
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
}
