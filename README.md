# AmpSim - Guitar Amp Simulator based on Daisy Seed

A guitar amp simulator featuring neural amp modeling (NAM A2) and plate reverb in a guitar pedal format.

## Overview

AmpSim combines cutting-edge neural amp modeling with studio-quality reverb in a compact pedal format:

- **NAM A2-Lite**: Neural network-based amp captures for authentic tube amp tones
- **Dattorro Plate Reverb**: Stereo plate reverb with smooth decay
- **True Bypass Relay**: Hardware bypass for pure analog signal path
- **6 Control Knobs**: Input gain, output volume, reverb mix, 3-band EQ
- **Rotary Encoder**: Browse and select from multiple amp models
- **State Persistence**: All settings saved automatically to flash memory
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
- Settings auto-save 2 seconds after changes

**Encoder:**
- **Rotate**: Browse available NAM models
- **Click**: Load the previewed model
- **Auto-revert**: Returns to current model after 10 seconds if not clicked

## Signal Chain

```
Input → Input Gain → NAM → 3-Band EQ → Reverb → Output Volume → Output
```

**Processing order rationale:**
1. **Input Gain** - Optimizes signal level for the NAM model
2. **NAM** - Neural amp modeling (the main tone)
3. **EQ** - Post-amp tone shaping (like an amp's tone stack)
4. **Reverb** - Adds space and depth (mono input → stereo output)
5. **Output Volume** - Final level control

## Controls

### Footswitches

| Switch | Function | LED | Bypass Behavior |
|--------|----------|-----|-----------------|
| **FS1 (Left)** | Reverb on/off | LED 0 (left) | Independent control |
| **FS2 (Right)** | NAM on/off | LED 1 (right) | Independent control |

**Bypass Logic:**
- **Both OFF** → True bypass (analog signal path, no DSP)
- **NAM ON, Reverb OFF** → NAM + EQ only (no reverb)
- **NAM OFF, Reverb ON** → Reverb only (no amp modeling)
- **Both ON** → Full chain (NAM + EQ + Reverb)

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

- **Rotate** (CW/CCW): Browse NAM models alphabetically
- **Click** (press): Load the previewed model
- **Preview Mode**: Display shows `->` prefix before model name
- **Auto-revert**: After 10 seconds without clicking, returns to current model

## Display

The 128x64 OLED shows real-time information:

```
Line 0: [Model Name]
Line 1: [Variant Name]
Line 2: [NAM: ON ] [REV: ON ]
Line 3: In:+2.5dB Out:+3.2dB
Line 4: Rev: 30% EQ:B+2 M-1 T+4
Line 5: [Instructions]
```

**Display details:**
- Model name and variant (from .nam file)
- NAM and Reverb status (ON/OFF)
- Input and output gain in dB
- Reverb mix as percentage
- EQ settings as ±dB values (B=Bass, M=Mid, T=Treble)
- Context-sensitive instructions

## State Persistence

All user settings are automatically saved to flash memory:

**What's saved:**
- Current NAM model selection
- NAM on/off state
- Reverb on/off state
- All 6 knob positions

**When it saves:**
- 2 seconds after any change (debounced)
- Immediately on footswitch press
- Immediately on model change

**Persistence:**
- Settings survive power cycles
- Stored in QSPI flash with wear leveling
- 10,000+ write cycles lifespan
- Validates settings on load (handles corruption)

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

### Obtaining NAM Captures

1. **Train your own**: Use [NeuralAmpModeler](https://github.com/sdatkinson/NeuralAmpModeler) to capture your amp
2. **Download from community**: Browse [ToneHunt](https://tonehunt.org) for shared models

### Installing New Models

```bash
# 1. Copy .nam file to Captures/ directory
cp ~/Downloads/my_amp.nam Captures/

# 2. Convert to C++ header
python3 tools/nam_to_header.py Captures/ > src/model_data.h

# 3. Rebuild and flash
make clean && make
make program
```

The conversion tool extracts the A2-Lite model (submodel 0) from the .nam file and generates a C++ header with all available models.

## Performance

**Resource usage:**
- **Binary size**: 731KB (9% of QSPI flash)
- **SRAM usage**: 62KB (12% of 512KB)
- **CPU usage**: ~68-74% at 48kHz
- **Latency**: ~1ms for NAM processing

**Performance characteristics:**
- Cortex-M7 @ 400MHz provides ~30% CPU headroom
- QSPI flash execution adds ~10-20 cycles per instruction fetch
- Reverb pre-delay optimized to 200ms (from 4s) for memory efficiency
- Display updates throttled to 30 FPS for responsiveness

## Testing Your Build

Complete this checklist after flashing:

- [ ] Audio passes through (connect guitar to input, amp to output)
- [ ] FS1 toggles reverb, LED 0 indicates state
- [ ] FS2 toggles NAM, LED 1 indicates state
- [ ] Both OFF = true bypass (no coloration)
- [ ] All 6 knobs respond smoothly
- [ ] Encoder browses models, click loads model
- [ ] Settings persist after power cycle (wait 2s before power off)
- [ ] No audio dropouts during extended play
- [ ] EQ response is musical (bass/mid/treble)
- [ ] Reverb adds stereo width

## Troubleshooting

### No Audio Output

**Symptoms**: No sound from output

**Solutions:**
1. Check bypass state (both effects OFF = true bypass)
2. Press FS2 to enable NAM
3. Verify input/output cables are connected
4. Check guitar volume knob
5. Try different amp model (encoder rotate + click)

### Model Won't Load

**Symptoms**: "Model Load Failed!" error on display

**Solutions:**
1. Ensure `model_data.h` was generated:
   ```bash
   python3 tools/nam_to_header.py Captures/ > src/model_data.h
   ```
2. Verify .nam files exist in `Captures/`
3. Rebuild firmware: `make clean && make`
4. Flash: `make program`

### Settings Not Saving

**Symptoms**: Settings reset on power cycle

**Solutions:**
1. Wait 2 seconds after changes before power off
2. Check QSPI flash isn't corrupted
3. Try factory reset: rebuild and flash fresh firmware

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

1. **Fixed EQ frequencies** - Bass (100Hz), Mid (1kHz), Treble (4kHz) are not adjustable
2. **Mono input only** - Hardware limitation (stereo input not wired)
3. **Brief mute during model changes** - Acceptable per design (model loading takes time)
4. **Reverb is stereo output only** - When reverb is ON, output is stereo; when OFF, mono
5. **No IR loader** - Cabinet simulation not implemented (future enhancement)
6. **No presets** - Only one setting bank (future enhancement)

## Architecture

```
AmpSim/
├── src/                    # Main application source
│   ├── main.cpp           # Main firmware entry point
│   ├── nam_processor.*    # NAM model wrapper
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
│   └── nam_to_header.py   # Model converter
├── patches/               # Submodule patches
│   └── remove_thread_local.patch
├── Captures/              # NAM model files (.nam)
├── docs/                  # Technical documentation
│   ├── ARCHITECTURE.md    # System design
│   ├── DEVELOPMENT.md     # Build system details
│   └── TESTING.md         # Validation procedures
├── libDaisy/              # Daisy hardware library (submodule)
├── DaisySP/               # Daisy DSP library (submodule)
├── NeuralAmpModelerCore/  # NAM engine (submodule, oyama fork)
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
