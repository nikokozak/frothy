#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
  echo "usage: $0 <cmake> <source_dir> <board> <platform> <ffi_config>" >&2
  exit 2
fi

CMAKE_BIN=$1
SOURCE_DIR=$2
BOARD=$3
PLATFORM=$4
FFI_CONFIG=$5
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/frothy-project-ffi.XXXXXX")

cleanup() {
  rm -rf "$BUILD_DIR"
}
trap cleanup EXIT INT TERM

"$CMAKE_BIN" -S "$SOURCE_DIR" -B "$BUILD_DIR" \
  -DFROTH_BOARD="$BOARD" \
  -DFROTH_PLATFORM="$PLATFORM" \
  -DFROTH_PROJECT_FFI_CONFIG="$FFI_CONFIG" \
  -DFROTHY_BUILD_HOST=ON

"$CMAKE_BIN" --build "$BUILD_DIR" --target Frothy
