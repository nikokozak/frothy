# CMake generated Testfile for 
# Source directory: /Users/niko/Developer/Frothy
# Build directory: /Users/niko/Developer/Frothy/build-tm1629
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[frothy_parser]=] "/Users/niko/Developer/Frothy/build-tm1629/frothy_parser_tests")
set_tests_properties([=[frothy_parser]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;204;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_eval]=] "/Users/niko/Developer/Frothy/build-tm1629/frothy_eval_tests")
set_tests_properties([=[frothy_eval]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;225;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_snapshot]=] "/Users/niko/Developer/Frothy/build-tm1629/frothy_snapshot_tests")
set_tests_properties([=[frothy_snapshot]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;257;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_shell]=] "/Users/niko/Developer/Frothy/build-tm1629/frothy_shell_tests")
set_tests_properties([=[frothy_shell]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;289;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_ffi]=] "/Users/niko/Developer/Frothy/build-tm1629/frothy_ffi_tests")
set_tests_properties([=[frothy_ffi]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;310;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_maintained]=] "/Users/niko/Developer/Frothy/build-tm1629/frothy_project_ffi_maintained_tests")
set_tests_properties([=[frothy_project_ffi_maintained]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;156;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;318;frothy_add_project_ffi_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_maintained_no_weak]=] "/Users/niko/Developer/Frothy/build-tm1629/frothy_project_ffi_maintained_no_weak_tests")
set_tests_properties([=[frothy_project_ffi_maintained_no_weak]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;156;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;323;frothy_add_project_ffi_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_legacy]=] "/Users/niko/Developer/Frothy/build-tm1629/frothy_project_ffi_legacy_tests")
set_tests_properties([=[frothy_project_ffi_legacy]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;156;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;334;frothy_add_project_ffi_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_legacy_no_weak]=] "/Users/niko/Developer/Frothy/build-tm1629/frothy_project_ffi_legacy_no_weak_tests")
set_tests_properties([=[frothy_project_ffi_legacy_no_weak]=] PROPERTIES  LABELS "frothy" TIMEOUT "30" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;156;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;340;frothy_add_project_ffi_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_config_maintained_smoke]=] "/bin/sh" "/Users/niko/Developer/Frothy/tests/project_ffi/project_ffi_config_smoke.sh" "/opt/homebrew/bin/cmake" "/Users/niko/Developer/Frothy" "esp32-devkit-v4-game-board" "posix" "/Users/niko/Developer/Frothy/tests/project_ffi/maintained_project_ffi.cmake")
set_tests_properties([=[frothy_project_ffi_config_maintained_smoke]=] PROPERTIES  LABELS "frothy" TIMEOUT "60" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;353;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
add_test([=[frothy_project_ffi_config_legacy_smoke]=] "/bin/sh" "/Users/niko/Developer/Frothy/tests/project_ffi/project_ffi_config_smoke.sh" "/opt/homebrew/bin/cmake" "/Users/niko/Developer/Frothy" "esp32-devkit-v4-game-board" "posix" "/Users/niko/Developer/Frothy/tests/project_ffi/legacy_project_ffi.cmake")
set_tests_properties([=[frothy_project_ffi_config_legacy_smoke]=] PROPERTIES  LABELS "frothy" TIMEOUT "60" _BACKTRACE_TRIPLES "/Users/niko/Developer/Frothy/CMakeLists.txt;365;add_test;/Users/niko/Developer/Frothy/CMakeLists.txt;0;")
