file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " c_bytes "${hex}")
set(rendered "static const char ${VARNAME}[] = {${c_bytes}0x00};\n")

if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" current)
else()
  set(current "")
endif()

if(NOT current STREQUAL rendered)
  file(WRITE "${OUTPUT}" "${rendered}")
endif()
