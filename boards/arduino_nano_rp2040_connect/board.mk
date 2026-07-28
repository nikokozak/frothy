# Generated from board.json by tools/gen-boards.mjs. Do not edit.
BOARD_TARGET := arduino-rp2040
BOARD_PROFILE := rp2040_nina
BOARD_ARDUINO_FQBN := rp2040:rp2040:arduino_nano_connect
BOARD_ARDUINO_OPTIONS := freq=133,opt=Small,usbstack=picosdk
BOARD_HAS_NINA := 1
BOARD_FLASH_BYTES := 16777216
BOARD_CFLAGS += -DFR_PROFILE_TARGET_FLASH_BYTES=16777216u
BOARD_SOURCES += boards/arduino_nano_rp2040_connect/board_defs.c
