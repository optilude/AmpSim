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

NAM captures (.nam files) must be converted to C++ headers before building.
The generated header embeds each model's JSON as a raw C string literal so
it lives in `.rodata` (QSPI flash) rather than the SRAM heap.

```bash
# Convert NAM captures to header file
python3 tools/nam_to_header.py Captures/ > src/model_data.h

# Then build
make
```

## Architecture & Memory

- **APP_TYPE = BOOT_QSPI**: Firmware runs from QSPI flash (~736 KB binary, too large for SRAM)
- **Submodule fork**: Uses `oyama/NeuralAmpModelerCore` branch `add-rp2350-support` (not upstream)
- **No TLS**: Bare-metal has no `thread_local` support. Compat shim at `include/compat/mutex` shadows `std::mutex`
- **Reverb delay lines in SDRAM**: The Dattorro tank + input APFs + pre-delay total ~1 MB (measured); they are allocated from a 1 MiB `g_reverb_arena` in `.sdram_bss`. `main()` installs the arena via `InterpDelayArena::set()` BEFORE calling `reverbProcessor.init()`. Do not construct `Dattorro` at global scope — it must run after the arena is armed. See `src/reverb_arena.{h,cpp}` and `src/dattorro/dsp/delays/InterpDelay.hpp`.
- **NAM JSON in flash**: `tools/nam_to_header.py` emits models as raw C string literals so they live in `.rodata` (QSPI), not on the SRAM heap.

## Testing

Run the desktop test suite before flashing to catch regressions early:

```bash
./test_integration.sh   # NAM + reverb + EQ + build + model conversion
```

Individual desktop tests:
- `test_nam.cpp` — NAM load / process / loudness normalization
- `test_reverb.cpp` — arena consumption, wet/dry mix math, decay
- `test_eq.cpp` — flat identity, +/-12 dB at target frequencies

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

## Required Tools

- ARM GCC toolchain: `brew install --cask gcc-arm-embedded` (macOS)
- dfu-util: `brew install dfu-util` (macOS)
- STLINK debug probe (optional, but simplifies flashing)
