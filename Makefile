# AmpSim - Guitar Amp Simulator Makefile
# For Electrosmith Daisy Seed on bkshepherd 125B platform

# Project Name and Sources
TARGET = AmpSim
APP_SRC = src/main.cpp

# Sources
CPP_SOURCES = $(APP_SRC) \
              hardware/guitar_pedal_125b.cpp

# Include paths
C_INCLUDES = -Ihardware

# Library Locations
LIBDAISY_DIR = libDaisy
DAISYSP_DIR = DaisySP

# Use Daisy bootloader: application is written to QSPI flash via DFU,
# then copied to SRAM at boot. This enables programming both firmware
# and data in a single DFU flash operation.
APP_TYPE = BOOT_SRAM

# Use the 2000ms grace period bootloader
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_3-intdfu-2000ms.bin

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

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
