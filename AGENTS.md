# AGENTS.md - AmpSim Development Guide

## Setup Critical Path

```bash
# Clone with submodules
git clone --recurse-submodules <repo-url>

# Optional: apply patches if using NeuralAmpModelerCore desktop/reference tools
./apply_patches.sh
```

Firmware no longer links NeuralAmpModelerCore. The submodule remains useful for
desktop/reference validation; its bare-metal patch is only needed if building it
for target firmware again.

## Model Conversion Workflow

NAM captures (.nam files) are converted to a QSPI capture blob before building.
Only exact A2 Lite captures are accepted: WaveNet, 3 channels, 23 layers, 1871
weights. Up to 128 captures are supported.

```bash
# Convert NAM captures to QSPI blob + metadata header
python3 tools/build_capture_blob.py Models/

# Then build
make
```

## Architecture & Memory

- **APP_TYPE = BOOT_SRAM**: Firmware is copied from QSPI to SRAM by the Daisy bootloader, leaving QSPI available for the capture blob and settings persistence.
- **QSPI layout**: app window `0x90040000..0x900bffff`; settings sector `0x900c0000..0x900c0fff`; capture blob starts at `0x900c1000`.
- **Static A2 runtime**: Firmware uses `src/nam_a2_runtime.h`, an allocation-free A2 Lite runtime derived from bkshepherd/nadavb work. It supports A2 Lite only.
- **Settings persistence**: Uses QSPI `PersistentStorage` under `BOOT_SRAM`; persist only selected capture index, NAM enabled, and reverb enabled. Physical knob positions are not persisted.
- **Submodule fork**: `oyama/NeuralAmpModelerCore` remains for desktop/reference work, not firmware linking.
- **No TLS**: Bare-metal has no `thread_local` support. Compat shim at `include/compat/mutex` shadows `std::mutex`
- **Reverb delay lines in SDRAM**: The Dattorro tank + input APFs + pre-delay are allocated from a 1 MiB `g_reverb_arena` in `.sdram_bss`. `main()` installs the arena via `InterpDelayArena::set()` BEFORE calling `reverbProcessor.init()`. Do not construct `Dattorro` at global scope — it must run after the arena is armed. See `src/reverb_arena.{h,cpp}` and `src/dattorro/dsp/delays/InterpDelay.hpp`.
- **Capture data in QSPI**: `tools/build_capture_blob.py` emits `build/capture_data.bin` and `src/capture_index.h`; `make program` pads the SRAM app image and appends the capture blob into `build/combined.bin`.

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
