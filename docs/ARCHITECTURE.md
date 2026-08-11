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
- Static A2 Lite WaveNet runtime derived from bkshepherd/nadavb work
- Supports only exact A2 Lite captures: 3 channels, 23 layers, 1871 weights
- Capture weights are stored in a QSPI blob and copied into fixed runtime buffers on model load
- No JSON parsing, Eigen, exceptions, or heap allocation in firmware NAM path
- ~1ms block latency

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

### Measured memory usage (2 A2-Lite captures, 48 kHz)

| Region | Size | Contents |
| --- | --- | --- |
| SRAM app | ~129 KB | BOOT_SRAM text+data copied from QSPI |
| DTCMRAM | ~66 KB | A2 hot weights/state and runtime data |
| RAM_D2 | ~77 KB | A2 history buffer |
| SRAM .bss | ~154 KB | audio buffers, HW state, display framebuffer, scratch |
| SDRAM .bss | 1024 KB | reverb delay arena (`g_reverb_arena`) |
| RAM_D2_DMA | 17 KB | libDaisy DMA buffers |
| QSPI capture blob | ~15 KB | current two packed A2 Lite captures |

NAM A2 Lite uses fixed buffers. Capture changes copy 1871 float weights from
QSPI into the static A2 runtime and prewarm/reset state.

### Why BOOT_SRAM?
The generic NeuralAmpModelerCore path was too large for `BOOT_SRAM`, but the
static A2 Lite runtime brings the app image down to roughly 129 KB. The Daisy
bootloader copies the app from QSPI to SRAM, leaving QSPI available for the
capture blob and Daisy `PersistentStorage` settings writes.

### Why SDRAM for the reverb?
The Dattorro tank delay lines with `maxTimeScale=4.0` and 48 kHz sample
rate total ~853 KB. Two models' NAM state + Eigen scratch use most of the
SRAM heap already; adding the reverb tank on top would exhaust SRAM and
throw `std::bad_alloc` at boot. The delay lines are placed in the 64 MB
external SDRAM via a compile-time arena; on-chip SRAM stays free for
NAM's hot data (which is far more cache-sensitive).

### Capture Blob Storage
`tools/build_capture_blob.py` converts exact A2 Lite `.nam` files into
`build/capture_data.bin` and emits `src/capture_index.h`. The app image is
padded to 512 KB and the capture blob is appended for flashing at
`0x900c1000`. The script fails if more than 128 captures are present, if a NAM
file is not exact A2 Lite, or if any QSPI region would overlap/overflow.

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
- App text+data: ~129 KB in SRAM app window
- SRAM/DTCM/RAM_D2 fixed runtime data as listed above
- No firmware NAM heap allocation

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
become the source of truth after ADC warm-up at boot.

### Storage Details
- **Location**: QSPI settings sector `0x900c0000..0x900c0fff`
- **Size**: small raw settings struct plus Daisy storage state word
- **Mechanism**: Daisy `PersistentStorage` under `BOOT_SRAM`
- **Debounce**: saves 2 seconds after selected capture/effect state changes

### Persistence Triggers
- Footswitch presses
- Model changes (encoder click)
- Settings validated on load (bounds checking)

## Build System Quirks

### Makefile Fixes
- Filters Daisy's `-MMD -MP -MF` flags (prevents spurious `-fasm`/`-fexceptions` files)
- Include path order: `include/compat` must be first (shadows `std::mutex`)
- Firmware NAM path is exception-free; generic NeuralAmpModelerCore is not linked

### Compilation Flags
```makefile
-std=gnu++17
APP_TYPE = BOOT_SRAM
```

## Submodule Dependencies

### NeuralAmpModelerCore
- Kept as a submodule for desktop/reference validation.
- Not linked into firmware.

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
