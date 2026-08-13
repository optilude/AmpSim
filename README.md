# AmpSim - Guitar Amp Simulator based on Daisy Seed

A guitar amp/cab simulator featuring NAM A2 Lite captures, cabinet IRs, and plate reverb in a guitar pedal format.

## Overview

AmpSim combines cutting-edge neural amp modeling with studio-quality reverb in a compact pedal format:

- **NAM A2-Lite**: Static A2 Lite runtime for authentic tube amp tones
- **Cabinet IR mode**: MuleBox-style partitioned convolution for cabinet IRs
- **Dattorro Plate Reverb**: Stereo plate reverb with smooth decay
- **True Bypass Relay**: Hardware bypass for pure analog signal path
- **6 Control Knobs**: Input gain, output volume, reverb mix, 3-band EQ
- **Rotary Encoder**: Browse and select from multiple amp models
- **State Persistence**: Selected capture and effect states are saved to QSPI
- **Stereo Output**: Reverb produces stereo widening effect

## Hardware

Built on the bkshepherd 125B PCB with Daisy Seed:

- **Daisy Seed 3** module (STM32H750 Cortex-M7 @ 400MHz)
- **2 Momentary Footswitches** for NAM and Reverb control
- **2 LEDs** for status indication
- **128x64 OLED Display** (SSD1306)
- **Rotary Encoder** with push button for model selection
- **6 Potentiometers** for real-time control
- **Stereo Audio I/O** (mono input, stereo output)
- **MIDI I/O** (future expansion)
- **True Bypass Relay** with mute circuit

## Quick Start

### First-Time Setup

1. **Build & flash firmware** (see Building & Flashing section below)
2. **Default settings on first boot**:
   - NAM: ON (LED 1 lit)
   - Reverb: ON at 30% mix (LED 0 lit)
   - All knobs at noon position

### Basic Operation

**Footswitches:**
- **FS1 (Left)**: Toggle reverb on/off
- **FS2 (Right)**: Toggle NAM amp modeling on/off

**Knobs:**
- Rotate to adjust parameters in real-time
- Physical knob positions are the source of truth and are not persisted

**Encoder:**
- **Rotate**: Browse available NAM models
- **Click**: Load the previewed model
- **Auto-revert**: Returns to current model after 10 seconds if not clicked

## Signal Chain

```
Input → Input Gain → NAM A2 Lite or Cabinet IR → 3-Band EQ → Reverb → Output Volume → Output
```

**Processing order rationale:**
1. **Input Gain** - Optimizes signal level for the NAM model
2. **NAM/IR engine** - Either A2 Lite amp modeling or cabinet IR, never both at once
3. **EQ** - Post-model tone shaping
4. **Reverb** - Adds space and depth (mono input → stereo output)
5. **Output Volume** - Final level control

## Controls

### Footswitches

| Switch | Function | LED | Bypass Behavior |
|--------|----------|-----|-----------------|
| **FS1 (Left)** | Reverb on/off | LED 0 (left) | Independent control |
| **FS2 (Right)** | Model engine on/off | LED 1 (right) | Independent control |

**Bypass Logic:**
- **Both OFF** → True bypass (analog signal path, no DSP)
- **Model ON, Reverb OFF** → selected NAM/IR + EQ only
- **Model OFF, Reverb ON** → Reverb only
- **Both ON** → selected NAM/IR + EQ + Reverb

### Knobs

| Position | Knob | Function | Range | Noon Position |
|----------|------|----------|-------|---------------|
| Top-Left | **0** | Input Gain | ±20dB | Unity (0dB) |
| Top-Middle | **1** | Output Volume | ±20dB | Unity (0dB) |
| Top-Right | **2** | Reverb Mix | 0-100% | 30% |
| Bottom-Left | **3** | Bass EQ | ±12dB @ 100Hz | Flat (0dB) |
| Bottom-Middle | **4** | Mid EQ | ±12dB @ 1kHz | Flat (0dB) |
| Bottom-Right | **5** | Treble EQ | ±12dB @ 4kHz | Flat (0dB) |

**Knob ranges:**
- **Gain knobs (0, 1)**: Fully CCW = -20dB, Noon = 0dB, Fully CW = +20dB
- **Reverb Mix (2)**: Fully CCW = 0% (dry), Fully CW = 100% (wet)
- **EQ knobs (3-5)**: Fully CCW = -12dB cut, Noon = flat, Fully CW = +12dB boost

### Rotary Encoder

- **Rotate** (CW/CCW): Browse NAM captures and IRs
- **Click** (press): Load the previewed model
- **Preview Mode**: Display shows `->` prefix before model name
- **Auto-revert**: After 10 seconds without clicking, returns to current model

## Display

The 128x64 OLED shows real-time information:

```
Line 0: [Model Name]
Line 1: [Variant Name]
Line 2: [MDL: ON ] [REV: ON ]
Line 3: In:+2.5dB Out:+3.2dB
Line 4: Rev: 30% EQ:B+2 M-1 T+4
Line 5: [Instructions]
```

**Display details:**
- Capture name and variant (NAM or IR)
- Model engine and Reverb status (ON/OFF)
- Input and output gain in dB
- Reverb mix as percentage
- EQ settings as ±dB values (B=Bass, M=Mid, T=Treble)
- Context-sensitive instructions

## State Persistence

The firmware runs with `BOOT_SRAM`, so the bootloader copies the app from QSPI
to SRAM before execution. The remaining QSPI space holds the capture blob and a
dedicated settings sector. This allows Daisy `PersistentStorage` to save state
without writing to the flash that code is executing from.

**What's persisted:**
- Current capture selection
- Model engine on/off state
- Reverb on/off state

**What's not persisted:**
- The six knob-controlled parameters. These follow the physical pot positions after boot.

**Storage layout:**
- App image: QSPI `0x90040000..0x900bffff` copied to SRAM at boot
- Settings sector: QSPI `0x900c0000..0x900c0fff`
- Capture blob: QSPI starting at `0x900c1000`

## Building & Flashing

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

**Clone with submodules:**
```bash
git clone --recurse-submodules <repository-url>
cd AmpSim

# If already cloned without submodules:
git submodule update --init --recursive

# Apply patches (REQUIRED before first build)
./apply_patches.sh
```

### Building

```bash
# Build the project
make

# Clean build files
make clean

# Clean everything including libraries
make clean-all
```

### Flashing

**First-time setup: Install Daisy bootloader**

*Option A - USB only:*
```bash
# 1. Connect Daisy Seed via USB
# 2. Hold BOOT button, press and release RESET, release BOOT
# 3. Run:
make program-boot
```

*Option B - Debug probe (recommended):*
```bash
# 1. Connect STLINK debug probe
# 2. Run:
make program-boot-probe
```

**Flashing application firmware:**

*Option A - USB DFU:*
```bash
# 1. Press RESET on the Daisy Seed
# 2. Within 2.5 seconds, run:
make program-dfu
```

*Option B - Debug probe (recommended):*
```bash
# 1. Connect STLINK debug probe
# 2. Run:
make program
```

## Adding New Models

### Obtaining NAM Models

1. **Train your own**: Use [NeuralAmpModeler](https://github.com/sdatkinson/NeuralAmpModeler) to capture your amp
2. **Download from community**: Browse [ToneHunt](https://tonehunt.org) for shared models

### Installing New Models

```bash
# 1. Copy .nam file to Models/ directory
cp ~/Downloads/my_amp.nam Models/

# 2. Rebuild and flash; the capture blob is generated automatically
make clean && make
make program
```

`tools/build_capture_blob.py` accepts exact A2 Lite NAM captures (WaveNet,
3 channels, 23 layers, 1871 weights) and 48 kHz mono/stereo WAV cabinet IRs.
Unsupported files fail the build. Up to 128 total captures are supported.

## Performance

**Resource usage (current BOOT_SRAM build, will re-measure on hardware):**
- **SRAM app image**:   ~129 KB text+data in the 480 KB SRAM app region
- **DTCMRAM**:          ~66 KB (A2 hot state/weights and runtime data)
- **RAM_D2**:           ~77 KB (A2 history buffer)
- **SRAM .bss**:       ~154 KB excluding SDRAM arena
- **SDRAM .bss**:      1024 KB reverb delay-line arena
- **Capture blob**:     ~31 KB for the current 2 A2 Lite captures plus 1 IR in QSPI
- **NAM runtime heap**: none in the audio path; A2 runtime uses fixed buffers
- **CPU usage**: NOT YET MEASURED — estimates in `docs/ARCHITECTURE.md`
  are extrapolated from bkshepherd's RP2350 numbers, not observed on this
  hardware. Expect to re-benchmark once the board is in hand.
- **Latency**: 1 ms audio block (48 samples @ 48 kHz), plus any configured reverb pre-delay

**Performance characteristics:**
- Cortex-M7 @ 400MHz, running from SRAM (`BOOT_SRAM`)
- Capture weights remain in QSPI and are copied into fixed A2 runtime buffers on model load
- NAM is processed once per 48-sample audio block
- Display updates throttled to 30 FPS for responsiveness

## Testing Your Build

### Automated desktop + build tests

Before flashing, run the desktop tests. These verify NAM loading, reverb
dry/wet math, EQ response, model conversion, and the Daisy build itself:

```bash
./test_integration.sh
```

Pass criteria:
- NAM: generated models load, output is finite/bounded, loudness normalization behaves
- IR: generated IR captures load and render the DI guitar sample with finite/bounded output
- Reverb: SDRAM arena consumed without fallback, mix knob crossfades linearly
- EQ: flat setting is bit-exact identity, +/- 12 dB targets are hit at
  centre frequencies within 2 dB
- Build: `BOOT_SRAM` app fits, capture blob fits QSPI, settings/captures do not overlap
- Model conversion: produces valid `capture_index.h` and `capture_data.bin` from `Models/`

### On-hardware checklist

After `make program`:

- [ ] Splash screen appears (proves reverb SDRAM init succeeded)
- [ ] Audio passes through (connect guitar to input, amp to output)
- [ ] FS1 toggles reverb, LED 0 indicates state
- [ ] FS2 toggles selected NAM/IR engine, LED 1 indicates state
- [ ] Both OFF = true bypass (no coloration)
- [ ] All 6 knobs respond smoothly
- [ ] Reverb mix knob crossfades between dry and wet (not a switch)
- [ ] Encoder browses NAM captures and IRs, click loads selection
- [ ] Model/effect persistence works after power cycle
- [ ] No audio dropouts during extended play
- [ ] EQ response is musical (bass/mid/treble)
- [ ] Reverb adds stereo width

## Troubleshooting

### No Audio Output

**Symptoms**: No sound from output

**Solutions:**
1. Check bypass state (both effects OFF = true bypass)
2. Press FS2 to enable the selected model engine
3. Verify input/output cables are connected
4. Check guitar volume knob
5. Try different capture/IR (encoder rotate + click)

### Model Won't Load

**Symptoms**: "Model Load Failed!" error on display

**Solutions:**
1. Ensure `capture_index.h` and `build/capture_data.bin` were generated:
   ```bash
   python3 tools/build_capture_blob.py Models/
   ```
2. Verify files exist in `Models/`
3. Rebuild firmware: `make clean && make`
4. Flash: `make program`

### Settings Not Saving

**Symptoms**: Settings reset on power cycle

**Solutions:**
1. Wait 2 seconds after changing model/effect state before power off
2. Confirm firmware was flashed as the combined image with `make program`
3. Rebuild and reflash if the settings schema changed

### Display Issues

**Symptoms**: Display shows garbage or doesn't update

**Solutions:**
1. Check OLED connections (hardware issue)
2. Rebuild firmware
3. Flash with `make program`

### Audio Dropouts

**Symptoms**: Clicks, pops, or dropouts during play

**Solutions:**
1. Use simpler NAM model (lower CPU usage)
2. Reduce reverb mix
3. Check for loose connections
4. Monitor CPU usage (may be hitting 90%+ threshold)

### Flashing Fails

**Symptoms**: "No DFU devices found"

**Solutions:**
1. **Timing**: Press RESET, then run `make program-dfu` **immediately** (within 2.5s)
2. **Use debug probe**: `make program` (no timing constraints)
3. **USB cable**: Try different USB port or cable

## Known Limitations

1. **Fixed EQ frequencies** - Bass (100 Hz), Mid (1 kHz), Treble (4 kHz) with fixed Q; not adjustable at runtime.
2. **Mono input only** - Hardware limitation (stereo input not wired).
3. **Brief mute during model changes** - Acceptable per design while weights are copied from QSPI and A2 state is reset.
4. **Reverb always produces stereo output** - When reverb is ON, the two output channels differ; when OFF, both channels carry the same mono signal.
5. **NAM and IR are mutually exclusive** - no NAM-into-cab-IR chain yet.
6. **No presets** - Only one setting bank (future enhancement).
7. **CPU/timing not yet measured on hardware.** All performance figures in this document should be re-verified once the board arrives.
8. **Long-press "settings mode" not yet implemented** - see `docs/Scope.md`.

## Architecture

```
AmpSim/
├── src/                    # Main application source
│   ├── main.cpp           # Main firmware entry point
│   ├── nam_processor.*    # A2 Lite processor wrapper
│   ├── nam_a2_runtime.h   # Static A2 Lite runtime
│   ├── convolution_engine.h # MuleBox-style partitioned IR convolution
│   ├── ir_processor.h     # Cabinet IR wrapper
│   ├── capture_index.h    # Generated QSPI capture metadata
│   ├── reverb_processor.h # Reverb wrapper
│   ├── settings.h         # State persistence
│   ├── gain_stage.h       # Input/output gain
│   ├── guitar_eq.h        # 3-band EQ
│   ├── constants.h        # Named constants
│   ├── helpers.h          # Helper functions
│   └── dattorro/          # Dattorro reverb engine
├── hardware/              # Hardware abstraction layer
│   ├── guitar_pedal_125b.h
│   └── guitar_pedal_125b.cpp
├── include/compat/        # Bare-metal compatibility shims
│   └── mutex              # No-op mutex for NAM library
├── tools/                 # Build and utility tools
│   ├── build_capture_blob.py
│   └── make_combined_image.py
├── patches/               # Submodule patches
│   └── remove_thread_local.patch
├── Models/                # NAM model files (.nam) and IR cabinet files (.wav)
├── docs/                  # Technical documentation
│   ├── ARCHITECTURE.md    # System design
│   ├── DEVELOPMENT.md     # Build system details
│   └── TESTING.md         # Validation procedures
├── libDaisy/              # Daisy hardware library (submodule)
├── DaisySP/               # Daisy DSP library (submodule)
├── NeuralAmpModelerCore/  # Desktop/reference NAM engine submodule
├── Makefile              # Build configuration
└── openocd_daisy_qspi.cfg # QSPI flash programming config
```

For detailed technical documentation, see `docs/ARCHITECTURE.md`, `docs/DEVELOPMENT.md`, and `docs/TESTING.md`.

## References

- [Daisy Documentation](https://docs.daisy.audio/)
- [bkshepherd 125B Hardware](https://github.com/bkshepherd/DaisySeedProjects)
- [NeuralAmpModeler](https://github.com/sdatkinson/NeuralAmpModeler)
- [NAM Community Models](https://tonehunt.org)
- [MuleBox Project](https://github.com/optilude/MuleBox)
- [Dattorro Reverb Paper](https://www.dafx.de/paper-archive/1997/P056.PDF)

## License

This project builds upon several open-source projects. See individual submodule directories for license details:
- NeuralAmpModelerCore: See `NeuralAmpModelerCore/LICENSE`
- libDaisy: See `libDaisy/LICENSE`
- DaisySP: See `DaisySP/LICENSE`
- Dattorro reverb: GPLv3 (see `src/dattorro/LICENSE-GPLv3.txt`)

## Contributing

Contributions welcome! Please:
1. Test thoroughly on hardware before submitting PRs
2. Follow the testing checklist in `docs/TESTING.md`
3. Ensure all changes pass build verification
4. Document any new features or API changes

## Support

For issues and questions:
- Open a GitHub issue for bugs
- Check `docs/TESTING.md` for troubleshooting
- See `docs/DEVELOPMENT.md` for build issues
