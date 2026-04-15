set(FROTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

include(${FROTH_ROOT}/cmake/froth_board_assets.cmake)

set(FROTHY_KERNEL_SRCS
    ${FROTH_ROOT}/src/frothy_boot.c
    ${FROTH_ROOT}/src/frothy_base_image.c
    ${FROTH_ROOT}/src/frothy_control.c
    ${FROTH_ROOT}/src/frothy_shell.c
    ${FROTH_ROOT}/src/frothy_ffi.c
    ${FROTH_ROOT}/src/frothy_console_compat.c
    ${FROTH_ROOT}/src/frothy_inspect.c
    ${FROTH_ROOT}/src/frothy_ir.c
    ${FROTH_ROOT}/src/frothy_parser.c
    ${FROTH_ROOT}/src/frothy_snapshot.c
    ${FROTH_ROOT}/src/frothy_snapshot_codec.c
    ${FROTH_ROOT}/src/frothy_value.c
    ${FROTH_ROOT}/src/frothy_eval.c
    ${FROTH_ROOT}/src/froth_stack.c
    ${FROTH_ROOT}/src/froth_fmt.c
    ${FROTH_ROOT}/src/froth_console.c
    ${FROTH_ROOT}/src/froth_ffi.c
    ${FROTH_ROOT}/src/froth_tbuf.c
    ${FROTH_ROOT}/src/froth_vm.c
    ${FROTH_ROOT}/src/froth_heap.c
    ${FROTH_ROOT}/src/froth_slot_table.c
    ${FROTH_ROOT}/src/froth_cellspace.c
    ${FROTH_ROOT}/src/froth_snapshot.c
    ${FROTH_ROOT}/src/froth_crc32.c
    ${FROTH_ROOT}/src/froth_transport.c
    ${FROTH_ROOT}/src/frothy_link_stub.c
    ${FROTH_ROOT}/boards/${FROTH_BOARD}/ffi.c
)

idf_component_register(
    SRCS
        "main.c"
        ${FROTHY_KERNEL_SRCS}
        "${FROTH_ROOT}/platforms/esp-idf/platform.c"
    INCLUDE_DIRS
        "${FROTH_ROOT}/src"
        "${FROTH_ROOT}/boards/${FROTH_BOARD}"
    REQUIRES driver esp_timer nvs_flash
)

# Frothy compile-time configuration
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    FROTH_CELL_SIZE_BITS=32
    FROTH_DS_CAPACITY=256
    FROTH_RS_CAPACITY=256
    FROTH_CS_CAPACITY=256
    FROTH_HEAP_SIZE=4096
    FROTH_SLOT_TABLE_SIZE=256
    FROTH_DATA_SPACE_SIZE=256
    FROTH_LINE_BUFFER_SIZE=1024
    FROTH_MAX_PERM_SIZE=8
    FROTH_VERSION="0.1.0"
    FROTH_BOARD_NAME="${FROTH_BOARD}"
    FROTH_HAS_SNAPSHOTS=1
    FROTH_SNAPSHOT_BLOCK_SIZE=2048
    FROTH_STRING_MAX_LEN=256
)

froth_embed_board_assets(${COMPONENT_LIB} ${FROTH_ROOT} ${FROTH_BOARD} ON)
frothy_generate_board_pin_seed(${COMPONENT_LIB} ${FROTH_ROOT} ${FROTH_BOARD})
frothy_generate_board_base_lib(${COMPONENT_LIB} ${FROTH_ROOT} ${FROTH_BOARD})

# Project FFI: user-provided C bindings from [ffi] in froth.toml
if(DEFINED FROTH_PROJECT_FFI_CONFIG AND NOT FROTH_PROJECT_FFI_CONFIG STREQUAL "")
  include(${FROTH_PROJECT_FFI_CONFIG})
  target_sources(${COMPONENT_LIB} PRIVATE ${FROTH_PROJECT_FFI_SOURCES})
  if(FROTH_PROJECT_FFI_INCLUDES)
    target_include_directories(${COMPONENT_LIB} PRIVATE ${FROTH_PROJECT_FFI_INCLUDES})
  endif()
  if(FROTH_PROJECT_FFI_DEFINES)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${FROTH_PROJECT_FFI_DEFINES})
  endif()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE FROTH_HAS_PROJECT_FFI)
endif()
