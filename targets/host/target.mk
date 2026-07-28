TARGET_BUILD_KIND := host
TARGET_CAPABILITIES := adc ble gpio i2c net pwm uart
TARGET_CC ?= cc
TARGET_SOURCES += targets/common/target_defs.c targets/host/platform.c
