# AmpSim Architecture

## System Overview

AmpSim is a guitar amp simulator running on the Electro-Smith Daisy Seed (STM32H750 Cortex-M7 @ 400MHz) with NAM neural amp modeling and Dattorro plate reverb.

## Signal Chain

```
Input → Input Gain → NAM → 3-Band EQ → Reverb → Output Volume → Output
           ↓           ↓                 ↓
        ±20dB      A2-Lite          Mono→Stereo
```

**Processing Order Rationale:**
1. **Input Gain**: Optimizes signal level for NAM model input
2. **NAM**: Neural amp model (A2-Lite architecture)
3. **EQ**: Post-amp tone shaping (mimics amp tone stack placement)
4. **Reverb**: Dattorro plate reverb (mono input, stereo output)
5. **Output Volume**: Final level control before output

## Audio Components

### Input/Output Gain Stage (`gain_stage.h`)
- Range: ±20dB
- Smooth parameter transitions (prevents zipper noise)
- Noon position = unity gain (0dB)

### 3-Band EQ (`guitar_eq.h`)
- **Bass**: Low shelf at 100Hz, ±12dB
- **Mid**: Peaking filter at 1kHz, ±12dB
- **Treble**: High shelf at 4kHz, ±12dB
- Uses RBJ cookbook biquad filters
- Coefficients are recomputed only when knob changes pass the deadband

### NAM Processor (`nam_processor.h/cpp`)
- A2-Lite architecture (fast path enabled)
- WaveNet-based neural network
- Model loaded from JSON at runtime
- ~1ms latency
- CPU usage: ~52% @ 48kHz

### Reverb Processor (`reverb_processor.h`, `reverb_arena.{h,cpp}`)
- Dattorro 1997 plate reverb algorithm
- Mono input -> stereo output; dry/wet crossfade applied at the output
- Default parameters (MuleBox / Flick "plate" preset):
  - Time Scale: 1.007500
  - Decay: 0.8
  - Tank Diffusion: 0.85
  - Modulation: Speed 0.8, Depth 1.5
- CPU usage: ~12-18% @ 48kHz (unmeasured on hardware yet)
- Pre-delay: configurable in the Dattorro engine; current preset uses 0 ms
- Delay-line memory: held in SDRAM via a static
  1 MiB arena (`g_reverb_arena` in `.sdram_bss`). See `src/reverb_arena.h`.
  The `Dattorro` object is constructed only after `InterpDelayArena::set()`
  is called from `main()` so its delay buffers land in SDRAM instead of
  the SRAM heap.

## Bypass System

### True Bypass Relay
- Hardware relay on D1, mute on D12
- Controlled by `SetAudioBypass()` and `SetAudioMute()`
- Engaged only when **both** NAM and Reverb are OFF
- 30ms mute timing before/after switching (pop prevention)

### Bypass Logic States
```
NAM ON  + Rev ON  → DSP Active (Full Chain) → Relay OFF
NAM ON  + Rev OFF → DSP Active (NAM + EQ)   → Relay OFF
NAM OFF + Rev ON  → DSP Active (Reverb)     → Relay OFF
NAM OFF + Rev OFF → True Bypass              → Relay ON
```

**Rationale**: Allows independent control of NAM and Reverb while maintaining true analog bypass when both effects are disabled.

## Memory Architecture

### Measured memory usage (2 A2-Lite models loaded, 48 kHz)

| Region | Size | Contents |
| --- | --- | --- |
| QSPIFLASH | 736 KB | code + rodata (including model JSON as raw C strings) |
| SRAM .bss | ~74 KB | audio buffers, HW state, display framebuffer, scratch |
| SRAM heap (free) | ~438 KB | reserved for NAM model tensors and libc scratch |
| SDRAM .bss | 1024 KB | reverb delay arena (`g_reverb_arena`) |
| RAM_D2_DMA | 17 KB | libDaisy DMA buffers |

NAM A2-Lite adds ~300 KB of heap allocations on the SRAM heap during
`loadModel()` (steady state; peak during JSON parse can be ~500 KB before
the parser's temporaries are freed).

### Why BOOT_QSPI?
Binary is 731 KB - larger than SRAM (512 KB) even without runtime data.
Running from QSPI flash adds ~10-20 cycles latency per instruction fetch;
acceptable for this application. The hot NAM inner loops still hit the
D-cache and I-cache.

`BOOT_SRAM` was tested and does not fit: `.text` uses roughly 734 KB against a
480 KB SRAM application region, before considering NAM runtime heap. This is why
stock QSPI `PersistentStorage` cannot be used directly for settings while the
program executes from QSPI.

### Why SDRAM for the reverb?
The Dattorro tank delay lines with `maxTimeScale=4.0` and 48 kHz sample
rate total ~853 KB. Two models' NAM state + Eigen scratch use most of the
SRAM heap already; adding the reverb tank on top would exhaust SRAM and
throw `std::bad_alloc` at boot. The delay lines are placed in the 64 MB
external SDRAM via a compile-time arena; on-chip SRAM stays free for
NAM's hot data (which is far more cache-sensitive).

### Model JSON storage
NAM model JSON is emitted by `tools/nam_to_header.py` as C raw string
literals (`R"namjson(...)namjson"`), which land in `.rodata` (QSPI). This
means models cost zero heap until `loadModel()` runs and parses them.
Previously the JSON was `std::string`-initialized at global init, which
allocated on the SRAM heap before `main()` could even show a splash.

## Performance Characteristics

### CPU Usage (Estimated @ 48kHz, 400MHz)
- NAM A2-Lite: ~52% (3,400 cycles/sample)
- Reverb: ~15% (800-1,200 cycles/sample)
- EQ: ~4% (300 cycles/sample)
- Overhead: ~3%
- **Total**: ~68-74%
- **Headroom**: ~26-32%

### Latency
- NAM processing: ~1ms (48 samples)
- EQ: Negligible (<0.1ms)
- Reverb: preset-dependent pre-delay
- Total throughput: ~1.1ms + configured pre-delay

### Memory Footprint (measured)
- Code+rodata: 736 KB (QSPI)
- .bss:        74 KB (SRAM) + 1024 KB (SDRAM arena)
- Heap runtime (per NAM model loaded): ~300 KB on SRAM heap

## Threading Model

### Single-Threaded Firmware
- **Audio Callback**: Runs on interrupt (highest priority)
  - 48kHz sample rate
  - 48 samples per block (1ms blocks)
  - Must complete within 1ms (8,333 cycles available)
  
- **Main Loop**: Foreground task (lower priority)
  - UI controls (knobs, switches, encoder)
  - Display updates (throttled to 30 FPS)
  - Settings persistence
  - Non-blocking operations only

### No RTOS
Bare-metal firmware for deterministic timing:
- No thread_local storage (not supported by ARM toolchain)
- No OS threads
- Interrupt-driven audio
- Simple mutex shim for NAM library compatibility

## State Persistence

### PersistentSettings Structure
```cpp
struct PersistentSettings {
    uint32_t schemaVersion;     // Settings schema guard
    int32_t modelIndex;        // Current NAM model
    uint8_t namEnabled;        // NAM on/off
    uint8_t reverbEnabled;     // Reverb on/off
};
```

Knob positions are not persisted. The physical pots are absolute controls and
become the source of truth after ADC warm-up at boot. Runtime persistence is
currently disabled while the firmware executes from QSPI because libDaisy's QSPI
driver rejects erase/write in that mode.

### Storage Details
- **Location**: pending BOOT_QSPI-safe storage backend, preferably internal flash
- **Size**: ~32 bytes
- **Mechanism**: pending; do not use QSPI `PersistentStorage` directly while running from QSPI
- **Debounce**: state changes are still dirty-tracked for a future storage hook

### Persistence Triggers
- Footswitch presses (dirty-tracked)
- Model changes (encoder click, dirty-tracked)
- Settings validated on load (bounds checking)

## Build System Quirks

### Makefile Fixes
- Filters Daisy's `-MMD -MP -MF` flags (prevents spurious `-fasm`/`-fexceptions` files)
- Include path order: `include/compat` must be first (shadows `std::mutex`)
- Enabled exceptions: `-fexceptions` (required by NAM library)

### Compilation Flags
```makefile
-std=gnu++17
-DNAM_ENABLE_A2_FAST=1
-DNAM_SHARED_PTR_ATOMIC_FREE_FUNCS=1
-DNAM_SAMPLE_FLOAT=1
-DNAM_USE_INLINE_GEMM=1
```

## Submodule Dependencies

### NeuralAmpModelerCore
- **Fork**: `oyama/NeuralAmpModelerCore` (branch `add-rp2350-support`)
- **Patch Required**: Remove `thread_local` storage (`patches/remove_thread_local.patch`)
- **Why Fork**: Bare-metal optimizations, no OS dependencies

### libDaisy
- Hardware abstraction layer
- Display driver (SSD1306 OLED)
- QSPI flash driver
- PersistentStorage implementation

### DaisySP
- DSP library
- Audio utilities
- Audio utilities

## Stereo Reverb Behavior

The Dattorro plate reverb produces **stereo output** from mono input:

### Stereo Width
- Left and right channels receive different reverb signals
- Creates natural stereo widening effect
- Based on tank delay line spacing and modulation

### Implementation
```cpp
reverbProcessor.process(monoInput, &leftOut, &rightOut);
```

**When Reverb Enabled**:
- Out L = Reverb L (different from R)
- Out R = Reverb R (different from L)

**When Reverb Disabled**:
- Out L = Dry signal
- Out R = Dry signal (identical)

This creates a **stereo image shift** when toggling reverb - this is intentional and desirable for plate reverb effects.

## Power Consumption

Estimated based on STM32H750 specs:
- **Idle**: ~150mA @ 5V
- **Full Load**: ~250mA @ 5V
- **Display**: ~20mA
- **Audio**: ~30mA
- **Total**: ~300mA (within USB power budget)

## Debugging

### CPU Measurement
```cpp
// Enable cycle counter
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

// Measure in AudioCallback
uint32_t start = DWT->CYCCNT;
// ... audio processing ...
uint32_t cycles = DWT->CYCCNT - start;
// Expected: ~5,700 cycles max (68% of 8,333 available)
```

### Memory Debugging
- Check SRAM usage: `arm-none-eabi-size build/AmpSim.elf`
- Monitor heap: Watch for allocation failures in reverb initialization
- Stack usage: Reserve ~8KB for main stack

## Future Enhancements

Potential improvements:
1. IR loader for cabinet simulation
2. Parametric EQ with frequency sweep
3. Preset system (multiple setting banks)
4. MIDI control
5. Model crossfading (seamless transitions)
6. Boot animation
7. CPU usage display
8. Stereo input support (hardware mod required)
