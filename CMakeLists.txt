cmake_minimum_required(VERSION 3.23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Frothy)
enable_testing()

include(${CMAKE_SOURCE_DIR}/cmake/froth_board_assets.cmake)

set(FROTH_BOARD "posix" CACHE STRING "Target board (directory name under boards/)")
set(FROTH_PLATFORM "posix" CACHE STRING "Target platform (directory name under platforms/)")

if(FROTH_PLATFORM STREQUAL "posix")
  set(FROTHY_BUILD_HOST_DEFAULT ON)
else()
  set(FROTHY_BUILD_HOST_DEFAULT OFF)
endif()
option(FROTHY_BUILD_HOST "Build the Frothy host runtime." ${FROTHY_BUILD_HOST_DEFAULT})
set(FROTHY_EVAL_VALUE_CAPACITY 256 CACHE STRING "Frothy evaluator scratch capacity in number of values.")
set(FROTHY_OBJECT_CAPACITY 128 CACHE STRING "Frothy runtime object and free-span capacity.")
set(FROTHY_PAYLOAD_CAPACITY 16384 CACHE STRING "Frothy runtime payload arena capacity in bytes.")
set(FROTHY_SHELL_SOURCE_CAPACITY 2048 CACHE STRING "Frothy multiline shell source capacity in bytes.")
set(FROTHY_PARSER_BINDING_CAPACITY 128 CACHE STRING "Frothy parser binding capacity.")
set(FROTHY_PARSER_SCOPE_CAPACITY 32 CACHE STRING "Frothy parser scope capacity.")
set(FROTHY_PARSER_FRAME_CAPACITY 16 CACHE STRING "Frothy parser frame capacity.")
set(FROTHY_IR_LITERAL_CAPACITY 128 CACHE STRING "Frothy IR literal capacity.")
set(FROTHY_IR_NODE_CAPACITY 512 CACHE STRING "Frothy IR node capacity.")
set(FROTHY_IR_LINK_CAPACITY 512 CACHE STRING "Frothy IR link capacity.")

set(FROTH_CELL_SIZE_BITS 32 CACHE STRING "Cell width in bits: 8, 16, 32 or 64")
set(FROTH_DS_CAPACITY 256 CACHE STRING "Data Stack (DS) Capacity in number of cells.")
set(FROTH_RS_CAPACITY 256 CACHE STRING "Return Stack (RS) Capacity in number of cells.")
set(FROTH_CS_CAPACITY 256 CACHE STRING "Control Stack (CS) Capacity in number of cells.")
set(FROTH_HEAP_SIZE 4096 CACHE STRING "Heap size in number of bytes.")
set(FROTH_SLOT_TABLE_SIZE 128 CACHE STRING "Maximum number of slots.")
set(FROTH_DATA_SPACE_SIZE 256 CACHE STRING "Mutable memory size in number of cells.")
set(FROTH_LINE_BUFFER_SIZE 1024 CACHE STRING "Input line buffer for REPL in number of bytes.")
set(FROTH_MAX_PERM_SIZE 16 CACHE STRING "Maximum number of indices in a perm pattern.")
set(FROTH_VERSION "0.1.0" CACHE STRING "Frothy version string")
set(FROTH_HAS_SNAPSHOTS ON CACHE BOOL "Target supports Frothy snapshots")
set(FROTH_SNAPSHOT_BLOCK_SIZE "2048" CACHE STRING "Snapshot size in target memory (bytes)")
set(FROTH_SNAPSHOT_PATH_A "froth_a.snap" CACHE STRING "Snapshot A file path (default froth_a.snap)")
set(FROTH_SNAPSHOT_PATH_B "froth_b.snap" CACHE STRING "Snapshot B file path (default froth_b.snap)")
set(FROTH_STRING_MAX_LEN 256 CACHE STRING "Maximum string length in bytes (all creation paths).")
set(FROTH_TBUF_SIZE 1024 CACHE STRING "Transient string scratch ring size in bytes.")
set(FROTH_TDESC_MAX 32 CACHE STRING "Maximum concurrent transient string descriptors.")
set(FROTH_FFI_MAX_TABLES 8 CACHE STRING "Maximum number of FFI binding tables.")
set(FROTH_PROJECT_FFI_CONFIG "" CACHE FILEPATH "Optional project FFI config fragment")

set(FROTH_RUNTIME_SUPPORT_SRCS
  src/froth_console.c
  src/froth_stack.c
  src/froth_fmt.c
  src/froth_ffi.c
  src/froth_tbuf.c
  src/frothy_ffi.c
  boards/${FROTH_BOARD}/ffi.c
)

set(FROTH_COMMON_INCLUDE_DIRS
  src
  ${CMAKE_BINARY_DIR}
  boards/${FROTH_BOARD}
  platforms/${FROTH_PLATFORM}
)

function(froth_configure_common_target target)
  target_include_directories(${target} PRIVATE ${FROTH_COMMON_INCLUDE_DIRS})
  set_property(TARGET ${target} PROPERTY C_STANDARD 11)
  set_property(TARGET ${target} PROPERTY C_STANDARD_REQUIRED ON)

  target_compile_definitions(${target} PRIVATE FROTH_CELL_SIZE_BITS=${FROTH_CELL_SIZE_BITS})
  target_compile_definitions(${target} PRIVATE FROTH_DS_CAPACITY=${FROTH_DS_CAPACITY})
  target_compile_definitions(${target} PRIVATE FROTH_RS_CAPACITY=${FROTH_RS_CAPACITY})
  target_compile_definitions(${target} PRIVATE FROTH_CS_CAPACITY=${FROTH_CS_CAPACITY})
  target_compile_definitions(${target} PRIVATE FROTH_HEAP_SIZE=${FROTH_HEAP_SIZE})
  target_compile_definitions(${target} PRIVATE FROTH_SLOT_TABLE_SIZE=${FROTH_SLOT_TABLE_SIZE})
  target_compile_definitions(${target} PRIVATE FROTH_DATA_SPACE_SIZE=${FROTH_DATA_SPACE_SIZE})
  target_compile_definitions(${target} PRIVATE FROTH_LINE_BUFFER_SIZE=${FROTH_LINE_BUFFER_SIZE})
  target_compile_definitions(${target} PRIVATE FROTH_MAX_PERM_SIZE=${FROTH_MAX_PERM_SIZE})
  target_compile_definitions(${target} PRIVATE FROTH_VERSION="${FROTH_VERSION}")
  target_compile_definitions(${target} PRIVATE FROTH_BOARD_NAME="${FROTH_BOARD}")
  target_compile_definitions(${target} PRIVATE FROTH_STRING_MAX_LEN=${FROTH_STRING_MAX_LEN})
  target_compile_definitions(${target} PRIVATE FROTH_TBUF_SIZE=${FROTH_TBUF_SIZE})
  target_compile_definitions(${target} PRIVATE FROTH_TDESC_MAX=${FROTH_TDESC_MAX})
  target_compile_definitions(${target} PRIVATE FROTH_FFI_MAX_TABLES=${FROTH_FFI_MAX_TABLES})

  if(FROTH_HAS_SNAPSHOTS)
    target_compile_definitions(${target} PRIVATE FROTH_HAS_SNAPSHOTS)
    target_compile_definitions(${target} PRIVATE FROTH_SNAPSHOT_BLOCK_SIZE=${FROTH_SNAPSHOT_BLOCK_SIZE})
    target_compile_definitions(${target} PRIVATE FROTH_SNAPSHOT_PATH_A="${FROTH_SNAPSHOT_PATH_A}")
    target_compile_definitions(${target} PRIVATE FROTH_SNAPSHOT_PATH_B="${FROTH_SNAPSHOT_PATH_B}")
  endif()
endfunction()

function(frothy_configure_target target)
  froth_configure_common_target(${target})
  target_compile_definitions(${target} PRIVATE
    FROTHY_EVAL_VALUE_CAPACITY=${FROTHY_EVAL_VALUE_CAPACITY}
    FROTHY_OBJECT_CAPACITY=${FROTHY_OBJECT_CAPACITY}
    FROTHY_PAYLOAD_CAPACITY=${FROTHY_PAYLOAD_CAPACITY}
    FROTHY_SHELL_SOURCE_CAPACITY=${FROTHY_SHELL_SOURCE_CAPACITY}
    FROTHY_PARSER_BINDING_CAPACITY=${FROTHY_PARSER_BINDING_CAPACITY}
    FROTHY_PARSER_SCOPE_CAPACITY=${FROTHY_PARSER_SCOPE_CAPACITY}
    FROTHY_PARSER_FRAME_CAPACITY=${FROTHY_PARSER_FRAME_CAPACITY}
    FROTHY_IR_LITERAL_CAPACITY=${FROTHY_IR_LITERAL_CAPACITY}
    FROTHY_IR_NODE_CAPACITY=${FROTHY_IR_NODE_CAPACITY}
    FROTHY_IR_LINK_CAPACITY=${FROTHY_IR_LINK_CAPACITY}
  )
endfunction()

if(FROTHY_BUILD_HOST)
  add_executable(Frothy)
  target_sources(Frothy
    PRIVATE
      src/frothy_main.c
      src/frothy_boot.c
      src/frothy_base_image.c
      src/frothy_control.c
      src/frothy_shell.c
      src/frothy_inspect.c
      src/frothy_ir.c
      src/frothy_parser.c
      src/frothy_snapshot.c
      src/frothy_snapshot_codec.c
      src/frothy_value.c
      src/frothy_eval.c
      src/froth_vm.c
      src/froth_heap.c
      src/froth_cellspace.c
      src/froth_slot_table.c
      src/froth_transport.c
      src/froth_crc32.c
      src/frothy_link_stub.c
      ${FROTH_RUNTIME_SUPPORT_SRCS}
      platforms/${FROTH_PLATFORM}/platform.c
  )
  if(FROTH_HAS_SNAPSHOTS)
    target_sources(Frothy PRIVATE src/froth_snapshot.c)
  endif()
  if(FROTH_PROJECT_FFI_CONFIG)
    include(${FROTH_PROJECT_FFI_CONFIG})
    target_sources(Frothy PRIVATE ${FROTH_PROJECT_FFI_SOURCES})
    if(FROTH_PROJECT_FFI_INCLUDES)
      target_include_directories(Frothy PRIVATE ${FROTH_PROJECT_FFI_INCLUDES})
    endif()
    if(FROTH_PROJECT_FFI_DEFINES)
      target_compile_definitions(Frothy PRIVATE ${FROTH_PROJECT_FFI_DEFINES})
    endif()
    target_compile_definitions(Frothy PRIVATE FROTH_HAS_PROJECT_FFI)
  endif()
  frothy_configure_target(Frothy)
  frothy_generate_board_pin_seed(Frothy ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
  frothy_generate_board_workshop_lib(Frothy ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})

  if(EXISTS "${CMAKE_SOURCE_DIR}/tests/frothy_parser_test.c")
    add_executable(frothy_parser_tests
      tests/frothy_parser_test.c
      src/frothy_ir.c
      src/frothy_parser.c
    )
    frothy_configure_target(frothy_parser_tests)
    target_compile_definitions(frothy_parser_tests PRIVATE
      FROTHY_PARSER_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/frothy_parser/fixtures"
    )
    add_test(NAME frothy_parser COMMAND frothy_parser_tests)
    set_tests_properties(frothy_parser PROPERTIES LABELS "frothy" TIMEOUT 30)
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/tests/frothy_eval_test.c")
    add_executable(frothy_eval_tests
      tests/frothy_eval_test.c
      src/frothy_ir.c
      src/frothy_parser.c
      src/frothy_value.c
      src/frothy_eval.c
      src/froth_vm.c
      src/froth_heap.c
      src/froth_cellspace.c
      src/froth_slot_table.c
      ${FROTH_RUNTIME_SUPPORT_SRCS}
      platforms/${FROTH_PLATFORM}/platform.c
    )
    frothy_configure_target(frothy_eval_tests)
    frothy_generate_board_pin_seed(frothy_eval_tests ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
    frothy_generate_board_workshop_lib(frothy_eval_tests ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
    add_test(NAME frothy_eval COMMAND frothy_eval_tests)
    set_tests_properties(frothy_eval PROPERTIES LABELS "frothy" TIMEOUT 30)
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/tests/frothy_snapshot_test.c")
    add_executable(frothy_snapshot_tests
      tests/frothy_snapshot_test.c
      src/frothy_boot.c
      src/frothy_base_image.c
      src/frothy_control.c
      src/frothy_shell.c
      src/frothy_ir.c
      src/frothy_parser.c
      src/frothy_inspect.c
      src/frothy_snapshot.c
      src/frothy_snapshot_codec.c
      src/frothy_value.c
      src/frothy_eval.c
      src/froth_vm.c
      src/froth_heap.c
      src/froth_cellspace.c
      src/froth_slot_table.c
      src/froth_transport.c
      src/froth_crc32.c
      src/frothy_link_stub.c
      ${FROTH_RUNTIME_SUPPORT_SRCS}
      src/froth_snapshot.c
      platforms/${FROTH_PLATFORM}/platform.c
    )
    frothy_configure_target(frothy_snapshot_tests)
    frothy_generate_board_pin_seed(frothy_snapshot_tests ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
    frothy_generate_board_workshop_lib(frothy_snapshot_tests ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
    add_test(NAME frothy_snapshot COMMAND frothy_snapshot_tests)
    set_tests_properties(frothy_snapshot PROPERTIES LABELS "frothy" TIMEOUT 30)
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/tests/frothy_shell_test.c")
    add_executable(frothy_shell_tests
      tests/frothy_shell_test.c
      src/frothy_base_image.c
      src/frothy_control.c
      src/frothy_shell.c
      src/frothy_inspect.c
      src/frothy_ir.c
      src/frothy_parser.c
      src/frothy_snapshot.c
      src/frothy_snapshot_codec.c
      src/frothy_value.c
      src/frothy_eval.c
      src/froth_vm.c
      src/froth_heap.c
      src/froth_cellspace.c
      src/froth_slot_table.c
      src/froth_transport.c
      src/froth_crc32.c
      src/frothy_link_stub.c
      ${FROTH_RUNTIME_SUPPORT_SRCS}
      src/froth_snapshot.c
      platforms/${FROTH_PLATFORM}/platform.c
    )
    frothy_configure_target(frothy_shell_tests)
    target_compile_definitions(frothy_shell_tests PRIVATE FROTHY_SHELL_TESTING)
    frothy_generate_board_pin_seed(frothy_shell_tests ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
    frothy_generate_board_workshop_lib(frothy_shell_tests ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
    add_test(NAME frothy_shell COMMAND frothy_shell_tests)
    set_tests_properties(frothy_shell PROPERTIES LABELS "frothy" TIMEOUT 30)
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/tests/frothy_ffi_test.c")
    add_executable(frothy_ffi_tests
      tests/frothy_ffi_test.c
      src/frothy_ir.c
      src/frothy_parser.c
      src/frothy_value.c
      src/frothy_eval.c
      src/froth_vm.c
      src/froth_heap.c
      src/froth_cellspace.c
      src/froth_slot_table.c
      ${FROTH_RUNTIME_SUPPORT_SRCS}
      platforms/${FROTH_PLATFORM}/platform.c
    )
    frothy_configure_target(frothy_ffi_tests)
    frothy_generate_board_pin_seed(frothy_ffi_tests ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
    frothy_generate_board_workshop_lib(frothy_ffi_tests ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
    add_test(NAME frothy_ffi COMMAND frothy_ffi_tests)
    set_tests_properties(frothy_ffi PROPERTIES LABELS "frothy" TIMEOUT 30)
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/benchmarks/frothy_runtime_bench.c")
    add_executable(frothy_runtime_bench
      benchmarks/frothy_runtime_bench.c
      src/frothy_boot.c
      src/frothy_base_image.c
      src/frothy_control.c
      src/frothy_shell.c
      src/frothy_ir.c
      src/frothy_parser.c
      src/frothy_inspect.c
      src/frothy_snapshot.c
      src/frothy_snapshot_codec.c
      src/frothy_value.c
      src/frothy_eval.c
      src/froth_vm.c
      src/froth_heap.c
      src/froth_cellspace.c
      src/froth_slot_table.c
      src/froth_transport.c
      src/froth_crc32.c
      src/frothy_link_stub.c
      ${FROTH_RUNTIME_SUPPORT_SRCS}
      src/froth_snapshot.c
      platforms/${FROTH_PLATFORM}/platform.c
    )
    frothy_configure_target(frothy_runtime_bench)
    frothy_generate_board_pin_seed(frothy_runtime_bench ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
    frothy_generate_board_workshop_lib(frothy_runtime_bench ${CMAKE_SOURCE_DIR} ${FROTH_BOARD})
  endif()
endif()
