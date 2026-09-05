# AmpSim - Guitar Amp Simulator based on Daisy Seed

A guitar amp/cab simulator featuring NAM A2 Lite captures, cabinet IRs, and plate reverb in a guitar pedal format. Includes a tuner.

Heavily inspired by https://github.com/bkshepherd/DaisySeedProjects, and using its excellent hardware platform.

## Overview

AmpSim combines neural amp modeling with studio-quality reverb in a compact pedal format:

- **NAM A2-Lite**: Static A2 Lite runtime for authentic tube amp tones
- **Cabinet IR mode**: Load impulse responses for cabinet simulation only
- **Combined models**: Pair a NAM head capture with a cabinet IR in one selectable model
- **Dattorro Plate Reverb**: Stereo plate reverb with smooth decay.
- **Simple Reverb**: An alternative, simpler reverb is also available.
- **True Bypass Relay**: Or buffered relay, via a setting, if desired.
- **6 Control Knobs**: Input gain, output volume, reverb mix, 3-band EQ
- **Rotary Encoder**: Browse and select from multiple amp models
- **State Persistence**: Selected capture and effect states are saved between restarts.
- **Stereo Output**: Or mono, selectable via the settings menu.

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

See [hardware build instructions](https://github.com/bkshepherd/DaisySeedProjects/blob/main/Hardware/GuitarPedal125b/docs/README.md) for details of how to order the PCB and assemble the hardware.

## Quick Start

First, you must build the firmware with the models and IRs you wish to include, and flash this to the Daisy Seed over a USB connection. Once this is done, you can disconnect the USB cable and use the pedal as normal.

See below for build instructions.

### Basic Operation

**Footswitches:**
- **FS1 (Left)**: Toggle NAM or IR amp modeling on/off
- **FS2 (Right)**: Toggle reverb on/off. Long-press to enable tuner.

**Knobs:**

The top three knobs are:

1. Input gain
2. Output volume
3. Reverb mix

The bottom three knobs are a three-band EQ: Bass, Middle, Treble. At noon, the EQ is flat.

**Rotary encoder:**
- **Rotate**: Browse available NAM models
- **Click**: Show the full current model name, or load the previewed model while browsing
- **Long-press**: Enter the settings menu

**Bypass**

By default, the pedal operates with true stereo bypass. You can enable buffered bypass or buffered mono-to-stereo (dual mono) in the settings menu.

## Signal Chain

```
Input → Input Gain → NAM A2 Lite → Cabinet IR → 3-Band EQ → Reverb → Output Volume → Output
```

Standalone models skip the unused NAM or IR stage. Combined models always run
the NAM capture into the IR before EQ and reverb.

# Building & Flashing

Before you can use the pedal, you need to build your own firmware and flash it onto a Daisy Seed module installed on the DaisySeedProjects 125B PCB.

## Prerequisites

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

The callback-noise notch filter is enabled by default. To compile it out, clean
first and pass `CALLBACK_NOISE_FILTER=0` to both the build and flash commands:

```bash
make clean
make CALLBACK_NOISE_FILTER=0
make program-dfu CALLBACK_NOISE_FILTER=0
```

Use the normal commands, or explicitly set `CALLBACK_NOISE_FILTER=1`, to restore
the filter. Make does not track changes to command-line build options, so always
run `make clean` when switching this setting.

### Flashing

**First-time setup: Install Daisy bootloader**

*Option A - USB only:*
```bash
# 1. Connect Daisy Seed via USB
# 2. Hold BOOT button, press and release RESET, release BOOT
# 3. Run:
make program-boot
```

*Option B - Debug probe (better, if you have one):*
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

*Option B - Debug probe:*
```bash
# 1. Connect STLINK debug probe
# 2. Run:
make program
```

Once the firmware has been flashed, you can use the Settings menu (long-press the rotary encoder) and click Reset instead of using the reset button on the Daisy Seed. This makes it easier to flash new firmware without taking the lid off the back of the pedal.

## Adding New Models

Models and IRs live in the `Models/` directory of the build. Subdirectories represent models and `.nam` or `.wav` files therein represents different variants of each. The model name is taken from the subdirectory name, and the variant name from the filename.

Files placed in this directory will be processed by the build (e.g. `make all`) and prepared for flashing onto the firmware.

To create a combined head-and-cab model, put a `.nam` capture and a `.wav` IR
with the same basename in the same model directory. For example,
`Models/My Amp/Clean.nam` and `Models/My Amp/Clean.wav` appear once in the UI as
`My Amp / Clean` and process NAM into IR. The status bar shows `NAM+IR`, followed
by `REV` when reverb is enabled.

Combined NAM+IR processing only has enough CPU headroom with the Simple reverb.
When a combined model and reverb are active, AmpSim therefore uses Simple even
if the Settings menu is set to Plate. The saved reverb choice is not changed;
Plate is used again when a standalone NAM or IR model is selected.

## Obtaining NAM Models

1. **Train your own**: Use [NeuralAmpModeler](https://github.com/sdatkinson/NeuralAmpModeler) to capture your amp
2. **Download from community**: Browse [ToneHunt](https://tonehunt.org) for shared models

## Installing New Models

```bash
# 1. Copy .nam file to Models/ directory
mkdir "Models/My Amp"
cp ~/Downloads/my_amp.nam "Models/My Amp"

# 2. Rebuild and flash; the capture blob is generated automatically
make clean && make
make program-dfu
```

`tools/build_capture_blob.py` accepts exact A2 Lite NAM captures (WaveNet, 3 channels, 23 layers, 1871 weights) and 48 kHz mono/stereo WAV cabinet IRs. Unsupported files fail the build. Up to 128 total models are supported; a same-basename NAM+IR pair counts as one model.

## Testing Your Build

### Automated desktop + build tests

Before flashing, run the desktop tests. These verify NAM loading, reverb dry/wet math, EQ response, model conversion, and the Daisy build itself:

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
- [ ] FS1 toggles selected NAM/IR engine, LED 1 indicates state
- [ ] FS2 toggles reverb, LED 2 indicates state
- [ ] Both OFF = true bypass (no coloration)
- [ ] All 6 knobs respond smoothly
- [ ] Reverb mix knob crossfades between dry and wet (not a switch)
- [ ] Encoder browses NAM captures and IRs, click loads selection
- [ ] Model/effect persistence works after power cycle
- [ ] No audio dropouts during extended play
- [ ] EQ response is musical (bass/mid/treble)
- [ ] Reverb adds stereo width

## References

- [Daisy Documentation](https://docs.daisy.audio/)
- [bkshepherd 125B Hardware](https://github.com/bkshepherd/DaisySeedProjects)
- [NeuralAmpModeler](https://github.com/sdatkinson/NeuralAmpModeler)
- [NAM Community Models](https://tone3000.com)
- [MuleBox Project](https://github.com/optilude/MuleBox)
- [Dattorro Reverb Paper](https://www.dafx.de/paper-archive/1997/P056.PDF)
