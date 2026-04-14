function(froth_write_if_changed output content)
  if(EXISTS "${output}")
    file(READ "${output}" current_content)
  else()
    set(current_content "")
  endif()

  if(NOT current_content STREQUAL content)
    file(WRITE "${output}" "${content}")
  endif()
endfunction()

function(froth_embed_board_assets target source_root board use_custom_targets)
  target_include_directories(${target} PRIVATE "${CMAKE_BINARY_DIR}")

  set(board_lib_path "${source_root}/boards/${board}/lib/board.froth")
  if(EXISTS "${board_lib_path}")
    set(board_lib_h "${CMAKE_BINARY_DIR}/froth_board_lib.h")
    add_custom_command(
      COMMAND ${CMAKE_COMMAND}
        -DINPUT=${board_lib_path}
        -DOUTPUT=${board_lib_h}
        -DVARNAME="froth_board_lib"
        -P ${source_root}/cmake/embed_froth.cmake
      OUTPUT ${board_lib_h}
      DEPENDS ${board_lib_path}
      COMMENT "Embedding board.froth as C data..."
    )
    target_compile_definitions(${target} PRIVATE FROTH_HAS_BOARD_LIB)
    if(use_custom_targets)
      string(MAKE_C_IDENTIFIER "${target}" target_id)
      set(board_lib_target "froth_board_lib_${target_id}")
      add_custom_target(${board_lib_target} DEPENDS ${board_lib_h})
      add_dependencies(${target} ${board_lib_target})
    else()
      target_sources(${target} PRIVATE ${board_lib_h})
    endif()
  endif()

  set(board_json_path "${source_root}/boards/${board}/board.json")
  if(NOT EXISTS "${board_json_path}")
    return()
  endif()

  file(READ "${board_json_path}" board_json_content)
  string(JSON board_pin_count ERROR_VARIABLE board_pin_error
         LENGTH "${board_json_content}" pins)
  if(board_pin_error OR board_pin_count STREQUAL "0")
    return()
  endif()

  set(board_pins_froth "${CMAKE_BINARY_DIR}/froth_board_pins.froth")
  set(board_pins_content "\\ Auto-generated from boards/${board}/board.json\n")
  math(EXPR board_last_pin_index "${board_pin_count} - 1")
  foreach(board_pin_index RANGE 0 ${board_last_pin_index})
    string(JSON board_pin_name MEMBER "${board_json_content}" pins ${board_pin_index})
    string(JSON board_pin_value GET "${board_json_content}" pins ${board_pin_name})
    string(APPEND board_pins_content "${board_pin_value} '${board_pin_name} value\n")
  endforeach()
  froth_write_if_changed("${board_pins_froth}" "${board_pins_content}")

  set(board_pins_h "${CMAKE_BINARY_DIR}/froth_board_pins.h")
  add_custom_command(
    COMMAND ${CMAKE_COMMAND}
      -DINPUT=${board_pins_froth}
      -DOUTPUT=${board_pins_h}
      -DVARNAME="froth_board_pins"
      -P ${source_root}/cmake/embed_froth.cmake
    OUTPUT ${board_pins_h}
    DEPENDS ${board_pins_froth}
    COMMENT "Embedding auto-generated board pin constants..."
  )
  target_compile_definitions(${target} PRIVATE FROTH_HAS_BOARD_PINS)
  if(use_custom_targets)
    string(MAKE_C_IDENTIFIER "${target}" target_id)
    set(board_pins_target "froth_board_pins_${target_id}")
    add_custom_target(${board_pins_target} DEPENDS ${board_pins_h})
    add_dependencies(${target} ${board_pins_target})
  else()
    target_sources(${target} PRIVATE ${board_pins_h})
  endif()
endfunction()

function(frothy_generate_board_pin_seed target source_root board)
  target_include_directories(${target} PRIVATE "${CMAKE_BINARY_DIR}")

  set(board_json_path "${source_root}/boards/${board}/board.json")
  if(NOT EXISTS "${board_json_path}")
    return()
  endif()

  file(READ "${board_json_path}" board_json_content)
  string(JSON board_pin_count ERROR_VARIABLE board_pin_error
         LENGTH "${board_json_content}" pins)
  if(board_pin_error OR board_pin_count STREQUAL "0")
    return()
  endif()

  set(frothy_pins_h "${CMAKE_BINARY_DIR}/frothy_board_pins.h")
  set(frothy_pins_content "#pragma once\n")
  string(APPEND frothy_pins_content "#include \"frothy_ffi.h\"\n\n")
  string(APPEND frothy_pins_content "static const frothy_board_pin_t frothy_generated_board_pins[] = {\n")

  math(EXPR board_last_pin_index "${board_pin_count} - 1")
  foreach(board_pin_index RANGE 0 ${board_last_pin_index})
    string(JSON board_pin_name MEMBER "${board_json_content}" pins ${board_pin_index})
    string(JSON board_pin_value GET "${board_json_content}" pins ${board_pin_name})
    string(APPEND frothy_pins_content "    {\"${board_pin_name}\", ${board_pin_value}},\n")
  endforeach()

  string(APPEND frothy_pins_content "    {NULL, 0},\n};\n")
  froth_write_if_changed("${frothy_pins_h}" "${frothy_pins_content}")
  target_compile_definitions(${target} PRIVATE FROTHY_HAS_BOARD_PINS)
endfunction()

function(frothy_generate_board_workshop_lib target source_root board)
  target_include_directories(${target} PRIVATE "${CMAKE_BINARY_DIR}")

  set(workshop_lib_path "${source_root}/boards/${board}/lib/workshop.frothy")
  if(NOT EXISTS "${workshop_lib_path}")
    return()
  endif()

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${workshop_lib_path}"
  )

  file(READ "${workshop_lib_path}" workshop_hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, "
         workshop_bytes "${workshop_hex}")

  set(workshop_h "${CMAKE_BINARY_DIR}/frothy_board_workshop_lib.h")
  set(workshop_content
      "#pragma once\nstatic const char frothy_board_workshop_lib[] = {${workshop_bytes}0x00};\n")
  froth_write_if_changed("${workshop_h}" "${workshop_content}")

  target_sources(${target} PRIVATE "${workshop_h}")
  target_compile_definitions(${target} PRIVATE FROTHY_HAS_BOARD_WORKSHOP_LIB)
endfunction()
