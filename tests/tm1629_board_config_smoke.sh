#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
  echo "usage: $0 <cmake> <source_dir> <board> <platform>" >&2
  exit 2
fi

CMAKE_BIN=$1
SOURCE_DIR=$2
BOARD=$3
PLATFORM=$4
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/frothy-tm1629.XXXXXX")

cleanup() {
  rm -rf "$BUILD_DIR"
}
trap cleanup EXIT INT TERM

"$CMAKE_BIN" -S "$SOURCE_DIR" -B "$BUILD_DIR" \
  -DFROTH_BOARD="$BOARD" \
  -DFROTH_PLATFORM="$PLATFORM" \
  -DFROTHY_BUILD_HOST=ON

"$CMAKE_BIN" --build "$BUILD_DIR" --target \
  frothy_eval_tests \
  frothy_snapshot_tests \
  frothy_tm1629_runtime_tests \
  frothy_tm1629_board_tests

ctest --test-dir "$BUILD_DIR" --output-on-failure \
  -R '^(frothy_eval|frothy_snapshot|frothy_tm1629_(runtime|board))$'
