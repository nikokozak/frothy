#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BINARY="${FROTHY_BINARY:-$ROOT_DIR/build/Frothy}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/frothy-m10.XXXXXX")"
HOST_ONLY=0
ASSUME_BLINK=0
TRANSCRIPT_OUT=
PORT=

if [ -d /opt/homebrew/bin ]; then
  PATH=/opt/homebrew/bin:$PATH
  export PATH
fi

usage() {
  cat >&2 <<'EOF'
usage:
  proof_m10_smoke.sh --host-only
  proof_m10_smoke.sh [--assume-blink-confirmed] [--transcript-out PATH] <PORT>
EOF
}

cleanup() {
  rm -rf "$WORK_DIR"
}

trap cleanup EXIT HUP INT TERM

while [ "$#" -gt 0 ]; do
  case "$1" in
    --host-only)
      HOST_ONLY=1
      shift
      ;;
    --assume-blink-confirmed)
      ASSUME_BLINK=1
      shift
      ;;
    --transcript-out)
      if [ "$#" -lt 2 ]; then
        usage
        exit 1
      fi
      TRANSCRIPT_OUT=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      usage
      exit 1
      ;;
    *)
      PORT=$1
      shift
      ;;
  esac
done

if [ "$HOST_ONLY" -eq 1 ] && [ -n "$PORT" ]; then
  usage
  exit 1
fi

if [ "$HOST_ONLY" -ne 1 ] && [ -z "$PORT" ]; then
  usage
  exit 1
fi

if [ ! -x "$BINARY" ]; then
  echo "error: $BINARY is missing; run cmake -S . -B build && cmake --build build" >&2
  exit 1
fi

for proof_file in \
  "$ROOT_DIR/tools/frothy/proof_m10_blink.frothy" \
  "$ROOT_DIR/tools/frothy/proof_m10_boot_persist.frothy" \
  "$ROOT_DIR/tools/frothy/proof_m10_cells_adc.frothy" \
  "$ROOT_DIR/tools/frothy/proof_m10_workshop_surface.frothy" \
  "$ROOT_DIR/tools/frothy/proof_m10_esp32_smoke.py"
do
  if [ ! -f "$proof_file" ]; then
    echo "error: missing required proof artifact: $proof_file" >&2
    exit 1
  fi
done

run_transcript() {
  (
    cd "$WORK_DIR"
    printf '%s\n' "$@" | "$BINARY"
  )
}

run_file() {
  (
    cd "$WORK_DIR"
    {
      cat "$1"
      printf 'quit\n'
    } | "$BINARY"
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

require_count_at_least() {
  transcript=$1
  needle=$2
  expected=$3
  actual=$(printf '%s\n' "$transcript" | grep -F -c "$needle")
  if [ "$actual" -lt "$expected" ]; then
    echo "error: expected at least $expected occurrences of: $needle (got $actual)" >&2
    exit 1
  fi
}

require_sequence() {
  transcript=$1
  shift
  TRANSCRIPT=$transcript python3 - "$@" <<'PY'
import os
import sys

text = os.environ["TRANSCRIPT"].replace("\r\n", "\n")
index = 0
for needle in sys.argv[1:]:
    next_index = text.find(needle, index)
    if next_index < 0:
        sys.stderr.write(f"error: expected transcript sequence element: {needle}\n")
        raise SystemExit(1)
    index = next_index + len(needle)
PY
}

BLINK_TRANSCRIPT="$(run_file "$ROOT_DIR/tools/frothy/proof_m10_blink.frothy")"
printf '%s\n' "$BLINK_TRANSCRIPT"
require_contains "$BLINK_TRANSCRIPT" 'Frothy shell'
require_contains "$BLINK_TRANSCRIPT" 'Type help for commands.'
require_contains "$BLINK_TRANSCRIPT" '[gpio] pin 2 -> OUTPUT'
require_count_at_least "$BLINK_TRANSCRIPT" '[gpio] pin 2 = HIGH' 3
require_count_at_least "$BLINK_TRANSCRIPT" '[gpio] pin 2 = LOW' 3
require_not_contains "$BLINK_TRANSCRIPT" 'eval error ('
require_not_contains "$BLINK_TRANSCRIPT" 'parse error ('

BOOT_SETUP_TRANSCRIPT="$(run_file "$ROOT_DIR/tools/frothy/proof_m10_boot_persist.frothy")"
printf '%s\n' "$BOOT_SETUP_TRANSCRIPT"
require_contains "$BOOT_SETUP_TRANSCRIPT" 'snapshot: none'
require_contains "$BOOT_SETUP_TRANSCRIPT" 'nil'
require_not_contains "$BOOT_SETUP_TRANSCRIPT" 'eval error ('
require_not_contains "$BOOT_SETUP_TRANSCRIPT" 'parse error ('

BOOT_VERIFY_TRANSCRIPT="$(
  run_transcript \
    'note' \
    'wipe()' \
    'note' \
    'quit'
)"
printf '%s\n' "$BOOT_VERIFY_TRANSCRIPT"
require_contains "$BOOT_VERIFY_TRANSCRIPT" 'snapshot: found'
require_contains "$BOOT_VERIFY_TRANSCRIPT" '[gpio] pin 2 -> OUTPUT'
require_contains "$BOOT_VERIFY_TRANSCRIPT" '[gpio] pin 2 = HIGH'
require_contains "$BOOT_VERIFY_TRANSCRIPT" '[gpio] pin 2 = LOW'
require_contains "$BOOT_VERIFY_TRANSCRIPT" '"booted"'
require_contains "$BOOT_VERIFY_TRANSCRIPT" 'eval error (4)'
require_not_contains "$BOOT_VERIFY_TRANSCRIPT" 'parse error ('

CELLS_TRANSCRIPT="$(run_file "$ROOT_DIR/tools/frothy/proof_m10_cells_adc.frothy")"
printf '%s\n' "$CELLS_TRANSCRIPT"
require_contains "$CELLS_TRANSCRIPT" 'Frothy shell'
require_count_at_least "$CELLS_TRANSCRIPT" '2048' 4
require_not_contains "$CELLS_TRANSCRIPT" 'eval error ('
require_not_contains "$CELLS_TRANSCRIPT" 'parse error ('

WORKSHOP_TRANSCRIPT="$(
  run_file "$ROOT_DIR/tools/frothy/proof_m10_workshop_surface.frothy"
)"
printf '%s\n' "$WORKSHOP_TRANSCRIPT"
require_contains "$WORKSHOP_TRANSCRIPT" 'millis | base | native | non-persistable | foreign'
require_count_at_least "$WORKSHOP_TRANSCRIPT" 'blink | base | code | persistable | user' 2
require_contains "$WORKSHOP_TRANSCRIPT" 'adc.percent | base | code | persistable | user'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"millis.check"' \
  'frothy> true'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"gpio.low.check"' \
  '[gpio] pin 2 = LOW' \
  'frothy> 0'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"gpio.high.check"' \
  '[gpio] pin 2 = HIGH' \
  'frothy> 1'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"gpio.toggle.check"' \
  '[gpio] pin 2 = LOW' \
  'frothy> 0'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"led.off.check"' \
  '[gpio] pin 2 -> OUTPUT' \
  '[gpio] pin 2 = LOW' \
  'frothy> 0'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"led.on.check"' \
  '[gpio] pin 2 -> OUTPUT' \
  '[gpio] pin 2 = HIGH' \
  'frothy> 1'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"led.toggle.check"' \
  '[gpio] pin 2 -> OUTPUT' \
  '[gpio] pin 2 = LOW' \
  'frothy> 0'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"adc.percent.check"' \
  'frothy> 50'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"anim.check"' \
  'frothy> 41' \
  'frothy> 42' \
  'frothy> 43'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"overlay.blink.check"' \
  'blink | overlay | code | persistable | user' \
  'frothy> 99'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"restored.blink.check"' \
  'blink | base | code | persistable | user' \
  '[gpio] pin 2 -> OUTPUT' \
  '[gpio] pin 2 = LOW' \
  '[gpio] pin 2 = HIGH' \
  '[gpio] pin 2 = LOW' \
  'nil'
require_not_contains "$WORKSHOP_TRANSCRIPT" 'eval error ('
require_not_contains "$WORKSHOP_TRANSCRIPT" 'parse error ('

if [ -e "$WORK_DIR/froth_a.snap" ] || [ -e "$WORK_DIR/froth_b.snap" ]; then
  echo "error: host preflight left snapshot files behind" >&2
  exit 1
fi

if [ "$HOST_ONLY" -eq 1 ]; then
  exit 0
fi

if [ ! -e "$PORT" ]; then
  echo "error: serial port is missing: $PORT" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "error: python3 is required for the ESP32 proof path" >&2
  exit 1
fi

if [ ! -f "$HOME/.froth/sdk/esp-idf/export.sh" ]; then
  echo "error: missing ESP-IDF export script at $HOME/.froth/sdk/esp-idf/export.sh" >&2
  exit 1
fi

PYTHON_ARGS="--port $PORT"
if [ "$ASSUME_BLINK" -eq 1 ]; then
  PYTHON_ARGS="--assume-blink-confirmed $PYTHON_ARGS"
fi
if [ -n "$TRANSCRIPT_OUT" ]; then
  PYTHON_ARGS="--transcript-out $TRANSCRIPT_OUT $PYTHON_ARGS"
fi

exec python3 "$ROOT_DIR/tools/frothy/proof_m10_esp32_smoke.py" $PYTHON_ARGS
