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
set(NORMALIZED_SMOKE_SOURCES)
foreach(SMOKE_SOURCE IN LISTS SMOKE_SOURCES)
  cmake_path(NORMAL_PATH SMOKE_SOURCE OUTPUT_VARIABLE NORMALIZED_SMOKE_SOURCE)
  list(APPEND NORMALIZED_SMOKE_SOURCES "${NORMALIZED_SMOKE_SOURCE}")
endforeach()

set(BOARD_JSON_PATH "${SOURCE_DIR}/boards/${BOARD_UNDER_TEST}/board.json")
if(NOT EXISTS "${BOARD_JSON_PATH}")
  message(FATAL_ERROR "board.json missing for ${BOARD_UNDER_TEST}")
endif()
file(READ "${BOARD_JSON_PATH}" BOARD_JSON_CONTENT)

function(expect_source source_path label)
  cmake_path(NORMAL_PATH source_path OUTPUT_VARIABLE normalized_source_path)
  list(FIND NORMALIZED_SMOKE_SOURCES "${normalized_source_path}" source_index)
  if(source_index EQUAL -1)
    message(FATAL_ERROR
      "${label}: ${normalized_source_path}\nactual sources: ${NORMALIZED_SMOKE_SOURCES}")
  endif()
endfunction()

function(expect_definition definition label)
  list(FIND SMOKE_DEFINITIONS "${definition}" definition_index)
  if(definition_index EQUAL -1)
    message(FATAL_ERROR "${label}: ${definition}")
  endif()
endfunction()

function(expect_macro definition label)
  list(FIND SMOKE_DEFINITIONS "${definition}" definition_index)
  if(definition_index EQUAL -1)
    message(FATAL_ERROR "${label}: ${definition}")
  endif()
endfunction()

function(board_config_or_default key default_value out_var)
  string(JSON config_value ERROR_VARIABLE config_error
         GET "${BOARD_JSON_CONTENT}" config "${key}")
  if(config_error)
    set(${out_var} "${default_value}" PARENT_SCOPE)
  else()
    set(${out_var} "${config_value}" PARENT_SCOPE)
  endif()
endfunction()

function(board_flag_or_default key default_value out_var)
  string(JSON config_value ERROR_VARIABLE config_error
         GET "${BOARD_JSON_CONTENT}" config "${key}")
  if(config_error)
    set(${out_var} "${default_value}" PARENT_SCOPE)
  elseif(config_value STREQUAL "true" OR
         config_value STREQUAL "TRUE" OR
         config_value STREQUAL "ON" OR
         config_value STREQUAL "1")
    set(${out_var} "ON" PARENT_SCOPE)
  else()
    set(${out_var} "OFF" PARENT_SCOPE)
  endif()
endfunction()

function(resolve_board_source source_entry out_var)
  if(IS_ABSOLUTE "${source_entry}")
    message(FATAL_ERROR "board source must be relative: ${source_entry}")
  endif()
  if("${source_entry}" MATCHES "(^|[\\\\/])\\.\\.([\\\\/]|$)")
    message(FATAL_ERROR "board source may not escape repo root: ${source_entry}")
  endif()

  set(board_relative "${SOURCE_DIR}/boards/${BOARD_UNDER_TEST}/${source_entry}")
  set(root_relative "${SOURCE_DIR}/${source_entry}")
  if(EXISTS "${board_relative}")
    set(${out_var} "${board_relative}" PARENT_SCOPE)
  elseif(EXISTS "${root_relative}")
    set(${out_var} "${root_relative}" PARENT_SCOPE)
  else()
    message(FATAL_ERROR
      "board ${BOARD_UNDER_TEST} declares missing source ${source_entry}")
  endif()
endfunction()

expect_source("${SOURCE_DIR}/boards/${BOARD_UNDER_TEST}/ffi.c"
              "esp-idf target is missing board ffi source")

set(TM1629_SOURCE "${SOURCE_DIR}/src/frothy_tm1629.c")
set(TM1629_DECLARED OFF)
string(JSON BOARD_SOURCE_COUNT ERROR_VARIABLE BOARD_SOURCE_ERROR
       LENGTH "${BOARD_JSON_CONTENT}" sources)
if(NOT BOARD_SOURCE_ERROR AND NOT BOARD_SOURCE_COUNT STREQUAL "0")
  math(EXPR BOARD_LAST_SOURCE_INDEX "${BOARD_SOURCE_COUNT} - 1")
  foreach(BOARD_SOURCE_INDEX RANGE 0 ${BOARD_LAST_SOURCE_INDEX})
    string(JSON BOARD_SOURCE_ENTRY GET "${BOARD_JSON_CONTENT}" sources
           ${BOARD_SOURCE_INDEX})
    resolve_board_source("${BOARD_SOURCE_ENTRY}" RESOLVED_BOARD_SOURCE)
    expect_source("${RESOLVED_BOARD_SOURCE}"
                  "esp-idf target is missing declared board source")
    if(RESOLVED_BOARD_SOURCE STREQUAL TM1629_SOURCE)
      set(TM1629_DECLARED ON)
    endif()
  endforeach()
endif()

if(TM1629_DECLARED)
  expect_source("${TM1629_SOURCE}"
                "esp-idf target is missing declared TM1629 runtime source")
else()
  cmake_path(NORMAL_PATH TM1629_SOURCE OUTPUT_VARIABLE NORMALIZED_TM1629_SOURCE)
  list(FIND NORMALIZED_SMOKE_SOURCES "${NORMALIZED_TM1629_SOURCE}" tm1629_source_index)
  if(NOT tm1629_source_index EQUAL -1)
    message(FATAL_ERROR
      "esp-idf target leaked undeclared board-specific source ${NORMALIZED_TM1629_SOURCE}")
  endif()
endif()

board_config_or_default("heap_size" "4096" EXPECTED_HEAP_SIZE)
board_config_or_default("slot_count" "128" EXPECTED_SLOT_COUNT)
board_config_or_default("frothy_eval_value_capacity" "256"
                        EXPECTED_EVAL_VALUE_CAPACITY)
board_config_or_default("frothy_object_capacity" "128"
                        EXPECTED_OBJECT_CAPACITY)
board_config_or_default("frothy_payload_capacity" "16384"
                        EXPECTED_PAYLOAD_CAPACITY)
board_config_or_default("frothy_parser_binding_capacity" "128"
                        EXPECTED_PARSER_BINDING_CAPACITY)
board_config_or_default("frothy_ir_literal_capacity" "128"
                        EXPECTED_IR_LITERAL_CAPACITY)
board_config_or_default("frothy_ir_node_capacity" "512"
                        EXPECTED_IR_NODE_CAPACITY)
board_config_or_default("frothy_ir_link_capacity" "512"
                        EXPECTED_IR_LINK_CAPACITY)
board_config_or_default("snapshot_block_size" "2048"
                        EXPECTED_SNAPSHOT_BLOCK_SIZE)
board_flag_or_default("has_snapshots" "ON" EXPECTED_HAS_SNAPSHOTS)

expect_definition("FROTH_HEAP_SIZE=${EXPECTED_HEAP_SIZE}"
                  "esp-idf target did not ingest board heap size")
expect_definition("FROTH_SLOT_TABLE_SIZE=${EXPECTED_SLOT_COUNT}"
                  "esp-idf target did not ingest board slot count")
expect_definition("FROTHY_EVAL_VALUE_CAPACITY=${EXPECTED_EVAL_VALUE_CAPACITY}"
                  "esp-idf target did not ingest Frothy eval value capacity")
expect_definition("FROTHY_OBJECT_CAPACITY=${EXPECTED_OBJECT_CAPACITY}"
                  "esp-idf target did not ingest Frothy object capacity")
expect_definition("FROTHY_PAYLOAD_CAPACITY=${EXPECTED_PAYLOAD_CAPACITY}"
                  "esp-idf target did not ingest Frothy payload capacity")
expect_definition(
  "FROTHY_PARSER_BINDING_CAPACITY=${EXPECTED_PARSER_BINDING_CAPACITY}"
  "esp-idf target did not ingest Frothy parser binding capacity")
expect_definition("FROTHY_IR_LITERAL_CAPACITY=${EXPECTED_IR_LITERAL_CAPACITY}"
                  "esp-idf target did not ingest Frothy IR literal capacity")
expect_definition("FROTHY_IR_NODE_CAPACITY=${EXPECTED_IR_NODE_CAPACITY}"
                  "esp-idf target did not ingest Frothy IR node capacity")
expect_definition("FROTHY_IR_LINK_CAPACITY=${EXPECTED_IR_LINK_CAPACITY}"
                  "esp-idf target did not ingest Frothy IR link capacity")
expect_macro("FROTH_HAS_BOARD_PINS"
             "esp-idf target did not embed board pin assets")
expect_macro("FROTHY_HAS_BOARD_PINS"
             "esp-idf target did not generate Frothy board pin seed")
expect_macro("FROTHY_HAS_BOARD_BASE_LIB"
             "esp-idf target did not generate Frothy board base lib")

if(EXPECTED_HAS_SNAPSHOTS STREQUAL "ON")
  expect_macro("FROTH_HAS_SNAPSHOTS"
               "esp-idf target did not define snapshot support")
  expect_definition("FROTH_SNAPSHOT_BLOCK_SIZE=${EXPECTED_SNAPSHOT_BLOCK_SIZE}"
                    "esp-idf target did not ingest snapshot block size")
else()
  list(FIND SMOKE_DEFINITIONS "FROTH_HAS_SNAPSHOTS" snapshot_index)
  if(NOT snapshot_index EQUAL -1)
    message(FATAL_ERROR "esp-idf target unexpectedly enabled snapshot support")
  endif()
endif()

list(FIND SMOKE_DEFINITIONS "FROTH_HAS_SNAPSHOTS=0" snapshot_zero_index)
if(NOT snapshot_zero_index EQUAL -1)
  message(FATAL_ERROR "esp-idf target encoded snapshot support as =0")
endif()
list(FIND SMOKE_DEFINITIONS "FROTH_HAS_SNAPSHOTS=1" snapshot_one_index)
if(NOT snapshot_one_index EQUAL -1)
  message(FATAL_ERROR "esp-idf target encoded snapshot support as =1")
endif()

set(FROTHY_BOARD_PINS_HEADER "${CMAKE_BINARY_DIR}/frothy_board_pins.h")
if(NOT EXISTS "${FROTHY_BOARD_PINS_HEADER}")
  message(FATAL_ERROR "esp-idf target did not emit frothy_board_pins.h")
endif()
file(READ "${FROTHY_BOARD_PINS_HEADER}" FROTHY_BOARD_PINS_CONTENT)

string(JSON BOARD_PIN_COUNT ERROR_VARIABLE BOARD_PIN_ERROR
       LENGTH "${BOARD_JSON_CONTENT}" pins)
if(BOARD_PIN_ERROR OR BOARD_PIN_COUNT STREQUAL "0")
  message(FATAL_ERROR "board ${BOARD_UNDER_TEST} is missing pins in board.json")
endif()
math(EXPR BOARD_LAST_PIN_INDEX "${BOARD_PIN_COUNT} - 1")
foreach(BOARD_PIN_INDEX RANGE 0 ${BOARD_LAST_PIN_INDEX})
  string(JSON BOARD_PIN_NAME MEMBER "${BOARD_JSON_CONTENT}" pins
         ${BOARD_PIN_INDEX})
  string(JSON BOARD_PIN_VALUE GET "${BOARD_JSON_CONTENT}" pins
         "${BOARD_PIN_NAME}")
  if(NOT FROTHY_BOARD_PINS_CONTENT MATCHES "\\{\"${BOARD_PIN_NAME}\", ${BOARD_PIN_VALUE}\\}")
    message(FATAL_ERROR
      "frothy_board_pins.h is missing ${BOARD_PIN_NAME}=${BOARD_PIN_VALUE}")
  endif()
endforeach()

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

if(NOT EXISTS "${SOURCE_DIR}/targets/esp-idf/main/CMakeLists.txt")
  message(FATAL_ERROR "esp-idf main CMakeLists.txt missing")
endif()
if(NOT EXISTS "${SOURCE_DIR}/cmake/froth_board_assets.cmake")
  message(FATAL_ERROR "froth_board_assets.cmake missing")
endif()

file(READ "${SOURCE_DIR}/cmake/froth_board_assets.cmake" BOARD_ASSET_CMAKE)
if(NOT BOARD_ASSET_CMAKE MATCHES "function\\(frothy_resolve_board_sources out_var source_root board\\)")
  message(FATAL_ERROR "build helper is missing frothy_resolve_board_sources()")
endif()
if(NOT BOARD_ASSET_CMAKE MATCHES "string\\(JSON board_source_count ERROR_VARIABLE board_source_error")
  message(FATAL_ERROR "build helper is not reading board.json sources")
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
