BOARD_TARGET := arduino-rp2040
BOARD_PROFILE := rp2040_plain
BOARD_ARDUINO_FQBN := rp2040:rp2040:seeed_xiao_rp2040
BOARD_ARDUINO_OPTIONS := flash=2097152_65536,freq=133,opt=Small,usbstack=picosdk
BOARD_SOURCES += boards/seeed_xiao_rp2040/board_defs.c
