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
- Uses DaisySP Svf filters
- Optimized processing with frequency caching

### NAM Processor (`nam_processor.h/cpp`)
- A2-Lite architecture (fast path enabled)
- WaveNet-based neural network
- Model loaded from JSON at runtime
- ~1ms latency
- CPU usage: ~52% @ 48kHz

### Reverb Processor (`reverb_processor.h`)
- Dattorro 1997 plate reverb algorithm
- Mono input → stereo output
- Default parameters (MuleBox Flick settings):
  - Time Scale: 1.007500
  - Decay: 0.8
  - Tank Diffusion: 0.85
  - Modulation: Speed 0.8, Depth 1.5
- CPU usage: ~12-18% @ 48kHz
- Pre-delay: 200ms (reduced from 4s for memory constraints)

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

### Memory Regions
- **QSPI Flash**: 731KB binary (9.21% of 8MB)
  - Application code runs from QSPI (APP_TYPE = BOOT_QSPI)
  - Required due to large binary size (NAM + Eigen + JSON)
  
- **SRAM**: 62KB (12% of 512KB)
  - NAM model state
  - Reverb delay lines
  - Audio buffers
  - Display buffer

### Why BOOT_QSPI?
Binary too large for SRAM:
- NAM engine: ~400KB
- Eigen library: ~200KB
- JSON parser: ~50KB
- Application code: ~80KB
- **Total**: ~731KB > 512KB SRAM

Running from QSPI flash adds ~10-20 cycles latency per instruction fetch, but acceptable for this application.

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
- Reverb: 0-200ms pre-delay (user configurable)
- Total throughput: ~1.1ms + pre-delay

### Memory Footprint
- Code: 731KB (QSPI)
- Data: 62KB (SRAM)
- NAM model state: ~40KB
- Reverb delay lines: ~75KB (after optimization)
- Display buffer: ~1KB

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
    int32_t modelIndex;        // Current NAM model
    bool namEnabled;           // NAM on/off
    bool reverbEnabled;        // Reverb on/off
    float inputGain;           // Knob 0 position
    float outputVolume;        // Knob 1 position
    float reverbMix;           // Knob 2 position
    float bass;                // Knob 3 position
    float mid;                 // Knob 4 position
    float treble;              // Knob 5 position
};
```

### Storage Details
- **Location**: QSPI flash sector
- **Size**: ~32 bytes
- **Mechanism**: Daisy's PersistentStorage with wear leveling
- **Lifetime**: 10,000+ write cycles
- **Debounce**: Saves 2 seconds after last change

### Persistence Triggers
- Knob changes (debounced)
- Footswitch presses
- Model changes (encoder click)
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
- Svf filter for EQ
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
