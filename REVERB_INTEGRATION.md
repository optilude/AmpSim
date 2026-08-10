# Dattorro Plate Reverb Integration

## Overview

Added the Dattorro 1997 plate reverb algorithm from the MuleBox project to AmpSim. The reverb processes mono input and produces stereo output, applied after NAM amp modeling.

## Files Added

### Core Reverb Engine
- `src/dattorro/Dattorro.cpp` - Main reverb processor
- `src/dattorro/Dattorro.hpp` - Header file

### DSP Components
- `src/dattorro/dsp/delays/InterpDelay.hpp` - Fractional delay with interpolation
- `src/dattorro/dsp/delays/AllpassFilter.hpp` - Allpass filter for diffusion
- `src/dattorro/dsp/filters/OnePoleFilters.hpp` - One-pole lowpass/highpass filters
- `src/dattorro/dsp/modulation/LFO.hpp` - LFO for reverb modulation
- `src/dattorro/utilities/Utilities.hpp` - Utility functions

### Wrapper
- `src/reverb_processor.h` - Simple wrapper for easy integration

## Features

### Audio Path
1. **Input** → Mono guitar signal
2. **NAM Processing** → Amp model applied (if enabled)
3. **Reverb Processing** → Dattorro plate reverb (if enabled)
4. **Output** → Stereo signal (left and right channels)

### Controls

#### Footswitches
- **FS1**: Toggle NAM amp modeling on/off
- **FS2**: Cycle through NAM models
- **FS3**: Toggle reverb on/off

#### Knobs
- **Knob 1**: Reverb mix (dry/wet balance, 0-100%)
- **Knob 2**: Reverb decay (0.5 to 1.0)
- **Knob 3**: Reverb tone (high-frequency damping)
- **Knob 4**: Output level (reserved for future use)

### Default Parameters (MuleBox Flick Settings)
- **Time Scale**: 1.007500
- **Input Diffusion**: Enabled
- **Pre-delay**: 0ms
- **Input Low Cut**: 2.87 pitch (≈220Hz)
- **Input High Cut**: 7.25 pitch (≈6kHz)
- **Decay**: 0.8
- **Tank Diffusion**: 0.85
- **Tank High Cut**: 7.25 pitch (≈6kHz)
- **Tank Low Cut**: 2.87 pitch (≈220Hz)
- **Mod Speed**: 0.8
- **Mod Depth**: 1.5
- **Mod Shape**: 0.25

## Implementation Details

### Changes from MuleBox Version
1. **InterpDelay**: Modified to use internal `std::vector` instead of external SDRAM buffers
2. **Removed SDRAM dependency**: All delay lines use heap-allocated memory
3. **Simplified initialization**: Wrapped in `ReverbProcessor` class for easy use

### Memory Usage
- **QSPI Flash**: 742KB (9.13% of 8MB)
- **SRAM**: 62KB (11.90% of 512KB)
- **Increase**: +15KB code, +2KB SRAM compared to NAM-only version

### CPU Performance
- **Expected**: ~10-15% additional CPU for reverb processing
- **Total estimated CPU**: ~70-75% at 48kHz (NAM + Reverb)

## Usage

### Basic Operation
1. Flash firmware: `make program`
2. NAM is on by default (LED 1 lit)
3. Reverb is on by default (LED 2 lit)
4. Use knobs to adjust reverb parameters in real-time

### Integration in Code
```cpp
#include "reverb_processor.h"

ReverbProcessor reverb;
reverb.init(48000.0f);
reverb.setMix(0.3f);  // 30% wet
reverb.setDecay(0.8f);

float outL, outR;
reverb.process(input, &outL, &outR);
```

## License

The Dattorro reverb implementation is from the PlateauNEVersio project and is licensed under GPLv3. See `src/dattorro/LICENSE-GPLv3.txt` for details.

## References

- Dattorro, J. (1997). "Effect Design, Part 1: Reverberator and Other Filters"
- MuleBox Project: https://github.com/yourusername/MuleBox (adapted from)
- Original algorithm based on Lexicon 224 plate reverb

## Testing

Build verification:
```bash
make clean
make
```

Expected output:
- Binary size: ~742KB
- No compilation errors
- All files compiled successfully

## Future Enhancements

Potential improvements:
1. Add reverb freeze feature
2. Add more reverb presets
3. Save/restore reverb settings
4. Add modulation speed/depth control via additional knobs
5. Optimize memory usage with static allocation
