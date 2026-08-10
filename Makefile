# AmpSim - Guitar Amp Simulator Makefile
# For Electrosmith Daisy Seed on bkshepherd 125B platform

# Project Name and Sources
TARGET = AmpSim

# Enable NAM A2 fast path optimization, bare-metal support, and C++17
CPP_STANDARD = -std=gnu++17

# Sources
CPP_SOURCES = src/main.cpp \
              src/nam_processor.cpp \
              hardware/guitar_pedal_125b.cpp \
              NeuralAmpModelerCore/NAM/activations.cpp \
              NeuralAmpModelerCore/NAM/container.cpp \
              NeuralAmpModelerCore/NAM/conv1d.cpp \
              NeuralAmpModelerCore/NAM/convnet.cpp \
              NeuralAmpModelerCore/NAM/dsp.cpp \
              NeuralAmpModelerCore/NAM/get_dsp.cpp \
              NeuralAmpModelerCore/NAM/linear.cpp \
              NeuralAmpModelerCore/NAM/lstm.cpp \
              NeuralAmpModelerCore/NAM/ring_buffer.cpp \
              NeuralAmpModelerCore/NAM/util.cpp \
              NeuralAmpModelerCore/NAM/wavenet/a2_fast.cpp \
              NeuralAmpModelerCore/NAM/wavenet/model.cpp \
              NeuralAmpModelerCore/NAM/wavenet/slimmable.cpp

# Include paths (compat first to shadow std::mutex)
C_INCLUDES = -Iinclude/compat \
             -Ihardware \
             -INeuralAmpModelerCore \
             -INeuralAmpModelerCore/Dependencies/eigen \
             -INeuralAmpModelerCore/Dependencies/nlohmann

# Library Locations
LIBDAISY_DIR = libDaisy
DAISYSP_DIR = DaisySP

# Use Daisy bootloader: application is written to QSPI flash via DFU,
# then runs from QSPI flash (not copied to SRAM).
# Required because NAM + Eigen + JSON libraries are too large for SRAM.
APP_TYPE = BOOT_QSPI

# Use the 2000ms grace period bootloader
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_3-intdfu-2000ms.bin

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Fix Daisy Makefile bug: dependency flags create spurious files
# Daisy sets -MF"$(@:%.o=%.d)" in global CPPFLAGS, which expands to -MF""
# outside of build rules, causing GCC to create files named after the next flag
# The dependency generation is already handled in the pattern rules, so remove it here
CPPFLAGS := $(filter-out -MMD -MP -MF%,$(CPPFLAGS))

# Enable exceptions for NeuralAmpModelerCore (required even in bare-metal fork)
CPPFLAGS := $(filter-out -fno-exceptions,$(CPPFLAGS))
CPPFLAGS += -fexceptions

# Add NAM-specific defines
CPPFLAGS += -DNAM_ENABLE_A2_FAST=1 -DNAM_SHARED_PTR_ATOMIC_FREE_FUNCS=1 -DNAM_SAMPLE_FLOAT=1 -DNAM_USE_INLINE_GEMM=1

# Override libDaisy's `program` target to flash firmware directly to QSPI
# via the STLINK debug probe.
.PHONY: program program-boot-probe

program:
	@echo "Flashing firmware to QSPI..."
	$(OCD) -s $(OCD_DIR) \
		-f $(PGM_DEVICE) \
		-c "set QUADSPI 1" \
		-f target/$(CHIPSET).cfg \
		-f openocd_daisy_qspi.cfg \
		-c "init" \
		-c "reset init" \
		-c "program $(BUILD_DIR)/$(TARGET_BIN) verify reset exit 0x90040000"

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
