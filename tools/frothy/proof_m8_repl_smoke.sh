#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BINARY="${FROTHY_BINARY:-$ROOT_DIR/build/Frothy}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/frothy-m8.XXXXXX")"

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

MAIN_TRANSCRIPT="$(
  run_transcript \
    'to inc with x' \
    '[ x + 1 ]' \
    'inc 4' \
    '1 / 0' \
    '2' \
    'set = 1' \
    '3' \
    'quit'
)"
printf '%s\n' "$MAIN_TRANSCRIPT"

require_contains "$MAIN_TRANSCRIPT" 'frothy> .. frothy> 5'
require_contains "$MAIN_TRANSCRIPT" 'eval error ('
require_contains "$MAIN_TRANSCRIPT" 'frothy> 2'
require_contains "$MAIN_TRANSCRIPT" 'parse error ('
require_contains "$MAIN_TRANSCRIPT" 'frothy> 3'

if [ "$(count_occurrences "$MAIN_TRANSCRIPT" 'parse error (')" -ne 1 ]; then
  echo "error: expected exactly one parse error in main transcript" >&2
  exit 1
fi

if [ "$(count_occurrences "$MAIN_TRANSCRIPT" 'eval error (')" -ne 1 ]; then
  echo "error: expected exactly one eval error in main transcript" >&2
  exit 1
fi

PAREN_TRANSCRIPT="$(
  run_transcript \
    '(1 +' \
    '2)' \
    'quit'
)"
printf '%s\n' "$PAREN_TRANSCRIPT"
require_contains "$PAREN_TRANSCRIPT" 'frothy> .. 3'
require_not_contains "$PAREN_TRANSCRIPT" 'parse error ('

BRACKET_TRANSCRIPT="$(
  run_transcript \
    'frame = cells(1)' \
    'frame[0' \
    ']' \
    'quit'
)"
printf '%s\n' "$BRACKET_TRANSCRIPT"
require_contains "$BRACKET_TRANSCRIPT" 'frothy> frothy> .. nil'
require_not_contains "$BRACKET_TRANSCRIPT" 'parse error ('

STRING_TRANSCRIPT="$(
  run_transcript \
    'label = "hel' \
    'lo"' \
    '2' \
    'quit'
)"
printf '%s\n' "$STRING_TRANSCRIPT"
require_contains "$STRING_TRANSCRIPT" 'frothy> .. frothy> 2'
require_not_contains "$STRING_TRANSCRIPT" 'parse error ('

STRING_BACKSLASH_TRANSCRIPT="$(
  run_transcript \
    'label = "a' \
    '\\' \
    'b"' \
    'label' \
    'quit'
)"
printf '%s\n' "$STRING_BACKSLASH_TRANSCRIPT"
require_contains "$STRING_BACKSLASH_TRANSCRIPT" 'frothy> .. .. frothy> "a\n\\\nb"'
require_not_contains "$STRING_BACKSLASH_TRANSCRIPT" 'parse error ('

NOT_TRANSCRIPT="$(
  run_transcript \
    'not' \
    'true' \
    'quit'
)"
printf '%s\n' "$NOT_TRANSCRIPT"
require_contains "$NOT_TRANSCRIPT" 'frothy> .. false'
require_not_contains "$NOT_TRANSCRIPT" 'parse error ('

COMMENT_TRANSCRIPT="$(
  run_transcript \
    'count() =' \
    '\\ comment' \
    '9' \
    'count()' \
    'quit'
)"
printf '%s\n' "$COMMENT_TRANSCRIPT"
require_contains "$COMMENT_TRANSCRIPT" 'frothy> .. .. frothy> 9'
require_not_contains "$COMMENT_TRANSCRIPT" 'parse error ('

EQUAL_TRANSCRIPT="$(
  run_transcript \
    'count() =' \
    '9' \
    'count()' \
    'quit'
)"
printf '%s\n' "$EQUAL_TRANSCRIPT"
require_contains "$EQUAL_TRANSCRIPT" 'frothy> .. frothy> 9'
require_not_contains "$EQUAL_TRANSCRIPT" 'parse error ('

IS_TRANSCRIPT="$(
  run_transcript \
    'count is' \
    '9' \
    'count' \
    'quit'
)"
printf '%s\n' "$IS_TRANSCRIPT"
require_contains "$IS_TRANSCRIPT" 'frothy> .. frothy> 9'
require_not_contains "$IS_TRANSCRIPT" 'parse error ('

FN_TRANSCRIPT="$(
  run_transcript \
    'fn with x, y' \
    '[ x + y ]' \
    'quit'
)"
printf '%s\n' "$FN_TRANSCRIPT"
require_contains "$FN_TRANSCRIPT" 'frothy> .. <fn/2>'
require_not_contains "$FN_TRANSCRIPT" 'parse error ('

OPERATOR_TRANSCRIPT="$(
  run_transcript \
    '1 +' \
    '2' \
    'quit'
)"
printf '%s\n' "$OPERATOR_TRANSCRIPT"
require_contains "$OPERATOR_TRANSCRIPT" 'frothy> .. 3'
require_not_contains "$OPERATOR_TRANSCRIPT" 'parse error ('

COMPARE_TRANSCRIPT="$(
  run_transcript \
    '1 ==' \
    '1' \
    'quit'
)"
printf '%s\n' "$COMPARE_TRANSCRIPT"
require_contains "$COMPARE_TRANSCRIPT" 'frothy> .. true'
require_not_contains "$COMPARE_TRANSCRIPT" 'parse error ('

UNARY_MINUS_TRANSCRIPT="$(
  run_transcript \
    'count() = -' \
    '1' \
    'count()' \
    'quit'
)"
printf '%s\n' "$UNARY_MINUS_TRANSCRIPT"
require_contains "$UNARY_MINUS_TRANSCRIPT" 'frothy> .. frothy> -1'
require_not_contains "$UNARY_MINUS_TRANSCRIPT" 'parse error ('

WHEN_TRANSCRIPT="$(
  run_transcript \
    'when true' \
    '[ 42 ]' \
    'quit'
)"
printf '%s\n' "$WHEN_TRANSCRIPT"
require_contains "$WHEN_TRANSCRIPT" 'frothy> .. 42'
require_not_contains "$WHEN_TRANSCRIPT" 'parse error ('

REPEAT_TRANSCRIPT="$(
  run_transcript \
    'repeat 3 as i' \
    '[ i ]' \
    'quit'
)"
printf '%s\n' "$REPEAT_TRANSCRIPT"
require_contains "$REPEAT_TRANSCRIPT" 'frothy> .. nil'
require_not_contains "$REPEAT_TRANSCRIPT" 'parse error ('

UNLESS_TRANSCRIPT="$(
  run_transcript \
    'unless false' \
    '[ 42 ]' \
    'quit'
)"
printf '%s\n' "$UNLESS_TRANSCRIPT"
require_contains "$UNLESS_TRANSCRIPT" 'frothy> .. 42'
require_not_contains "$UNLESS_TRANSCRIPT" 'parse error ('

AND_OR_TRANSCRIPT="$(
  run_transcript \
    'false and' \
    'true' \
    'true or' \
    'false' \
    'quit'
)"
printf '%s\n' "$AND_OR_TRANSCRIPT"
require_contains "$AND_OR_TRANSCRIPT" 'frothy> .. false'
require_contains "$AND_OR_TRANSCRIPT" 'frothy> .. true'
require_not_contains "$AND_OR_TRANSCRIPT" 'parse error ('

CALL_HEADER_TRANSCRIPT="$(
  run_transcript \
    'makeInc is fn [ fn with x [ x + 1 ] ]' \
    'call' \
    'makeInc: with 41' \
    'quit'
)"
printf '%s\n' "$CALL_HEADER_TRANSCRIPT"
require_contains "$CALL_HEADER_TRANSCRIPT" 'frothy> frothy> .. 42'
require_not_contains "$CALL_HEADER_TRANSCRIPT" 'parse error ('

COLON_CONTINUATION_TRANSCRIPT="$(
  run_transcript \
    'to inc with x [ x + 1 ]' \
    'inc:' \
    '41' \
    'quit'
)"
printf '%s\n' "$COLON_CONTINUATION_TRANSCRIPT"
require_contains "$COLON_CONTINUATION_TRANSCRIPT" 'frothy> frothy> .. 42'
require_not_contains "$COLON_CONTINUATION_TRANSCRIPT" 'eval error (108)'
require_not_contains "$COLON_CONTINUATION_TRANSCRIPT" 'parse error ('

CHAINED_MINUS_TRANSCRIPT="$(
  run_transcript \
    '1 + -' \
    '2' \
    'quit'
)"
printf '%s\n' "$CHAINED_MINUS_TRANSCRIPT"
require_contains "$CHAINED_MINUS_TRANSCRIPT" 'frothy> .. -1'
require_not_contains "$CHAINED_MINUS_TRANSCRIPT" 'parse error ('

COMMA_TRANSCRIPT="$(
  run_transcript \
    '1,' \
    '2' \
    'quit'
)"
printf '%s\n' "$COMMA_TRANSCRIPT"
require_contains "$COMMA_TRANSCRIPT" 'frothy> .. parse error ('
require_not_contains "$COMMA_TRANSCRIPT" 'frothy> parse error ('
require_not_contains "$COMMA_TRANSCRIPT" 'frothy> 2'
if [ "$(count_occurrences "$COMMA_TRANSCRIPT" 'parse error (')" -ne 1 ]; then
  echo "error: expected exactly one parse error in comma transcript" >&2
  exit 1
fi

CALL_TRANSCRIPT="$(
  run_transcript \
    'count() = 7' \
    'count:' \
    'makeInc is fn [ fn with x [ x + 1 ] ]' \
    'call makeInc: with 41' \
    'blink = fn(a, b) { a + b }' \
    'blink 4, 5' \
    'blink -1, 2' \
    'tick.on = fn() { 7 }' \
    'tick.on' \
    'quit'
)"
printf '%s\n' "$CALL_TRANSCRIPT"
require_contains "$CALL_TRANSCRIPT" 'frothy> frothy> 7'
require_contains "$CALL_TRANSCRIPT" 'frothy> 42'
require_contains "$CALL_TRANSCRIPT" 'frothy> frothy> 9'
require_contains "$CALL_TRANSCRIPT" 'frothy> 1'
require_contains "$CALL_TRANSCRIPT" 'frothy> frothy> 7'
require_not_contains "$CALL_TRANSCRIPT" 'parse error ('
