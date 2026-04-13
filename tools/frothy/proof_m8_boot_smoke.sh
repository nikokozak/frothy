#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BINARY="${FROTHY_BINARY:-$ROOT_DIR/build/Frothy}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/frothy-m8-boot.XXXXXX")"

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

SETUP_TRANSCRIPT="$(
  run_transcript \
    'note = nil' \
    'boot = fn() { see("save") set note = "booted" }' \
    'save()' \
    'quit'
)"
printf '%s\n' "$SETUP_TRANSCRIPT"
require_contains "$SETUP_TRANSCRIPT" 'snapshot: none'

BOOT_TRANSCRIPT="$(
  run_transcript \
    'note' \
    'restore()' \
    'note' \
    'quit'
)"
printf '%s\n' "$BOOT_TRANSCRIPT"
require_contains "$BOOT_TRANSCRIPT" 'snapshot: found'
require_contains "$BOOT_TRANSCRIPT" 'save | base | native'
require_contains "$BOOT_TRANSCRIPT" '<native save/0>'
require_contains "$BOOT_TRANSCRIPT" '"booted"'
require_not_contains() {
  transcript=$1
  needle=$2
  if printf '%s\n' "$transcript" | grep -F "$needle" >/dev/null; then
    echo "error: transcript unexpectedly contained: $needle" >&2
    exit 1
  fi
}
require_not_contains "$BOOT_TRANSCRIPT" 'eval error ('

SAVE_LINE="$(printf '%s\n' "$BOOT_TRANSCRIPT" | grep -n -F 'save | base | native' | head -n1 | cut -d: -f1)"
FIRST_BOOTED_LINE="$(printf '%s\n' "$BOOT_TRANSCRIPT" | grep -n -F 'frothy> "booted"' | head -n1 | cut -d: -f1)"
FIRST_NIL_LINE="$(printf '%s\n' "$BOOT_TRANSCRIPT" | grep -n -F 'frothy> nil' | head -n1 | cut -d: -f1)"
if [ -z "$SAVE_LINE" ] || [ -z "$FIRST_BOOTED_LINE" ] || [ "$SAVE_LINE" -ge "$FIRST_BOOTED_LINE" ]; then
  echo 'error: expected boot output before the first prompt result' >&2
  exit 1
fi
if [ -z "$FIRST_NIL_LINE" ] || [ "$FIRST_BOOTED_LINE" -ge "$FIRST_NIL_LINE" ]; then
  echo 'error: expected the first post-startup note read to succeed before restore()' >&2
  exit 1
fi

RESTORE_COUNT="$(printf '%s\n' "$BOOT_TRANSCRIPT" | grep -c -F 'save | base | native')"
if [ "$RESTORE_COUNT" -ne 1 ]; then
  echo "error: expected boot hook to run exactly once at startup, got $RESTORE_COUNT" >&2
  exit 1
fi
