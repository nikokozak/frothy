#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BINARY="${FROTHY_BINARY:-$ROOT_DIR/build/Frothy}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/frothy-m9-ffi.XXXXXX")"

cleanup() {
  rm -rf "$WORK_DIR"
}

trap cleanup EXIT HUP INT TERM

if [ ! -x "$BINARY" ]; then
  echo "error: $BINARY is missing; run cmake -S . -B build && cmake --build build" >&2
  exit 1
fi

run_transcript() {
  (
    cd "$WORK_DIR"
    printf '%s\n' "$@" | "$BINARY"
  )
}

require_contains() {
  transcript=$1
  needle=$2
  if ! printf '%s\n' "$transcript" | grep -F "$needle" >/dev/null; then
    echo "error: expected transcript to contain: $needle" >&2
    exit 1
  fi
}

require_not_contains() {
  transcript=$1
  needle=$2
  if printf '%s\n' "$transcript" | grep -F "$needle" >/dev/null; then
    echo "error: transcript unexpectedly contained: $needle" >&2
    exit 1
  fi
}

WORDS_TRANSCRIPT="$(
  run_transcript \
    'words()' \
    'quit'
)"
printf '%s\n' "$WORDS_TRANSCRIPT"
require_contains "$WORDS_TRANSCRIPT" 'LED_BUILTIN'
require_contains "$WORDS_TRANSCRIPT" 'UART_TX'
require_contains "$WORDS_TRANSCRIPT" 'UART_RX'
require_contains "$WORDS_TRANSCRIPT" 'A0'
require_contains "$WORDS_TRANSCRIPT" 'gpio.mode'
require_contains "$WORDS_TRANSCRIPT" 'gpio.write'
require_contains "$WORDS_TRANSCRIPT" 'ms'
require_contains "$WORDS_TRANSCRIPT" 'adc.read'
require_contains "$WORDS_TRANSCRIPT" 'uart.init'
require_contains "$WORDS_TRANSCRIPT" 'uart.write'
require_contains "$WORDS_TRANSCRIPT" 'uart.read'

MAIN_TRANSCRIPT="$(
  run_transcript \
    'slotInfo("adc.read")' \
    'gpio.mode(LED_BUILTIN, 1)' \
    'gpio.write(LED_BUILTIN, 1)' \
    'gpio.write(LED_BUILTIN, 0)' \
    'ms(1)' \
    'adc.read(A0)' \
    'u = uart.init(UART_TX, UART_RX, 115200)' \
    'uart.write(79, u)' \
    'uart.read(u)' \
    'quit'
)"
printf '%s\n' "$MAIN_TRANSCRIPT"
require_contains "$MAIN_TRANSCRIPT" 'adc.read | base | native | non-persistable | foreign'
require_contains "$MAIN_TRANSCRIPT" '[gpio] pin 2 -> OUTPUT'
require_contains "$MAIN_TRANSCRIPT" '[gpio] pin 2 = HIGH'
require_contains "$MAIN_TRANSCRIPT" '[gpio] pin 2 = LOW'
require_contains "$MAIN_TRANSCRIPT" '2048'
require_contains "$MAIN_TRANSCRIPT" 'O'
require_contains "$MAIN_TRANSCRIPT" '102'
require_not_contains "$MAIN_TRANSCRIPT" 'eval error ('
require_not_contains "$MAIN_TRANSCRIPT" 'parse error ('
