TARGET_MAIN_SOURCE :=
TARGET_SOURCES += \
	targets/common/target_defs.c \
	targets/arduino-rp2040/platform.cpp

ARDUINO_RP2040_CORE_VERSION := 4.6.0
ARDUINO_RP2040_SKETCH_TEMPLATE := targets/arduino-rp2040/Frothy/Frothy.ino
ARDUINO_RP2040_SKETCH_DIR = $(abspath $(BUILD_DIR))/arduino-sketch/Frothy
ARDUINO_RP2040_BUILD_DIR = $(abspath $(BUILD_DIR))/arduino-build
ARDUINO_RP2040_OUTPUT_DIR = $(abspath $(BUILD_DIR))/arduino-output
ARDUINO_RP2040_FLAGS = $(subst \",",$(filter-out -std=c99 -Wall -Wextra -Werror -pedantic,$(FR_CFLAGS)))

ARTIFACT_ELF = $(BUILD_DIR)/arduino-output/Frothy.ino.elf
ARTIFACT_UF2 = $(BUILD_DIR)/arduino-output/Frothy.ino.uf2
ARTIFACT_HEX =

TARGET_BUILD_DEPS += $(ARDUINO_RP2040_SKETCH_TEMPLATE)

ARDUINO_RP2040_TOOLCHAIN_CHECK = command -v arduino-cli >/dev/null || { printf 'arduino-cli is required for %s\n' "$(BOARD)"; exit 2; }; arduino-cli core list | awk '$$1 == "rp2040:rp2040" && $$2 == "$(ARDUINO_RP2040_CORE_VERSION)" { found = 1 } END { exit !found }' || { printf 'install the RP2040 core with:\n  arduino-cli core install rp2040:rp2040@$(ARDUINO_RP2040_CORE_VERSION) --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json\n'; exit 2; }

TARGET_BUILD_COMMAND = set -e; $(ARDUINO_RP2040_TOOLCHAIN_CHECK); test "$(ARDUINO_RP2040_SKETCH_DIR)" != "/arduino-sketch/Frothy"; rm -rf "$(ARDUINO_RP2040_SKETCH_DIR)"; mkdir -p "$(ARDUINO_RP2040_SKETCH_DIR)/src" "$(ARDUINO_RP2040_BUILD_DIR)" "$(ARDUINO_RP2040_OUTPUT_DIR)"; cp "$(ARDUINO_RP2040_SKETCH_TEMPLATE)" "$(ARDUINO_RP2040_SKETCH_DIR)/Frothy.ino"; for source in $(FROTHY_SOURCES); do destination="$(ARDUINO_RP2040_SKETCH_DIR)/src/$$source"; mkdir -p "$$(dirname "$$destination")"; cp "$$source" "$$destination"; done; arduino-cli compile --fqbn "$(BOARD_ARDUINO_FQBN)" --board-options "$(BOARD_ARDUINO_OPTIONS)" --build-path "$(ARDUINO_RP2040_BUILD_DIR)" --output-dir "$(ARDUINO_RP2040_OUTPUT_DIR)" --warnings all --build-property 'compiler.c.extra_flags=$(ARDUINO_RP2040_FLAGS)' --build-property 'compiler.cpp.extra_flags=$(ARDUINO_RP2040_FLAGS)' "$(ARDUINO_RP2040_SKETCH_DIR)"
TARGET_ARTIFACTS_CHECK = @test -f "$(ARTIFACT_UF2)"
TARGET_FLASH_DEPS = $(ARTIFACT_ELF)
TARGET_FLASH_CHECK = @$(ARDUINO_RP2040_TOOLCHAIN_CHECK)
TARGET_FLASH_COMMAND = arduino-cli upload --fqbn "$(BOARD_ARDUINO_FQBN)" --board-options "$(BOARD_ARDUINO_OPTIONS)" --port "$(BOARD_PORT)" --input-dir "$(ARDUINO_RP2040_OUTPUT_DIR)" "$(ARDUINO_RP2040_SKETCH_DIR)"
