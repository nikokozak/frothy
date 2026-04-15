# CMake generated Testfile for 
# Source directory: /Users/niko/Developer/Frothy
# Build directory: /Users/niko/Developer/Frothy/build-default-helper
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[frothy_parser]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_parser_tests")
set_tests_properties([=[frothy_parser]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;336;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_eval]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_eval_tests")
set_tests_properties([=[frothy_eval]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;357;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_snapshot]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_snapshot_tests")
set_tests_properties([=[frothy_snapshot]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;389;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_shell]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_shell_tests")
set_tests_properties([=[frothy_shell]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;421;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_ffi]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_ffi_tests")
set_tests_properties([=[frothy_ffi]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;442;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_tm1629_runtime]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_tm1629_runtime_tests")
set_tests_properties([=[frothy_tm1629_runtime]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;452;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_maintained]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_project_ffi_maintained_tests")
set_tests_properties([=[frothy_project_ffi_maintained]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;288;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;490;frothy_add_project_ffi_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_maintained_no_weak]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_project_ffi_maintained_no_weak_tests")
set_tests_properties([=[frothy_project_ffi_maintained_no_weak]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;288;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;495;frothy_add_project_ffi_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_legacy]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_project_ffi_legacy_tests")
set_tests_properties([=[frothy_project_ffi_legacy]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;288;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;506;frothy_add_project_ffi_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_legacy_no_weak]=] "/Users/niko/Developer/Frothy/build-default-helper/frothy_project_ffi_legacy_no_weak_tests")
set_tests_properties([=[frothy_project_ffi_legacy_no_weak]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;288;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;512;frothy_add_project_ffi_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_config_maintained_smoke]=] "/bin/sh" "/Users/niko/Developer/Frothy/tests/project_ffi/project_ffi_config_smoke.sh" "/opt/homebrew/bin/cmake" "/Users/niko/Developer/Frothy" "posix" "posix" "/Users/niko/Developer/Frothy/tests/project_ffi/maintained_project_ffi.cmake")
set_tests_properties([=[frothy_project_ffi_config_maintained_smoke]=] PROPERTIES  LABELS "frothy" TIMEOUT "60" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;525;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_config_legacy_smoke]=] "/bin/sh" "/Users/niko/Developer/Frothy/tests/project_ffi/project_ffi_config_smoke.sh" "/opt/homebrew/bin/cmake" "/Users/niko/Developer/Frothy" "posix" "posix" "/Users/niko/Developer/Frothy/tests/project_ffi/legacy_project_ffi.cmake")
set_tests_properties([=[frothy_project_ffi_config_legacy_smoke]=] PROPERTIES  LABELS "frothy" TIMEOUT "60" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;537;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_tm1629_board_smoke]=] "/bin/sh" "/Users/niko/Developer/Frothy/tests/tm1629_board_config_smoke.sh" "/opt/homebrew/bin/cmake" "/Users/niko/Developer/Frothy" "esp32-devkit-v4-game-board" "posix")
set_tests_properties([=[frothy_tm1629_board_smoke]=] PROPERTIES  LABELS "frothy" TIMEOUT "60" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;552;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_esp_idf_board_config_smoke]=] "/bin/sh" "/Users/niko/Developer/Frothy/tests/esp_idf_board_config_smoke.sh" "/opt/homebrew/bin/cmake" "/Users/niko/Developer/Frothy" "esp32-devkit-v4-game-board")
set_tests_properties([=[frothy_esp_idf_board_config_smoke]=] PROPERTIES  LABELS "frothy" TIMEOUT "60" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;565;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
