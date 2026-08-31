# Third-Party Licenses and Attributions

This document provides detailed information about third-party software, algorithms, and hardware used in the AmpSim project.

## Software Dependencies

### libDaisy
- **Project**: https://github.com/electro-smith/libDaisy
- **License**: MIT
- **Copyright**: (c) 2019 Electrosmith
- **Purpose**: Hardware abstraction layer and core DSP library for the Daisy Seed
- **Usage**: Core firmware foundation; manages audio codec, GPIO, ADC, and DSP primitives

### DaisySP
- **Project**: https://github.com/electro-smith/DaisySP
- **License**: MIT (with nested attributions)
- **Copyright**: (c) 2020 Electrosmith, Corp.
- **Purpose**: DSP utilities including filters, oscillators, and effects
- **Sub-components**:
  - **Plaits** (Emilie Gillet, MIT): Synthesis primitives
  - **Soundpipe** (Paul Batchelor, MIT): Audio DSP library
- **Usage**: EQ filters, filter utilities, general DSP helpers

### NeuralAmpModelerCore
- **Project**: https://github.com/sdatkinson/NeuralAmpModeler
- **License**: MIT
- **Copyright**: (c) 2023 Steven Atkinson
- **Purpose**: NAM (Neural Amp Modeler) neural network inference
- **Usage**: Reference implementation; AmpSim firmware uses a static A2 Lite runtime
  (`src/nam_a2_runtime.h`) derived from the NAM reference implementation
- **Note**: The submodule is retained for desktop validation and reference work,
  but firmware does not dynamically link against it

## Algorithms

### Dattorro 1997 Plate Reverb Implementation

**IMPORTANT: This component is GPL v3 licensed**

The Dattorro reverb implementation in `src/dattorro/` is **derived from the Flick pedal project**
and is therefore subject to GPL v3 licensing terms.

#### Lineage

1. **Academic Foundation**: Jon Dattorro's 1997 plate reverb algorithm
   - Paper: "Effect Design Using Delay Networks"
   - Publication: Proc. 1997 AES Conference, Copenhagen, Denmark, Paper 4780
   - Reference: https://www.dafx.de/paper-archive/1997/P056.PDF
   - Status: Published academic algorithm; algorithm itself is not copyrighted

2. **PlateauNEVersio** → **Flick** → **AmpSim**
   - The implementation traces back through PlateauNEVersio to the Flick pedal
   - Flick: https://github.com/joulupukki/Flick (GPL v3)
   - Author: joulupukki
   - License: GPL v3
   - AmpSim adapted the reverb implementation from Flick for integration with NAM
     and other effects on the Daisy Seed platform

#### GPL v3 Compliance

Because this code is derived from Flick (GPL v3), the following requirements apply:

- Any distribution of this software must include a copy of the GPL v3 license
- Source code must be made available to users
- Any modifications to the reverb implementation must be documented
- Derivative works must use a compatible license (GPL v3 or later)

The reverb components affected:
- `src/dattorro/Dattorro.hpp` and `src/dattorro/Dattorro.cpp`
- `src/dattorro/dsp/delays/AllpassFilter.hpp`
- `src/dattorro/dsp/delays/InterpDelay.hpp`
- `src/dattorro/dsp/filters/OnePoleFilters.hpp`
- `src/dattorro/dsp/modulation/LFO.hpp`
- `src/reverb_arena.h` (SDRAM allocation for reverb)
- `src/reverb_processor.h` (wrapper and integration)

## Hardware

### Daisy Seed 3
- **Manufacturer**: Electrosmith
- **Processor**: STM32H750 ARM Cortex-M7 @ 400 MHz
- **Memory**: 512 KB internal RAM + 1 MB external SDRAM, QSPI flash
- **License**: See STM32 HAL and CMSIS sections below
- **Documentation**: https://docs.daisy.audio/

### 125B Guitar Pedal PCB
- **Design**: bkshepherd DaisySeedProjects
- **Repository**: https://github.com/bkshepherd/DaisySeedProjects
- **Documentation**: https://github.com/bkshepherd/DaisySeedProjects/blob/main/Hardware/GuitarPedal125b/docs/README.md
- **Hardware**: Audio codec (PCM3060), power supply, relay for true bypass,
  potentiometers, footswitches, LEDs, OLED display, rotary encoder

### STM32H7 Microcontroller Hardware
- **Manufacturer**: STMicroelectronics
- **Series**: STM32H7xx
- **HAL License**: See `libDaisy/Drivers/STM32H7xx_HAL_Driver/LICENSE.md`
- **CMSIS License**: See `libDaisy/Drivers/CMSIS*/` for ARM CMSIS licensing

## Third-Party Licensing Compliance

### MIT-Licensed Components
The following are licensed under the MIT License and fully compatible with
AmpSim's MIT license:
- libDaisy
- DaisySP (and its sub-components: Plaits, Soundpipe)
- NeuralAmpModelerCore

### ARM CMSIS
Located in: `libDaisy/Drivers/CMSIS*/`
- License: ARM CMSIS License
- See bundled license files for terms

### STM32 HAL
Located in: `libDaisy/Drivers/STM32H7xx_HAL_Driver/`
- License: See `LICENSE.md` in that directory
- Proprietary but available for use with STM32 devices

## How to Verify Licenses

Each third-party dependency includes a LICENSE file in its respective directory:
```
├── libDaisy/LICENSE
├── DaisySP/LICENSE
├── NeuralAmpModelerCore/LICENSE
├── libDaisy/Drivers/STM32H7xx_HAL_Driver/LICENSE.md
├── libDaisy/Drivers/CMSIS*/LICENSE.*
└── ... (and others)
```

## Building With This Project

When building AmpSim, you are combining:
1. **Your own code** (MIT license)
2. **MIT-licensed dependencies** (libDaisy, DaisySP, NeuralAmpModelerCore)
3. **Proprietary hardware support code** (STM32 HAL, ARM CMSIS)
4. **Published algorithms** (Dattorro reverb)

This combination is permissible because:
- MIT is a permissive license compatible with proprietary code
- The Dattorro algorithm is not subject to copyright
- You retain rights to derivative works and binary distributions

## Distribution Considerations

If you distribute binaries or source code derived from AmpSim:
1. Include a copy of the LICENSE file
2. Include or reference this THIRD_PARTY.md file
3. Preserve copyright notices in source files
4. You are free to add your own copyright and license terms
   (since MIT is permissive)

For detailed license text of each component, consult the LICENSE files
in the respective directories.

---

**Last Updated**: August 31, 2025
