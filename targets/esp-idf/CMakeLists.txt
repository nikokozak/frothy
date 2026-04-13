cmake_minimum_required(VERSION 3.16)

set(FROTH_BOARD "esp32-devkit-v1" CACHE STRING "Target board (directory name under boards/)")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(froth)
