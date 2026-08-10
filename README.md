# AmpSim - Guitar Amp Simulator based on Daisy Seed

A guitar amp simulator based on the Electro-Smith Daisy Seed 3 module, built on the bkshepherd 125B hardware platform.

## Project Goals

This project aims to build a guitar amp simulator that can process a mono guitar signal using:
- Neural Amp Modeller Architecture 2 (NAM A2) capture
- Optional Impulse Response (IR) cabinet simulation
- Plate reverb effect

## Hardware

The pedal is built on the bkshepherd 125B PCB which features:
- Daisy Seed 3 module (STM32H750)
- 2 momentary footswitches
- 2 LEDs
- 128x64 OLED display (SSD1306)
- Rotary encoder with push button
- 6 potentiometers
- Stereo audio input/output
- MIDI I/O
- True bypass relay

## Prerequisites

1. **ARM GCC Toolchain**
   ```bash
   # macOS
   brew install --cask gcc-arm-embedded
   
   # Linux (Debian/Ubuntu)
   sudo apt-get install gcc-arm-none-eabi
   ```

2. **dfu-util** (for flashing via USB)
   ```bash
   # macOS
   brew install dfu-util
   
   # Linux
   sudo apt-get install dfu-util
   ```

3. **Clone with submodules**
   ```bash
   git clone --recurse-submodules <repository-url>
   cd AmpSim
   
   # If already cloned without submodules:
   git submodule update --init --recursive
   ```

## Building

```bash
# Build the project
make

# Clean build files
make clean

# Clean everything including libraries
make clean-all
```

## Flashing

### First-time setup: Install the Daisy bootloader

**Option A - USB only:**
1. Connect Daisy Seed via USB
2. Hold BOOT button, press and release RESET, release BOOT
3. Run `make program-boot`

**Option B - Debug probe:**
1. Connect STLINK debug probe
2. Run `make program-boot-probe`

### Flashing firmware

**Option A - USB only:**
1. Press RESET on the Daisy Seed
2. Within 2.5 seconds, run `make program-dfu`

**Option B - Debug probe:**
1. Connect STLINK debug probe
2. Run `make program`

## Current Status

This is the initial scaffolding with minimal firmware that:
- Initializes the hardware (LCD, LEDs, knobs, encoder, footswitches)
- Displays text on the LCD
- Shows knob values (0-100) on the display
- Shows rotary encoder position
- Shows footswitch states
- Manipulates LEDs in response to inputs

## Architecture

```
AmpSim/
├── src/                    # Main application source
│   └── main.cpp           # Main firmware entry point
├── hardware/              # Hardware abstraction layer
│   ├── guitar_pedal_125b.h
│   └── guitar_pedal_125b.cpp
├── libDaisy/              # Daisy hardware library (submodule)
├── DaisySP/               # Daisy DSP library (submodule)
├── tools/                 # Build and utility tools
├── Makefile              # Build configuration
└── openocd_daisy_qspi.cfg # QSPI flash programming config
```

## References

- [Daisy Seed Documentation](https://docs.daisy.audio/)
- [bkshepherd 125B Hardware](https://github.com/bkshepherd/DaisySeedProjects)
- [MuleBox Project](https://github.com/optilude/MuleBox)
- [Flick Project](https://github.com/joulupukki/Flick)
