#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
  echo "usage: $0 <cmake> <source_dir> <board>" >&2
  exit 2
fi

CMAKE_BIN=$1
SOURCE_DIR=$2
BOARD=$3
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/frothy-esp-idf-build.XXXXXX")
FIXTURE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/frothy-esp-idf-src.XXXXXX")

cleanup() {
  rm -rf "$BUILD_DIR" "$FIXTURE_DIR"
}
trap cleanup EXIT INT TERM

cat >"$FIXTURE_DIR/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.23)
project(frothy_esp_idf_board_config_smoke C)

include(CMakeParseArguments)

if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED BOARD_UNDER_TEST OR BOARD_UNDER_TEST STREQUAL "")
  message(FATAL_ERROR "BOARD_UNDER_TEST is required")
endif()

set(FROTH_BOARD "${BOARD_UNDER_TEST}")
set(COMPONENT_LIB frothy_esp_idf_component)
set(ESP_IDF_MAIN_DIR "${SOURCE_DIR}/targets/esp-idf/main")

function(idf_component_register)
  set(options)
  set(one_value_args)
  set(multi_value_args SRCS INCLUDE_DIRS REQUIRES)
  cmake_parse_arguments(IDF "${options}" "${one_value_args}"
                        "${multi_value_args}" ${ARGN})
  set(RESOLVED_SRCS)
  set(RESOLVED_INCLUDES)

  foreach(SRC IN LISTS IDF_SRCS)
    if(IS_ABSOLUTE "${SRC}")
      list(APPEND RESOLVED_SRCS "${SRC}")
    else()
      list(APPEND RESOLVED_SRCS "${ESP_IDF_MAIN_DIR}/${SRC}")
    endif()
  endforeach()
  foreach(INCLUDE_DIR IN LISTS IDF_INCLUDE_DIRS)
    if(IS_ABSOLUTE "${INCLUDE_DIR}")
      list(APPEND RESOLVED_INCLUDES "${INCLUDE_DIR}")
    else()
      list(APPEND RESOLVED_INCLUDES "${ESP_IDF_MAIN_DIR}/${INCLUDE_DIR}")
    endif()
  endforeach()

  add_library(${COMPONENT_LIB} STATIC ${RESOLVED_SRCS})
  target_include_directories(${COMPONENT_LIB} PRIVATE ${RESOLVED_INCLUDES})
endfunction()

include("${SOURCE_DIR}/targets/esp-idf/main/CMakeLists.txt")

get_target_property(SMOKE_SOURCES ${COMPONENT_LIB} SOURCES)
get_target_property(SMOKE_DEFINITIONS ${COMPONENT_LIB} COMPILE_DEFINITIONS)

set(SMOKE_SOURCE_SET ";${SMOKE_SOURCES};")
set(SMOKE_DEFINITION_SET ";${SMOKE_DEFINITIONS};")

if(NOT SMOKE_SOURCE_SET MATCHES "(/|;)frothy_tm1629\\.c(;|$)")
  message(FATAL_ERROR "esp-idf target is missing src/frothy_tm1629.c")
endif()
if(NOT SMOKE_SOURCE_SET MATCHES "(/|;)boards/${BOARD_UNDER_TEST}/ffi\\.c(;|$)")
  message(FATAL_ERROR "esp-idf target is missing board ffi source")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTH_HEAP_SIZE=16384;")
  message(FATAL_ERROR "esp-idf target did not ingest board heap size")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTH_SLOT_TABLE_SIZE=256;")
  message(FATAL_ERROR "esp-idf target did not ingest board slot count")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTHY_EVAL_VALUE_CAPACITY=512;")
  message(FATAL_ERROR "esp-idf target did not ingest Frothy eval value capacity")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTHY_OBJECT_CAPACITY=256;")
  message(FATAL_ERROR "esp-idf target did not ingest Frothy object capacity")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTHY_PAYLOAD_CAPACITY=65536;")
  message(FATAL_ERROR "esp-idf target did not ingest Frothy payload capacity")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTHY_PARSER_BINDING_CAPACITY=256;")
  message(FATAL_ERROR "esp-idf target did not ingest Frothy parser binding capacity")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTHY_IR_LITERAL_CAPACITY=256;")
  message(FATAL_ERROR "esp-idf target did not ingest Frothy IR literal capacity")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTHY_IR_NODE_CAPACITY=1024;")
  message(FATAL_ERROR "esp-idf target did not ingest Frothy IR node capacity")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTHY_IR_LINK_CAPACITY=1024;")
  message(FATAL_ERROR "esp-idf target did not ingest Frothy IR link capacity")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTH_HAS_BOARD_PINS;")
  message(FATAL_ERROR "esp-idf target did not embed board pin assets")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTHY_HAS_BOARD_PINS;")
  message(FATAL_ERROR "esp-idf target did not generate Frothy board pin seed")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTHY_HAS_BOARD_BASE_LIB;")
  message(FATAL_ERROR "esp-idf target did not generate Frothy board base lib")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTH_HAS_SNAPSHOTS;")
  message(FATAL_ERROR "esp-idf target did not define snapshot support")
endif()
if(NOT SMOKE_DEFINITION_SET MATCHES ";FROTH_SNAPSHOT_BLOCK_SIZE=2048;")
  message(FATAL_ERROR "esp-idf target did not ingest snapshot block size")
endif()
if(SMOKE_DEFINITION_SET MATCHES ";FROTH_HAS_SNAPSHOTS=0;")
  message(FATAL_ERROR "esp-idf target encoded snapshot support as =0")
endif()
if(SMOKE_DEFINITION_SET MATCHES ";FROTH_HAS_SNAPSHOTS=1;")
  message(FATAL_ERROR "esp-idf target encoded snapshot support as =1")
endif()

set(FROTHY_BOARD_PINS_HEADER "${CMAKE_BINARY_DIR}/frothy_board_pins.h")
if(NOT EXISTS "${FROTHY_BOARD_PINS_HEADER}")
  message(FATAL_ERROR "esp-idf target did not emit frothy_board_pins.h")
endif()
file(READ "${FROTHY_BOARD_PINS_HEADER}" FROTHY_BOARD_PINS_CONTENT)
if(NOT FROTHY_BOARD_PINS_CONTENT MATCHES "\\{\"JOY_4\", 17\\}")
  message(FATAL_ERROR "frothy_board_pins.h is missing JOY_4 pin seed")
endif()
if(NOT FROTHY_BOARD_PINS_CONTENT MATCHES "\\{\"POT_LEFT\", 33\\}")
  message(FATAL_ERROR "frothy_board_pins.h is missing POT_LEFT pin seed")
endif()

set(FROTHY_BOARD_BASE_HEADER "${CMAKE_BINARY_DIR}/frothy_board_base_lib.h")
if(NOT EXISTS "${FROTHY_BOARD_BASE_HEADER}")
  message(FATAL_ERROR "esp-idf target did not emit frothy_board_base_lib.h")
endif()
set(FROTHY_BOARD_BASE_SOURCE
    "${SOURCE_DIR}/boards/${BOARD_UNDER_TEST}/lib/base.frothy")
if(NOT EXISTS "${FROTHY_BOARD_BASE_SOURCE}")
  message(FATAL_ERROR "esp-idf target board is missing lib/base.frothy")
endif()
file(READ "${FROTHY_BOARD_BASE_SOURCE}" EXPECTED_BASE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, "
       EXPECTED_BASE_BYTES "${EXPECTED_BASE_HEX}")
set(EXPECTED_BASE_CONTENT
    "#pragma once\nstatic const char frothy_board_base_lib[] = {${EXPECTED_BASE_BYTES}0x00};\n")
file(READ "${FROTHY_BOARD_BASE_HEADER}" ACTUAL_BASE_CONTENT)
if(NOT ACTUAL_BASE_CONTENT STREQUAL EXPECTED_BASE_CONTENT)
  message(FATAL_ERROR "frothy_board_base_lib.h does not match board lib/base.frothy")
endif()

string(MAKE_C_IDENTIFIER "${COMPONENT_LIB}" COMPONENT_LIB_ID)
if(NOT TARGET "froth_board_pins_${COMPONENT_LIB_ID}")
  message(FATAL_ERROR "esp-idf target did not register board pin embed target")
endif()
EOF

"$CMAKE_BIN" -S "$FIXTURE_DIR" -B "$BUILD_DIR" \
  -DSOURCE_DIR="$SOURCE_DIR" \
  -DBOARD_UNDER_TEST="$BOARD"

ESP_IDF_CMAKE="$SOURCE_DIR/targets/esp-idf/main/CMakeLists.txt"
if ! grep -q 'if(FROTH_BOARD_CONFIG_HAS_SNAPSHOTS)' "$ESP_IDF_CMAKE"; then
  echo "esp-idf CMake is missing conditional snapshot gating" >&2
  exit 1
fi
if grep -q 'FROTH_HAS_SNAPSHOTS=' "$ESP_IDF_CMAKE"; then
  echo "esp-idf CMake still encodes FROTH_HAS_SNAPSHOTS as =0/=1" >&2
  exit 1
fi
