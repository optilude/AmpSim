# Development Guide

## Build System

### Prerequisites

**ARM GCC Toolchain:**
```bash
# macOS
brew install --cask gcc-arm-embedded

# Linux (Debian/Ubuntu)
sudo apt-get install gcc-arm-none-eabi
```

**dfu-util (USB flashing):**
```bash
# macOS
brew install dfu-util

# Linux
sudo apt-get install dfu-util
```

**STLINK debug probe (optional but recommended):**
- Simplifies flashing (no timing constraints)
- Enables debugging with GDB
- OpenOCD support

### Cloning with Submodules

```bash
# Clone with all submodules
git clone --recurse-submodules <repository-url>
cd AmpSim

# If already cloned without submodules:
git submodule update --init --recursive
```

**Submodules:**
- `libDaisy` - Hardware abstraction layer
- `DaisySP` - DSP library
- `NeuralAmpModelerCore` - NAM engine (oyama fork)

## Build Commands

```bash
# Build the project
make

# Clean build files (keep library builds)
make clean

# Clean everything including libraries
make clean-all

# Show help
make help
```

### Build Output
```
Memory region         Used Size  Region Size  %age Used
SRAM:                62KB        512KB       12%
QSPIFLASH:           731KB       7936KB      9.21%
```

## Patch Management

### Why Patches Are Mandatory

The `oyama/NeuralAmpModelerCore` fork targets bare-metal ARM systems, but still contains desktop-oriented code that won't compile on Daisy:

- **`thread_local` storage**: Not supported by ARM toolchain (no OS threads)
- Must be patched before building

### Applying Patches

**After cloning or updating submodules:**
```bash
./apply_patches.sh
```

This script:
- Applies `patches/remove_thread_local.patch`
- Idempotent (safe to run multiple times)
- Must be run before first build

### Verifying Patches

```bash
cd NeuralAmpModelerCore
git status
# Should show: modified: NAM/dsp.cpp
```

### Updating Patches

If you modify the submodule:
```bash
# Generate new patch
cd NeuralAmpModelerCore
git diff NAM/dsp.cpp > ../patches/remove_thread_local.patch

# Test patch application
git checkout NAM/dsp.cpp
git apply ../patches/remove_thread_local.patch
```

## Flashing Firmware

### First-Time Setup: Install Daisy Bootloader

**Option A - USB only:**
```bash
# 1. Connect Daisy Seed via USB
# 2. Hold BOOT button, press and release RESET, release BOOT
# 3. Run:
make program-boot
```

**Option B - Debug probe (recommended):**
```bash
# 1. Connect STLINK debug probe
# 2. Run:
make program-boot-probe
```

### Flashing Application Firmware

**Option A - USB DFU:**
```bash
# 1. Press RESET on the Daisy Seed
# 2. Within 2.5 seconds, run:
make program-dfu
```

**Option B - Debug probe (recommended):**
```bash
# 1. Connect STLINK debug probe
# 2. Run:
make program
```

**Advantages of debug probe:**
- No timing constraints
- Faster flashing
- Enables GDB debugging
- No button pressing required

## Model Conversion

### Captures To QSPI Blob

NAM models (`.nam`) and cabinet IRs (`.wav`) are converted to a QSPI capture blob before building:

```bash
# Convert captures and IRs
python3 tools/build_capture_blob.py Captures/ --irs IRs

# Then rebuild
make clean
make
```

### Adding New Models

1. **Obtain NAM capture:**
   - Train with [NeuralAmpModeler](https://github.com/sdatkinson/NeuralAmpModeler)
   - Or download from [NAM community](https://tonehunt.org)

2. **Add to Captures folder:**
   ```bash
   cp ~/Downloads/my_amp.nam Captures/
   ```

3. **Convert and rebuild:**
   ```bash
   python3 tools/build_capture_blob.py Captures/ --irs IRs
   make clean && make
   ```

4. **Flash to hardware:**
   ```bash
   make program
   ```

### Model Format

The conversion tool accepts:
- Exact NAM A2 Lite captures: WaveNet, 3 channels, 23 layers, 1871 weights
- 48 kHz mono/stereo WAV cabinet IRs, normalized and stored as 4096-sample captures
- Up to 128 total captures

It emits `build/capture_data.bin`, `src/capture_index.h`, and `build/capture_data.map`.

## Memory Constraints

### Why APP_TYPE = BOOT_SRAM?

The firmware uses a static A2 Lite runtime instead of the generic desktop NAM
stack, so the app fits in the BOOT_SRAM window. The bootloader copies the app
to SRAM, leaving QSPI available for the capture blob and settings persistence.

### Memory Optimization

**Pre-delay fix (already applied):**
- Original: 4 seconds = 1.5MB buffer
- Optimized: 200ms = 75KB buffer
- Savings: 1.4MB

**Heap allocation:**
- Reverb delay lines: ~75KB (heap)
- NAM model state: ~40KB (heap)
- Display buffer: ~1KB (SRAM)
- **Total heap**: ~116KB (well within limits)

### Binary Size Reduction

If binary grows too large:
1. Disable optional IR engine / CMSIS FFT sources
2. Disable MIDI support if unused
3. Optimize for size: `-Os` compiler flag

## Debugging

### Serial Debugging

```cpp
#include "daisy.h"

// In main():
hw.seed.StartLog(true);  // true = wait for USB serial

// Debug prints:
hw.seed.PrintLine("Model loaded: %s", modelName);
hw.seed.PrintLine("CPU cycles: %lu", cpuCycles);
```

**View serial output:**
```bash
# macOS/Linux
screen /dev/tty.usbmodem* 115200

# Or use Arduino Serial Monitor
```

### GDB with Debug Probe

```bash
# Terminal 1: Start OpenOCD
openocd -f openocd_daisy_qspi.cfg

# Terminal 2: Start GDB
arm-none-eabi-gdb build/AmpSim.elf
(gdb) target remote localhost:3333
(gdb) load
(gdb) continue
```

### CPU Measurement

```cpp
// Enable cycle counter (in main())
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

// Measure in AudioCallback
uint32_t start = DWT->CYCCNT;
// ... audio processing ...
uint32_t cycles = DWT->CYCCNT - start;
// Max allowed: 8,333 cycles (48kHz @ 400MHz)
// Target: <7,500 cycles (90% headroom)
```

## Build System Quirks

### Makefile Fixes

**Problem**: Daisy's Makefile creates spurious files (`-fasm`, `-fexceptions`)

**Solution**: Filter dependency flags:
```makefile
CPPFLAGS := $(filter-out -MMD -MP -MF%,$(CPPFLAGS))
```

### Include Path Order

**Critical**: `include/compat` must come first to shadow `std::mutex`

```makefile
C_INCLUDES = -Iinclude/compat \
             -Isrc \
             -IHardware \
             ...
```

**Why?** NAM library uses `std::mutex`, but bare-metal ARM doesn't provide it. The compat shim provides no-op mutex implementations.

### Exception Handling

**Enabled**: `-fexceptions` (required by NAM library)

**Trade-off**: +50KB binary size, but necessary for library compatibility

## Submodule Management

### Updating Submodules

```bash
# Update all submodules to latest
git submodule update --remote

# Re-apply patches
./apply_patches.sh

# Rebuild
make clean-all
make
```

### NeuralAmpModelerCore Fork

**Repository**: `https://github.com/oyama/NeuralAmpModelerCore`  
**Branch**: `add-rp2350-support`

**Why this fork?**
- Bare-metal optimizations
- No OS dependencies
- A2 fast path enabled
- Tested on similar hardware (RP2350 Cortex-M33)

**Differences from upstream:**
- Removes desktop-only features
- Optimized memory allocation
- No RTOS dependencies

## Development Workflow

### Iterative Development

```bash
# 1. Edit source files
vim src/main.cpp

# 2. Build
make

# 3. Flash
make program

# 4. Test on hardware

# 5. Repeat
```

### Before Committing

```bash
# 1. Ensure clean build
make clean && make

# 2. Test on hardware
make program

# 3. Check binary size hasn't grown unexpectedly
arm-none-eabi-size build/AmpSim.elf

# 4. Verify patches still apply
cd NeuralAmpModelerCore && git status

# 5. Commit
git add -A
git commit -m "Description of changes"
```

## Troubleshooting

### Build Fails: "thread_local" not defined

**Solution**: Apply patches
```bash
./apply_patches.sh
make clean
make
```

### Build Fails: Undefined references to std::mutex

**Solution**: Check include path order in Makefile
```makefile
# Must be first:
C_INCLUDES = -Iinclude/compat ...
```

### Flashing Fails: "No DFU devices found"

**Solution**: Timing is critical
- Press RESET
- Run `make program-dfu` **immediately** (within 2.5 seconds)
- Or use debug probe instead: `make program`

### No Audio Output

**Debug steps:**
1. Check bypass state (both effects OFF = true bypass)
2. Verify model loaded (check display)
3. Check input/output connections
4. Try enabling NAM (FS2)

### Audio Dropouts

**Possible causes:**
- CPU usage > 90%
- Interrupt latency
- QSPI flash access delays

**Solutions:**
1. Measure CPU usage (cycle counter)
2. Reduce reverb complexity
3. Use simpler NAM models
4. Increase audio block size (trade-off: higher latency)

## Performance Profiling

### CPU Usage Measurement

```cpp
// Global variables
uint32_t cpu_cycles = 0;
uint32_t max_cycles = 0;
uint32_t avg_cycles = 0;

// In AudioCallback:
uint32_t start = DWT->CYCCNT;
// ... audio processing ...
uint32_t end = DWT->CYCCNT;
cpu_cycles = end - start;
if (cpu_cycles > max_cycles) max_cycles = cpu_cycles;
avg_cycles = (avg_cycles * 99 + cpu_cycles) / 100;

// Display:
// "CPU: 5700/8333 cyc (68%)"
```

### Memory Usage

```bash
# Check SRAM/QSPI usage
arm-none-eabi-size --format=berkeley build/AmpSim.elf

# Check heap usage (runtime)
# Monitor sbrk() calls or use memory profiler
```

## References

- [Daisy Documentation](https://docs.daisy.audio/)
- [libDaisy GitHub](https://github.com/electro-smith/libDaisy)
- [DaisySP GitHub](https://github.com/electro-smith/DaisySP)
- [NeuralAmpModelerCore Fork](https://github.com/oyama/NeuralAmpModelerCore)
- [NAM Community Models](https://tonehunt.org)
