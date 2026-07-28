BOARD_TARGET := arduino-rp2040
BOARD_PROFILE := rp2040_plain
BOARD_ARDUINO_FQBN := rp2040:rp2040:arduino_nano_connect
BOARD_ARDUINO_OPTIONS := flash=16777216_65536,freq=133,opt=Small,usbstack=picosdk
BOARD_SOURCES += boards/arduino_nano_rp2040_connect/board_defs.c
