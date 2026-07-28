# Build-service key for the Earle Philhower Arduino-Pico toolchain.
TARGET_BUILD_KIND := arduino-pico
# I2S is the toolchain's PIO-backed native-library surface.
TARGET_CAPABILITIES := adc ble gpio i2c i2s net pwm
TARGET_MAIN_SOURCE :=
TARGET_SOURCES += \
	targets/common/target_defs.c \
	targets/arduino-rp2040/platform.cpp

ifeq ($(strip $(BOARD_FLASH_BYTES)),)
$(error BOARD_FLASH_BYTES is required for the arduino-rp2040 target)
endif
ifeq ($(strip $(BOARD_ARDUINO_FQBN)),)
$(error BOARD_ARDUINO_FQBN is required for the arduino-rp2040 target)
endif
ifeq ($(strip $(BOARD_ARDUINO_OPTIONS)),)
$(error BOARD_ARDUINO_OPTIONS is required for the arduino-rp2040 target)
endif

ifeq ($(BOARD_HAS_NINA),1)
TARGET_SOURCES += targets/arduino-rp2040/nina.cpp
TARGET_CFLAGS += -DFR_BOARD_HAS_NINA=1 -DFAKE_GAP=1
endif

ARDUINO_RP2040_CORE_VERSION := 4.6.0
ARDUINO_RP2040_SKETCH_TEMPLATE := targets/arduino-rp2040/Frothy/Frothy.ino
ARDUINO_RP2040_SKETCH_DIR = $(abspath $(BUILD_DIR))/arduino-sketch/Frothy
ARDUINO_RP2040_BUILD_DIR = $(abspath $(BUILD_DIR))/arduino-build
ARDUINO_RP2040_OUTPUT_DIR = $(abspath $(BUILD_DIR))/arduino-output
ARDUINO_RP2040_FLAGS = $(subst \",",$(filter-out -std=c99 -Wall -Wextra -Werror -pedantic,$(FR_CFLAGS)))
# The current platform reserves two 32 KiB persistence slots.
ARDUINO_RP2040_BOARD_OPTIONS = flash=$(BOARD_FLASH_BYTES)_65536,$(BOARD_ARDUINO_OPTIONS)

ARTIFACT_ELF = $(BUILD_DIR)/arduino-output/Frothy.ino.elf
ARTIFACT_UF2 = $(BUILD_DIR)/arduino-output/Frothy.ino.uf2
ARTIFACT_HEX =

TARGET_BUILD_DEPS += $(ARDUINO_RP2040_SKETCH_TEMPLATE)

ARDUINO_RP2040_TOOLCHAIN_CHECK = command -v arduino-cli >/dev/null || { printf 'arduino-cli is required for %s\n' "$(BOARD)"; exit 2; }; arduino-cli core list | awk '$$1 == "rp2040:rp2040" && $$2 == "$(ARDUINO_RP2040_CORE_VERSION)" { found = 1 } END { exit !found }' || { printf 'install the RP2040 core with:\n  arduino-cli core install rp2040:rp2040@$(ARDUINO_RP2040_CORE_VERSION) --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json\n'; exit 2; }

ifeq ($(BOARD_HAS_NINA),1)
ARDUINO_RP2040_TOOLCHAIN_CHECK += ; for library in 'WiFiNINA 2.1.1' 'ArduinoBLE 2.1.0' 'Arduino_SpiNINA 0.0.2'; do set -- $$library; arduino-cli lib list | awk -v name="$$1" -v version="$$2" '$$1 == name && $$2 == version { found = 1 } END { exit !found }' || { printf 'install the Nano radio libraries with:\n  arduino-cli lib install WiFiNINA@2.1.1 ArduinoBLE@2.1.0 Arduino_SpiNINA@0.0.2\n'; exit 2; }; done
endif

TARGET_BUILD_COMMAND = set -e; $(ARDUINO_RP2040_TOOLCHAIN_CHECK); test "$(ARDUINO_RP2040_SKETCH_DIR)" != "/arduino-sketch/Frothy"; rm -rf "$(ARDUINO_RP2040_SKETCH_DIR)"; mkdir -p "$(ARDUINO_RP2040_SKETCH_DIR)/src" "$(ARDUINO_RP2040_BUILD_DIR)" "$(ARDUINO_RP2040_OUTPUT_DIR)"; cp "$(ARDUINO_RP2040_SKETCH_TEMPLATE)" "$(ARDUINO_RP2040_SKETCH_DIR)/Frothy.ino"; external_index=0; for source in $(FROTHY_SOURCES); do case "$$source" in /*) external_index=$$((external_index + 1)); destination="$(ARDUINO_RP2040_SKETCH_DIR)/src/external/source-$$external_index-$$(basename "$$source")" ;; *) destination="$(ARDUINO_RP2040_SKETCH_DIR)/src/$$source" ;; esac; mkdir -p "$$(dirname "$$destination")"; cp "$$source" "$$destination"; done; arduino-cli compile --fqbn "$(BOARD_ARDUINO_FQBN)" --board-options "$(ARDUINO_RP2040_BOARD_OPTIONS)" --build-path "$(ARDUINO_RP2040_BUILD_DIR)" --output-dir "$(ARDUINO_RP2040_OUTPUT_DIR)" --warnings all --build-property 'compiler.c.extra_flags=$(ARDUINO_RP2040_FLAGS)' --build-property 'compiler.cpp.extra_flags=$(ARDUINO_RP2040_FLAGS)' "$(ARDUINO_RP2040_SKETCH_DIR)"
TARGET_ARTIFACTS_CHECK = @test -f "$(ARTIFACT_UF2)"
TARGET_FLASH_DEPS = $(ARTIFACT_ELF)
TARGET_FLASH_CHECK = @$(ARDUINO_RP2040_TOOLCHAIN_CHECK)
TARGET_FLASH_COMMAND = arduino-cli upload --fqbn "$(BOARD_ARDUINO_FQBN)" --board-options "$(ARDUINO_RP2040_BOARD_OPTIONS)" --port "$(BOARD_PORT)" --input-dir "$(ARDUINO_RP2040_OUTPUT_DIR)" "$(ARDUINO_RP2040_SKETCH_DIR)"
