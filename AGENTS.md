# AGENTS.md - AmpSim Development Guide

## Setup Critical Path

```bash
# Clone with submodules
git clone --recurse-submodules <repo-url>

# CRITICAL: Apply patches after cloning or updating submodules
./apply_patches.sh

# Verify patches applied
cd NeuralAmpModelerCore && git status
# Should show: modified: NAM/dsp.cpp
```

**Why patches are mandatory**: NeuralAmpModelerCore targets desktop systems with `thread_local` storage. Bare-metal ARM (Daisy Seed) requires patching to remove TLS (no OS threads). Build will fail without patches.

## Model Conversion Workflow

NAM captures (.nam files) must be converted to C++ headers before building:

```bash
# Convert NAM captures to header file
python3 tools/nam_to_header.py Captures/ > src/model_data.h

# Then build
make
```

## Architecture & Memory

- **APP_TYPE = BOOT_QSPI**: Firmware runs from QSPI flash (731KB binary, too large for SRAM)
- **Submodule fork**: Uses `oyama/NeuralAmpModelerCore` branch `add-rp2350-support` (not upstream)
- **No TLS**: Bare-metal has no `thread_local` support. Compat shim at `include/compat/mutex` shadows `std::mutex`

## Building & Flashing

```bash
# Build
make

# Clean
make clean          # Build files only
make clean-all      # Including library builds

# Flash via debug probe (recommended - no timing constraints)
make program

# Flash via USB DFU (requires timing - press RESET, then run within 2.5s)
make program-dfu
```

**First-time only**: Install Daisy bootloader with `make program-boot-probe` (debug probe) or `make program-boot` (USB).

## Build System Quirks

- Makefile filters Daisy's `-MMD -MP -MF` flags to prevent spurious `-fasm`/`-fexceptions` files
- Include path order matters: `include/compat` must come first to shadow `std::mutex`

## Testing

No automated tests. This is bare-metal firmware tested on hardware.

## Required Tools

- ARM GCC toolchain: `brew install --cask gcc-arm-embedded` (macOS)
- dfu-util: `brew install dfu-util` (macOS)
- STLINK debug probe (optional, but simplifies flashing)
