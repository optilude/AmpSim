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

### DaisySP-LGPL

**IMPORTANT: This component is LGPL v2.1 licensed**

- **Project**: https://github.com/electro-smith/DaisySP (the `DaisySP-LGPL` submodule)
- **License**: GNU Lesser General Public License v2.1
- **Copyright**: (c) 2023 Electrosmith, Corp, Sean Costello, Istvan Varga, Paul Batchelor
- **Purpose**: Provides `ReverbSc`, a Schroeder/Costello-style FDN reverb used
  by the Settings menu's "Simple" reverb engine (`src/simple_reverb_processor.h`)
  as a lighter-weight alternative to the Dattorro plate tank
- **Usage**: Statically linked as `libdaisysp-lgpl.a` (built from
  `DaisySP/DaisySP-LGPL/`, enabled via `USE_DAISYSP_LGPL=1` in the Makefile)
- **LGPL v2.1 compliance**: Because this is a statically-linked embedded
  firmware image (no dynamic relinking is possible on the target), LGPL v2.1
  compliance is satisfied by this project remaining fully open source: the
  complete source of both AmpSim and the exact `DaisySP-LGPL` version it links
  against is available, so anyone can rebuild the firmware against a modified
  copy of the library. See `DaisySP/DaisySP-LGPL/LICENSE` for the full license
  text.
- **Note**: This is a separate submodule from `DaisySP` above -- the rest of
  `DaisySP` remains MIT-licensed. Only the `Simple` reverb engine depends on
  `DaisySP-LGPL`.

### NeuralAmpModelerCore
- **Project**: https://github.com/sdatkinson/NeuralAmpModeler
- **License**: MIT
- **Copyright**: (c) 2023 Steven Atkinson
- **Purpose**: NAM (Neural Amp Modeler) neural network inference
- **Usage**: Reference implementation; AmpSim firmware uses a static A2 Lite runtime
  (`src/nam_a2_runtime.h`) derived from the NAM reference implementation
- **Note**: The submodule is retained for desktop validation and reference work,
  but firmware does not dynamically link against it

### Cycfi Q (Pitch Detection Library)

**IMPORTANT: This component is Boost Software License 1.0 licensed**

- **Project**: https://github.com/cycfi/q
- **License**: Boost Software License 1.0
- **Copyright**: (c) 2014-2026 Joel de Guzman
- **Purpose**: Zero-crossing + bitstream autocorrelation pitch detection
  (`pitch_detector`) and its pre-conditioning chain (`signal_conditioner`),
  used by the chromatic tuner
- **Usage**: A 36-header subset is vendored under `third_party/q/include/`,
  traced by include reachability from `q/pitch/pitch_detector.hpp` and
  `q/fx/signal_conditioner.hpp` (the only two entry points AmpSim actually
  uses). Wrapped by `src/tuner_processor.cpp`, the only translation unit that
  includes any Q header (see that file for why it is isolated and built at
  C++20 while the rest of AmpSim stays on C++17). `q/support/pitch_names.hpp`
  was deliberately **not** vendored; the two frequency constants AmpSim needs
  from it are computed directly in `tuner_processor.cpp` instead.
- **Sub-component**: `infra/assert.hpp` (also vendored, under
  `third_party/q/include/infra/`) is MIT-licensed, (c) 2016-2023 Joel de Guzman

### 1€ Filter
- **Project**: https://cristal.univ-lille.fr/~casiez/1euro/ (Géry Casiez,
  Nicolas Roussel, Daniel Vogel)
- **License**: MIT
- **Copyright**: (c) 2014-2020 Jonathan Aceituno
- **Purpose**: Smooths the chromatic tuner's raw `pitch_detector` frequency
  output before it is displayed
- **Usage**: Vendored verbatim as `third_party/oneeurofilter/1efilter.hpp`,
  itself copied from `Util/1efilter.hpp` in
  https://github.com/bkshepherd/DaisySeedProjects (`Software/GuitarPedal/`)

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

### "Simple" Reverb Engine (ReverbSc wrapper)

- **Source**: `Effect-Modules/reverb_module.h` and `.cpp` in
  https://github.com/bkshepherd/DaisySeedProjects (`Software/GuitarPedal/`)
- **License**: MIT (c) 2023 Keith Shepherd
- **Ported to**: `src/simple_reverb_processor.h`
- **Usage**: AmpSim's port keeps bkshepherd's Time/Damp-to-ReverbSc parameter
  mapping and default values (Time = 0.45, Damp = 0.3), adapted to AmpSim's
  `process(reverbIn, dryIn, outL, outR)` / `setMix` / `clear` interface so it
  can be selected from the Settings menu alongside the Dattorro engine. Time
  and Damp are not exposed as separate controls; only Mix (shared with the
  Dattorro engine's knob) is user-adjustable.

### Chromatic Tuner (Cycfi Q wrapper + UI)
- **Source**: `Util/frequency_detector_q.h`/`.cpp` and
  `Effect-Modules/tuner_module.h`/`.cpp` in
  https://github.com/bkshepherd/DaisySeedProjects (`Software/GuitarPedal/`)
- **License**: MIT (c) 2023 Keith Shepherd
- **Ported to**: `src/tuner_processor.h`/`.cpp` (the Cycfi Q + 1€ filter
  wrapper) and `DrawTunerScreen()` in `src/main.cpp` (the note-name/octave/
  block-strip/frequency display)
- **Usage**: Entered by holding FS2 for 2 seconds; keeps upstream's detector
  configuration, smoothing constants, and 21-block in-tune strip layout,
  adapted to AmpSim's manual `SetCursor`/`WriteString` display conventions and
  wired into AmpSim's own footswitch/bypass/mute state machine rather than
  upstream's effect-module framework

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
- The ported "Simple" reverb engine (`Effect-Modules/reverb_module.h`/`.cpp`
  from bkshepherd/DaisySeedProjects)
- The ported chromatic tuner UI and note math
  (`Util/frequency_detector_q.h`/`.cpp`, `Effect-Modules/tuner_module.h`/`.cpp`
  from bkshepherd/DaisySeedProjects)
- The 1€ filter (`third_party/oneeurofilter/1efilter.hpp`)
- Cycfi Q's `infra/assert.hpp` (vendored alongside the Boost-licensed Q core,
  see below)

### LGPL v2.1-Licensed Components
- **DaisySP-LGPL** (`ReverbSc`, used by the "Simple" reverb engine) -- see the
  DaisySP-LGPL section above for compliance details

### Boost Software License 1.0-Licensed Components
- **Cycfi Q** (`third_party/q/include/`, the pitch-detection engine behind the
  chromatic tuner) -- see the Cycfi Q section above for what is vendored and
  why. Boost SL 1.0 is a short, permissive license (similar in effect to MIT)
  that requires only that the license text accompany source and binary
  redistributions; it imposes no copyleft or attribution-in-UI obligation. The
  full text is reproduced at https://www.boost.org/LICENSE_1_0.txt and should
  be included alongside this file in any redistribution.

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
├── DaisySP/DaisySP-LGPL/LICENSE
├── NeuralAmpModelerCore/LICENSE
├── libDaisy/Drivers/STM32H7xx_HAL_Driver/LICENSE.md
├── libDaisy/Drivers/CMSIS*/LICENSE.*
├── third_party/q/LICENSE
├── third_party/oneeurofilter/LICENSE
└── ... (and others)
```

## Building With This Project

When building AmpSim, you are combining:
1. **Your own code** (MIT license)
2. **MIT-licensed dependencies** (libDaisy, DaisySP, NeuralAmpModelerCore)
3. **An LGPL v2.1-licensed dependency** (DaisySP-LGPL's `ReverbSc`, statically linked)
4. **A Boost Software License 1.0-licensed dependency** (Cycfi Q, statically linked)
5. **Proprietary hardware support code** (STM32 HAL, ARM CMSIS)
6. **Published algorithms** (Dattorro reverb)

This combination is permissible because:
- MIT is a permissive license compatible with proprietary code
- The project remains fully open source, satisfying LGPL v2.1's static-linking
  requirement that users be able to rebuild against a modified copy of the library
- Boost SL 1.0 is permissive and imposes no copyleft obligation, only that its
  license text travel with the source/binary
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

**Last Updated**: September 2, 2026
