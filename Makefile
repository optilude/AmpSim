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

# The capture blob and src/capture_index.h are generated from Models/.
#
# Two layers of change detection, because this used to force a full rebuild on
# every single make: the generator was phony, so it reran and rewrote
# capture_index.h -- which every translation unit includes -- each time. Ten
# seconds of compiling is long enough to miss the bootloader's DFU window.
#
#   1. The generator runs on every make, but it is 0.15 s and it only rewrites
#      an output whose content actually changed.
#   2. Objects depend on the generated header as an ordinary prerequisite, so
#      they rebuild exactly when its content moved -- not when a model file was
#      merely touched, and not just because make ran.
#
# `captures` is an order-only prerequisite (the `|`): it is guaranteed to run
# before anything is compiled, but being phony it never itself forces a
# rebuild. That is what stops the generator from invalidating the world.
CAPTURE_HDR = src/capture_index.h
CAPTURE_BIN = build/capture_data.bin
COMBINED_BIN = build/combined.bin

captures:
	@mkdir -p build
	@python3 tools/build_capture_blob.py Models/ --out-bin $(CAPTURE_BIN) --out-header $(CAPTURE_HDR) --out-map build/capture_data.map > /dev/null

$(CAPTURE_HDR) $(CAPTURE_BIN): | captures

$(OBJECTS): $(CAPTURE_HDR)

$(COMBINED_BIN): all $(CAPTURE_BIN) tools/make_combined_image.py
	@echo "Creating combined firmware + capture blob..."
	python3 tools/make_combined_image.py --app $(BUILD_DIR)/$(TARGET_BIN) --captures $(CAPTURE_BIN) --out $@

# Build the flashable image without flashing it. Useful right before a DFU
# session so the compile is already done when the window opens.
combined: $(COMBINED_BIN)

# -w waits for the device to enumerate instead of failing if it is not already
# in DFU mode, so the bootloader's window does not have to be won in a race
# against the build. Press reset whenever; this will pick it up.
program-dfu: $(COMBINED_BIN)
	@echo "Waiting for Daisy in DFU mode -- press RESET on the pedal now."
	dfu-util -w -a 0 -s $(FLASH_ADDRESS):leave -D $(COMBINED_BIN) -d ,0483:$(USBPID)

program: $(COMBINED_BIN)
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
	@echo "Flashing firmware (always flashes app + capture blob together):"
	@echo "  Option A - USB only:"
	@echo "    1. Run 'make program-dfu' -- it builds, then waits for the device"
	@echo "    2. Press RESET on the pedal when it says so (no time pressure)"
	@echo "  Option B - Debug probe (no USB or buttons needed):"
	@echo "    1. Connect STLINK debug probe"
	@echo "    2. Run 'make program'"
	@echo ""
	@echo "Common targets:"
	@echo "  make                  - Build the project"
	@echo "  make combined         - Build build/combined.bin without flashing"
	@echo "  make clean            - Clean build files"
	@echo "  make clean-all        - Clean including library builds"
	@echo "  make program-boot     - Flash bootloader via USB DFU"
	@echo "  make program-dfu      - Flash firmware via USB DFU"
	@echo "  make flash            - Alias for program-dfu"
	@echo "  make program          - Flash firmware via debug probe"
	@echo "  make program-boot-probe - Flash bootloader via debug probe"
