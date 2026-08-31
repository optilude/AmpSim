# AmpSim - Guitar Amp Simulator Makefile
# For Electrosmith Daisy Seed on bkshepherd 125B platform

# Project Name and Sources
TARGET = AmpSim

# Enable NAM A2 fast path optimization, bare-metal support, and C++17
CPP_STANDARD = -std=gnu++17

# Match bkshepherd. libDaisy defaults to -O2, which is not enough for the
# WaveNet inner loops. Note that -Ofast implies -ffinite-math-only, so
# std::isnan/std::isfinite fold to false -- use fguard:: from float_guard.h
# for any non-finite check that has to survive.
OPT = -Ofast

# Sources
CPP_SOURCES = src/main.cpp \
              src/nam_processor.cpp \
              src/reverb_arena.cpp \
              Hardware/guitar_pedal_125b.cpp \
              src/dattorro/Dattorro.cpp

CMSIS_DSP_DIR = $(LIBDAISY_DIR)/Drivers/CMSIS-DSP
C_SOURCES += $(CMSIS_DSP_DIR)/Source/TransformFunctions/arm_rfft_fast_f32.c \
             $(CMSIS_DSP_DIR)/Source/TransformFunctions/arm_rfft_fast_init_f32.c \
             $(CMSIS_DSP_DIR)/Source/TransformFunctions/arm_cfft_f32.c \
             $(CMSIS_DSP_DIR)/Source/TransformFunctions/arm_cfft_init_f32.c \
             $(CMSIS_DSP_DIR)/Source/TransformFunctions/arm_cfft_radix8_f32.c \
             $(CMSIS_DSP_DIR)/Source/TransformFunctions/arm_bitreversal2.c \
             $(CMSIS_DSP_DIR)/Source/CommonTables/arm_common_tables.c \
             $(CMSIS_DSP_DIR)/Source/CommonTables/arm_const_structs.c \
             $(CMSIS_DSP_DIR)/Source/ComplexMathFunctions/arm_cmplx_mult_cmplx_f32.c \
             $(CMSIS_DSP_DIR)/Source/BasicMathFunctions/arm_add_f32.c

# Include paths (compat first to shadow std::mutex)
C_INCLUDES = -Iinclude/compat \
             -Isrc \
             -IHardware \
             -I$(CMSIS_DSP_DIR)/Include

# Library Locations
LIBDAISY_DIR = libDaisy
DAISYSP_DIR = DaisySP

# Use Daisy bootloader: application is written to QSPI flash via DFU, then
# copied to SRAM by the bootloader. The large capture blob remains in QSPI.
APP_TYPE = BOOT_SRAM

# Use the 2000ms grace period bootloader
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_4-intdfu-2000ms.bin

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
LDFLAGS += -T$(abspath src/nam_a2_sections.lds)
include $(SYSTEM_FILES_DIR)/Makefile

# Fix Daisy Makefile bug: dependency flags create spurious files
# Daisy sets -MF"$(@:%.o=%.d)" in global CPPFLAGS, which expands to -MF""
# outside of build rules, causing GCC to create files named after the next flag
# The dependency generation is already handled in the pattern rules, so remove it here
CPPFLAGS := $(filter-out -MMD -MP -MF%,$(CPPFLAGS))

# Static A2 Lite runtime is exception-free and allocation-free in audio.

# Override libDaisy's `program` and `program-dfu` targets so both flash the
# COMBINED image. libDaisy's program-dfu writes $(TARGET_BIN) -- the app alone
# -- which leaves whatever capture blob was last written in QSPI. Once the
# layout changes, the firmware reads the old blob at the new offset, every
# model fails its CRC check and the pedal reports "Bad model data". There is no
# reason to ever flash the app without the blob it was built against, so the
# app-only target is not offered at all.
.PHONY: program program-dfu program-boot-probe captures combined

captures: tools/build_capture_blob.py
	@echo "Rebuilding capture blob..."
	@mkdir -p build
	python3 tools/build_capture_blob.py Models/ --out-bin build/capture_data.bin --out-header src/capture_index.h --out-map build/capture_data.map

$(OBJECTS): captures

combined: all captures
	@echo "Creating combined firmware + capture blob..."
	python3 tools/make_combined_image.py --app $(BUILD_DIR)/$(TARGET_BIN) --captures build/capture_data.bin --out build/combined.bin

program-dfu: combined
	@echo "Flashing combined firmware to QSPI over DFU..."
	dfu-util -a 0 -s $(FLASH_ADDRESS):leave -D build/combined.bin -d ,0483:$(USBPID)

program: combined
	@echo "Flashing combined firmware to QSPI..."
	$(OCD) -s $(OCD_DIR) \
		-f $(PGM_DEVICE) \
		-c "set QUADSPI 1" \
		-f target/$(CHIPSET).cfg \
		-f openocd_daisy_qspi.cfg \
		-c "init" \
		-c "reset init" \
		-c "program build/combined.bin verify reset exit 0x90040000"

# Flash the Daisy bootloader to internal flash via debug probe.
program-boot-probe:
	@echo "Flashing Daisy bootloader via debug probe..."
	$(OCD) -s $(OCD_DIR) $(OCDFLAGS) \
		-c "program $(BOOT_BIN) verify reset exit $(INTERNAL_ADDRESS)"

# Additional targets for convenience
.PHONY: clean-all flash help

# Clean everything including libraries
clean-all: clean
	$(MAKE) -C $(LIBDAISY_DIR) clean
	$(MAKE) -C $(DAISYSP_DIR) clean

# Alias for program-dfu
flash: program-dfu

# Help target
help:
	@echo "AmpSim Build System"
	@echo "==================="
	@echo ""
	@echo "First-time setup (flash Daisy bootloader):"
	@echo "  Option A - USB only:"
	@echo "    1. Connect Daisy Seed via USB"
	@echo "    2. Hold BOOT button and press RESET, release both"
	@echo "    3. Run 'make program-boot'"
	@echo "  Option B - Debug probe (no buttons needed):"
	@echo "    1. Connect STLINK debug probe"
	@echo "    2. Run 'make program-boot-probe'"
	@echo ""
	@echo "Flashing firmware:"
	@echo "  Option A - USB only:"
	@echo "    1. Enter Daisy bootloader (press RESET)"
	@echo "    2. Run 'make program-dfu' within 2.5 seconds"
	@echo "  Option B - Debug probe (no USB or buttons needed):"
	@echo "    1. Connect STLINK debug probe"
	@echo "    2. Run 'make program'"
	@echo ""
	@echo "Common targets:"
	@echo "  make                  - Build the project"
	@echo "  make clean            - Clean build files"
	@echo "  make clean-all        - Clean including library builds"
	@echo "  make program-boot     - Flash bootloader via USB DFU"
	@echo "  make program-dfu      - Flash firmware via USB DFU"
	@echo "  make flash            - Alias for program-dfu"
	@echo "  make program          - Flash firmware via debug probe"
	@echo "  make program-boot-probe - Flash bootloader via debug probe"
