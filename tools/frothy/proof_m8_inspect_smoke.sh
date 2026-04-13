#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BINARY="${FROTHY_BINARY:-$ROOT_DIR/build/Frothy}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/frothy-m8-inspect.XXXXXX")"

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

count_occurrences() {
  transcript=$1
  needle=$2
  printf '%s\n' "$transcript" |
    awk -v needle="$needle" 'index($0, needle) { count++ } END { print count + 0 }'
}

require_not_contains_line() {
  transcript=$1
  needle=$2
  if printf '%s\n' "$transcript" | grep -Fx "$needle" >/dev/null; then
    echo "error: transcript unexpectedly contained line: $needle" >&2
    exit 1
  fi
}

HELP_TRANSCRIPT="$(
  run_transcript \
    'help' \
    'quit'
)"
printf '%s\n' "$HELP_TRANSCRIPT"
require_contains "$HELP_TRANSCRIPT" 'words'
require_contains "$HELP_TRANSCRIPT" 'see @name'
require_contains "$HELP_TRANSCRIPT" 'core @name'
require_contains "$HELP_TRANSCRIPT" 'info @name'
require_contains "$HELP_TRANSCRIPT" 'save'
require_contains "$HELP_TRANSCRIPT" 'restore'
require_contains "$HELP_TRANSCRIPT" 'wipe'
require_contains "$HELP_TRANSCRIPT" '.control'
require_contains "$HELP_TRANSCRIPT" 'quit'
require_contains "$HELP_TRANSCRIPT" 'exit'

WORDS_TRANSCRIPT="$(
  run_transcript \
    'inc = fn(x) { x + 1 }' \
    'words' \
    'quit'
)"
printf '%s\n' "$WORDS_TRANSCRIPT"
require_contains "$WORDS_TRANSCRIPT" 'save'
require_contains "$WORDS_TRANSCRIPT" 'restore'
require_contains "$WORDS_TRANSCRIPT" 'wipe'
require_contains "$WORDS_TRANSCRIPT" 'words'
require_contains "$WORDS_TRANSCRIPT" 'see'
require_contains "$WORDS_TRANSCRIPT" 'core'
require_contains "$WORDS_TRANSCRIPT" 'slotInfo'
require_contains "$WORDS_TRANSCRIPT" 'inc'
require_not_contains_line "$WORDS_TRANSCRIPT" 'boot'
require_not_contains "$WORDS_TRANSCRIPT" 'frothy> nil'

BOOT_WORDS_TRANSCRIPT="$(
  run_transcript \
    'boot = fn() { nil }' \
    'words' \
    'quit'
)"
printf '%s\n' "$BOOT_WORDS_TRANSCRIPT"
require_contains "$BOOT_WORDS_TRANSCRIPT" 'boot'
require_not_contains "$BOOT_WORDS_TRANSCRIPT" 'frothy> nil'

BASE_TRANSCRIPT="$(
  run_transcript \
    'see("save")' \
    'core("save")' \
    'slotInfo("save")' \
    'quit'
)"
printf '%s\n' "$BASE_TRANSCRIPT"
require_contains "$BASE_TRANSCRIPT" 'save | base | native'
require_contains "$BASE_TRANSCRIPT" '<native save/0>'
require_contains "$BASE_TRANSCRIPT" 'save | base | native | non-persistable | foreign'
require_not_contains "$BASE_TRANSCRIPT" 'eval error ('
require_not_contains "$BASE_TRANSCRIPT" 'parse error ('

INSPECT_TRANSCRIPT="$(
  run_transcript \
    'inc = fn(x) { x + 1 }' \
    'alias = inc' \
    'see("alias")' \
    'core("alias")' \
    'slotInfo("alias")' \
    'quit'
)"
printf '%s\n' "$INSPECT_TRANSCRIPT"
require_contains "$INSPECT_TRANSCRIPT" 'alias | overlay | code'
require_contains "$INSPECT_TRANSCRIPT" '(fn arity=1 locals=1 (seq (call (builtin "+") (read-local 0) (lit 1))))'
require_contains "$INSPECT_TRANSCRIPT" 'alias | overlay | code | persistable | user'
require_not_contains "$INSPECT_TRANSCRIPT" 'eval error ('
require_not_contains "$INSPECT_TRANSCRIPT" 'parse error ('

REBIND_TRANSCRIPT="$(
  run_transcript \
    'see = fn() { 42 }' \
    'slotInfo("see")' \
    'see()' \
    'quit'
)"
printf '%s\n' "$REBIND_TRANSCRIPT"
require_contains "$REBIND_TRANSCRIPT" 'see | overlay | code | persistable | user'
require_contains "$REBIND_TRANSCRIPT" '42'
require_not_contains "$REBIND_TRANSCRIPT" 'eval error ('
require_not_contains "$REBIND_TRANSCRIPT" 'parse error ('

COMMAND_TRANSCRIPT="$(
  run_transcript \
    'alias = fn() { 42 }' \
    'see @alias' \
    'core @alias' \
    'info @alias' \
    'note = "saved"' \
    'save' \
    'note = "changed"' \
    'restore' \
    'note' \
    'wipe' \
    'note' \
    'quit'
)"
printf '%s\n' "$COMMAND_TRANSCRIPT"
require_contains "$COMMAND_TRANSCRIPT" 'alias | overlay | code'
require_contains "$COMMAND_TRANSCRIPT" 'alias | overlay | code | persistable | user'
require_contains "$COMMAND_TRANSCRIPT" 'frothy> "saved"'
require_contains "$COMMAND_TRANSCRIPT" 'eval error ('
require_not_contains "$COMMAND_TRANSCRIPT" 'frothy> nil'

CALLABLE_TRANSCRIPT="$(
  run_transcript \
    'note = "saved"' \
    'save()' \
    'note = "changed"' \
    'restore()' \
    'note' \
    'wipe()' \
    'note' \
    'see("save")' \
    'quit'
)"
printf '%s\n' "$CALLABLE_TRANSCRIPT"
require_contains "$CALLABLE_TRANSCRIPT" 'save | base | native'
require_contains "$CALLABLE_TRANSCRIPT" 'frothy> "saved"'
require_contains "$CALLABLE_TRANSCRIPT" 'eval error ('
if [ "$(count_occurrences "$CALLABLE_TRANSCRIPT" 'frothy> nil')" -ne 3 ]; then
  echo 'error: expected exactly three raw nils in callable transcript' >&2
  exit 1
fi
