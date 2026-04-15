#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BINARY="${FROTHY_BINARY:-$ROOT_DIR/build/Frothy}"
CLI_BIN="${FROTHY_CLI_BINARY:-$ROOT_DIR/tools/cli/froth-cli}"
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

if [ ! -x "$CLI_BIN" ]; then
  echo "error: $CLI_BIN is missing; run make build" >&2
  exit 1
fi

for proof_file in \
  "$ROOT_DIR/tools/frothy/proof_m10_blink.frothy" \
  "$ROOT_DIR/tools/frothy/proof_m10_boot_persist.frothy" \
  "$ROOT_DIR/tools/frothy/proof_m10_cells_adc.frothy" \
  "$ROOT_DIR/tools/frothy/proof_m10_workshop_surface.frothy" \
  "$ROOT_DIR/tools/frothy/proof_m10_workshop_starter_checks.frothy" \
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

require_count_exact() {
  transcript=$1
  needle=$2
  expected=$3
  actual=$(printf '%s\n' "$transcript" | grep -F -c "$needle")
  if [ "$actual" -ne "$expected" ]; then
    echo "error: expected exactly $expected occurrences of: $needle (got $actual)" >&2
    exit 1
  fi
}

require_file_contains() {
  path=$1
  needle=$2
  if ! grep -F "$needle" "$path" >/dev/null; then
    echo "error: expected $path to contain: $needle" >&2
    exit 1
  fi
}

require_sequence() {
  transcript=$1
  shift
  printf '%s\0' "$@" | env TRANSCRIPT="$transcript" awk '
    BEGIN {
      RS = "\0"
      text = ENVIRON["TRANSCRIPT"]
      index_pos = 1
    }
    {
      segment = substr(text, index_pos)
      next_index = index(segment, $0)
      if (next_index == 0) {
        printf("error: expected transcript sequence element: %s\n", $0) > "/dev/stderr"
        exit 1
      }
      index_pos += next_index - 1 + length($0)
    }
  '
}

STARTER_DIR="$WORK_DIR/workshop-starter"
STARTER_OUTPUT="$("$CLI_BIN" new --target esp32-devkit-v1 "$STARTER_DIR")"
printf '%s\n' "$STARTER_OUTPUT"
require_contains "$STARTER_OUTPUT" 'Created project workshop-starter'
require_contains "$STARTER_OUTPUT" 'target: esp32-devkit-v1 (esp-idf)'
require_contains "$STARTER_OUTPUT" 'froth doctor'
require_contains "$STARTER_OUTPUT" 'froth flash'
for starter_file in \
  "$STARTER_DIR/src/main.froth" \
  "$STARTER_DIR/src/workshop/lesson.froth" \
  "$STARTER_DIR/src/workshop/game.froth"
do
  if [ ! -f "$starter_file" ]; then
    echo "error: missing workshop starter file: $starter_file" >&2
    exit 1
  fi
done
require_file_contains "$STARTER_DIR/src/main.froth" '\ #use "./workshop/lesson.froth"'
require_file_contains "$STARTER_DIR/src/main.froth" '\ #use "./workshop/game.froth"'
require_file_contains "$STARTER_DIR/src/main.froth" 'to boot ['
require_file_contains "$STARTER_DIR/src/workshop/lesson.froth" '\ #allow-toplevel'
require_file_contains "$STARTER_DIR/src/workshop/lesson.froth" 'to lesson.ready ['
require_file_contains "$STARTER_DIR/src/workshop/lesson.froth" 'to lesson.animate with step ['
require_file_contains "$STARTER_DIR/src/workshop/game.froth" '\ #allow-toplevel'
require_file_contains "$STARTER_DIR/src/workshop/game.froth" 'player is cells(2)'
require_file_contains "$STARTER_DIR/src/workshop/game.froth" 'to game.capture ['
STARTER_PROOF="$WORK_DIR/workshop-starter-proof.frothy"
STARTER_PROOF_WARNINGS="$WORK_DIR/workshop-starter-proof.stderr"
"$CLI_BIN" tooling resolve-source "$STARTER_DIR/src/main.froth" \
  > "$STARTER_PROOF" \
  2> "$STARTER_PROOF_WARNINGS"
if grep -F 'warning:' "$STARTER_PROOF_WARNINGS" >/dev/null; then
  echo "error: workshop starter resolve emitted warnings" >&2
  cat "$STARTER_PROOF_WARNINGS" >&2
  exit 1
fi
printf '\n' >> "$STARTER_PROOF"
cat "$ROOT_DIR/tools/frothy/proof_m10_workshop_starter_checks.frothy" >> "$STARTER_PROOF"

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
require_not_contains "$BOOT_SETUP_TRANSCRIPT" 'eval error ('
require_not_contains "$BOOT_SETUP_TRANSCRIPT" 'parse error ('

BOOT_VERIFY_TRANSCRIPT="$(
  run_transcript \
    'note' \
    'dangerous.wipe' \
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
require_sequence "$WORKSHOP_TRANSCRIPT" \
  $'millis\n  slot: base\n  kind: native\n  call: 0 -> 1\n  owner: board ffi\n  persistence: not saved\n  effect: ( -- n )\n  help: Return wrapped monotonic uptime in milliseconds.'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  $'blink\n  slot: base\n  kind: code\n  call: 3 -> 1\n  owner: base image\n  persistence: not saved'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  $'adc.percent\n  slot: base\n  kind: code\n  call: 1 -> 1\n  owner: base image\n  persistence: not saved'
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
  $'blink\n  slot: overlay\n  kind: code\n  call: 3 -> 1\n  owner: overlay image\n  persistence: saved in snapshot' \
  'frothy> 99'
require_sequence "$WORKSHOP_TRANSCRIPT" \
  '"restored.blink.check"' \
  $'blink\n  slot: base\n  kind: code\n  call: 3 -> 1\n  owner: base image\n  persistence: not saved' \
  '[gpio] pin 2 -> OUTPUT' \
  '[gpio] pin 2 = LOW' \
  '[gpio] pin 2 = HIGH' \
  '[gpio] pin 2 = LOW'
require_not_contains "$WORKSHOP_TRANSCRIPT" 'eval error ('
require_not_contains "$WORKSHOP_TRANSCRIPT" 'parse error ('
STARTER_TRANSCRIPT="$(
  run_file "$STARTER_PROOF"
)"
printf '%s\n' "$STARTER_TRANSCRIPT"
require_sequence "$STARTER_TRANSCRIPT" \
  '"boot.check"' \
  'frothy> "Workshop starter ready"' \
  'frothy> 0' \
  'frothy> 0' \
  'frothy> 0'
require_sequence "$STARTER_TRANSCRIPT" \
  '"lesson.ready.check"' \
  '[gpio] pin 2 -> OUTPUT' \
  '[gpio] pin 2 = LOW' \
  'frothy> "Workshop starter ready"'
require_sequence "$STARTER_TRANSCRIPT" \
  '"lesson.blink.check"' \
  '[gpio] pin 2 -> OUTPUT' \
  '[gpio] pin 2 = LOW' \
  '[gpio] pin 2 = HIGH' \
  '[gpio] pin 2 = LOW'
require_sequence "$STARTER_TRANSCRIPT" \
  '"lesson.animate.check"' \
  'frothy> 7' \
  'frothy> 8' \
  'frothy> 9'
require_sequence "$STARTER_TRANSCRIPT" \
  '"game.capture.check"' \
  'frothy> 1' \
  'frothy> 1' \
  'frothy> 50'
require_sequence "$STARTER_TRANSCRIPT" \
  '"game.restore.check"' \
  'frothy> true' \
  'frothy> true' \
  'frothy> true'
require_sequence "$STARTER_TRANSCRIPT" \
  '"game.wipe.check"' \
  'frothy> eval error (4)'
require_count_exact "$STARTER_TRANSCRIPT" 'eval error (4)' 1
require_sequence "$STARTER_TRANSCRIPT" \
  '"base.recovery.check"' \
  'blink' \
  'slot: base' \
  '[gpio] pin 2 -> OUTPUT' \
  '[gpio] pin 2 = LOW' \
  '[gpio] pin 2 = HIGH' \
  '[gpio] pin 2 = LOW'
require_not_contains "$STARTER_TRANSCRIPT" 'parse error ('

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

set -- --port "$PORT"
if [ "$ASSUME_BLINK" -eq 1 ]; then
  set -- --assume-blink-confirmed "$@"
fi
if [ -n "$TRANSCRIPT_OUT" ]; then
  set -- --transcript-out "$TRANSCRIPT_OUT" "$@"
fi

is_retryable_serial_failure() {
  path=$1
  grep -E \
    'Could not exclusively lock port|device disconnected or multiple access on port|device reports readiness to read but returned no data|Waiting for the device to reconnect|port is busy or doesn'\''t exist' \
    "$path" >/dev/null
}

attempt=1
max_attempts=${FROTHY_M10_DEVICE_ATTEMPTS:-4}
while [ "$attempt" -le "$max_attempts" ]; do
  err_file="$WORK_DIR/device-proof.$attempt.stderr"
  if python3 "$ROOT_DIR/tools/frothy/proof_m10_esp32_smoke.py" "$@" 2>"$err_file"; then
    rm -f "$err_file"
    exit 0
  fi

  if [ "$attempt" -lt "$max_attempts" ] && is_retryable_serial_failure "$err_file"; then
    printf 'retry: device proof attempt %s/%s hit serial contention, retrying\n' \
      "$attempt" "$max_attempts" >&2
    attempt=$((attempt + 1))
    sleep "$attempt"
    continue
  fi

  cat "$err_file" >&2
  exit 1
done
