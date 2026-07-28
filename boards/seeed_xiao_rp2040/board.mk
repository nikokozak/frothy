# Generated from board.json by tools/gen-boards.mjs. Do not edit.
BOARD_TARGET := arduino-rp2040
BOARD_PROFILE := rp2040_plain
BOARD_ARDUINO_FQBN := rp2040:rp2040:seeed_xiao_rp2040
BOARD_ARDUINO_OPTIONS := freq=133,opt=Small,usbstack=picosdk
BOARD_FLASH_BYTES := 2097152
BOARD_CFLAGS += -DFR_PROFILE_TARGET_FLASH_BYTES=2097152u
BOARD_SOURCES += boards/seeed_xiao_rp2040/board_defs.c
