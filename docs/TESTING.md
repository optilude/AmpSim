# Testing Guide

## Hardware Testing Checklist

Before considering the firmware production-ready, verify all items on this checklist:

### Basic Functionality

- [ ] **Audio passes through**
  - Connect guitar to input, amp to output
  - Sound is clean with no distortion (when effects OFF)
  
- [ ] **True bypass works**
  - Both footswitches OFF → true bypass relay engaged
  - Sound is pure analog (no coloration)
  - LED 0 and LED 1 are OFF

- [ ] **NAM toggle works**
  - Press FS2 (right footswitch)
  - LED 1 lights up
  - Sound changes (amp modeling active)
  - Display shows "NAM: ON"

- [ ] **Reverb toggle works**
  - Press FS1 (left footswitch)
  - LED 0 lights up
  - Reverb is audible
  - Display shows "REV: ON"

### Control Testing

- [ ] **Knob 0 (Input Gain)**
  - Rotate CCW → volume decreases
  - Rotate CW → volume increases
  - Noon position ≈ unity gain
  - Display shows dB value

- [ ] **Knob 1 (Output Volume)**
  - Same behavior as Knob 0
  - Affects final output level

- [ ] **Knob 2 (Reverb Mix)**
  - Rotate CCW → less reverb
  - Rotate CW → more reverb
  - Display shows percentage
  - 0% = dry only, 100% = wet only

- [ ] **Knob 3 (Bass EQ)**
  - Rotate CCW → bass cut
  - Rotate CW → bass boost
  - Noon = flat EQ
  - Display shows ±dB

- [ ] **Knob 4 (Mid EQ)**
  - Same behavior as Knob 3
  - Affects midrange (1kHz)

- [ ] **Knob 5 (Treble EQ)**
  - Same behavior as Knob 3
  - Affects treble (4kHz)

### Encoder Testing

- [ ] **Encoder rotation**
  - Rotate clockwise → next model
  - Rotate counter-clockwise → previous model
  - Display shows "->" prefix (preview mode)
  - Display shows model name/variant

- [ ] **Encoder click**
  - Click encoder button
  - Display shows "Loading..."
  - Model loads successfully
  - Display shows new model name

- [ ] **Preview timeout**
  - Rotate encoder (don't click)
  - Wait 10 seconds
  - Display reverts to current model
  - "->" prefix disappears

### Bypass Behavior Testing

- [ ] **NAM ON, Reverb OFF**
  - Press FS2 (NAM on), FS1 (reverb off)
  - Sound: NAM + EQ only
  - No reverb audible
  - Display: "[NAM:ON] [REV:OFF]"

- [ ] **NAM OFF, Reverb ON**
  - Press FS1 (reverb on), FS2 (NAM off)
  - Sound: Reverb only (no amp modeling)
  - Display: "[NAM:OFF] [REV:ON]"

- [ ] **Both ON**
  - Press both footswitches
  - Sound: NAM + EQ + Reverb (full chain)
  - Display: "[NAM:ON] [REV:ON]"

- [ ] **Both OFF**
  - Press both footswitches to turn off
  - Sound: True bypass (analog)
  - No coloration
  - Relay clicks
  - Display: "[NAM:OFF] [REV:OFF]"

### Persistence Testing

- [ ] **Settings persistence across power cycles**
  1. Change all knobs to random positions
  2. Toggle NAM and Reverb states
  3. Select different model
  4. Wait 2+ seconds (for save)
  5. Power cycle (unplug, wait 5s, plug back in)
  6. Verify all settings restored

- [ ] **No save spam**
  - Rapidly rotate a knob
  - Only one save occurs (after 2s idle)
  - Check serial debug for save confirmations

### Audio Quality Testing

- [ ] **No audio dropouts**
  - Play for 5+ minutes
  - No clicks, pops, or dropouts
  - Smooth sustained notes

- [ ] **No zipper noise**
  - Slowly rotate gain/volume knobs
  - No audible stepping or zipper noise
  - Smooth parameter changes

- [ ] **No relay pops**
  - Toggle NAM on/off multiple times
  - No loud pops when switching
  - 30ms mute timing is sufficient

- [ ] **EQ response is musical**
  - Bass control affects low frequencies
  - Mid control affects midrange
  - Treble control affects high frequencies
  - Boost/cut is smooth

- [ ] **Reverb quality**
  - Smooth decay
  - No artifacts or static
  - Stereo widening effect present
  - Mix control works smoothly

### Display Testing

- [ ] **Display updates correctly**
  - All text is readable
  - No corruption or artifacts
  - Updates at ~30 FPS (smooth)

- [ ] **Display shows correct values**
  - dB values match perceived gain
  - Percentages match knob positions
  - Model names are correct

### Error Handling Testing

- [ ] **Invalid model index**
  - If corrupted settings, defaults to model 0
  - No crash

- [ ] **Model load failure**
  - If model JSON is corrupt
  - Shows "Model Load Failed!" for 5 seconds
  - Continues with previous model

- [ ] **No models available**
  - If `NAM_MODEL_COUNT == 0`
  - Shows "No Models!" message
  - Audio still passes through

## Validation Levels

### Level 1: Desktop Unit Tests (Automated)

**Purpose**: Verify NAM logic on familiar platform

**Run:**
```bash
./test_integration.sh
```

**What it validates:**
- ✅ Model loading from JSON
- ✅ A2 model initialization
- ✅ Audio processing (input → output transformation)
- ✅ State persistence across multiple blocks
- ✅ No crashes or assertions

### Level 2: Daisy Build Verification (Automated)

**Purpose**: Ensure code compiles for target hardware

**Run:**
```bash
make clean
make
```

**What it validates:**
- ✅ ARM Cortex-M7 compilation
- ✅ No undefined references
- ✅ Binary fits in memory constraints
- ✅ Correct memory regions

**Expected output:**
```
Memory region         Used Size  Region Size  %age Used
SRAM:                62KB        512KB       12%
QSPIFLASH:           731KB       7936KB      9.21%
```

### Level 3: Hardware Integration Testing (Manual)

**Purpose**: Verify on actual Daisy hardware

**Procedure:**
1. Flash firmware: `make program`
2. Connect guitar to input
3. Connect amp/output to output
4. Complete hardware testing checklist above

### Level 4: Real-World Testing (Manual)

**Purpose**: Verify tone quality and performance

**Test scenarios:**
- [ ] Clean tone model sounds correct
- [ ] High-gain model sounds correct
- [ ] No latency noticeable while playing
- [ ] Stable over extended use (1+ hour)
- [ ] Different playing styles (chords, leads, palm mutes)
- [ ] Various pickup types (single coil, humbucker)
- [ ] Different guitars (Les Paul, Strat, etc.)

## Performance Testing

### CPU Usage Measurement

**Setup:**
```cpp
// Add to main.cpp globals:
uint32_t cpu_cycles = 0;
uint32_t max_cycles = 0;
uint32_t avg_cycles = 0;

// In AudioCallback:
uint32_t start = DWT->CYCCNT;
// ... audio processing ...
uint32_t end = DWT->CYCCNT;
cpu_cycles = end - start;
if (cpu_cycles > max_cycles) max_cycles = cpu_cycles;
avg_cycles = (avg_cycles * 99 + cpu_cycles) / 100;

// Display on OLED or serial debug
```

**Expected results:**
- **Average**: ~5,700 cycles (68% CPU)
- **Max**: <7,500 cycles (90% CPU)
- **Available**: 8,333 cycles per block (48kHz @ 400MHz)

### Stress Test

**Procedure:**
1. Enable NAM + Reverb
2. Play continuously for 30 minutes
3. Monitor for:
   - Audio dropouts
   - Display freezing
   - Control responsiveness
   - CPU usage spikes

**Pass criteria:**
- No dropouts
- Controls remain responsive
- CPU usage stable (<75% average)

## Known Issues & Mitigations

### Issue 1: `thread_local` Storage

**Problem**: Bare-metal ARM doesn't support `thread_local`

**Solution**: Patch applied automatically by `apply_patches.sh`

**Verification:**
```bash
cd NeuralAmpModelerCore
git status
# Should show: modified: NAM/dsp.cpp
```

### Issue 2: Binary Size

**Problem**: Large binary (731KB) exceeds SRAM

**Solution**: `APP_TYPE = BOOT_QSPI` runs from flash

**Verification:**
```bash
arm-none-eabi-size build/AmpSim.elf
# Check that binary < 8MB (QSPI size)
```

### Issue 3: Exception Handling

**Problem**: NAM library uses exceptions

**Solution**: Enabled in build (`-fexceptions`)

**Trade-off**: +50KB binary, but required for compatibility

## Continuous Validation

### Before Every Commit

```bash
# 1. Clean build
make clean && make

# 2. Check binary size
arm-none-eabi-size build/AmpSim.elf

# 3. Test on hardware
make program
# Complete hardware testing checklist

# 4. Commit
git add -A
git commit -m "Description of changes"
```

### When Updating Submodules

```bash
# 1. Update submodules
git submodule update --remote

# 2. Re-apply patches
./apply_patches.sh

# 3. Rebuild
make clean-all
make

# 4. Test
make program
# Complete testing checklist
```

## Testing Report Template

When reporting test results, use this format:

```
## Test Results - [Date]

### Hardware
- Daisy Seed: [Rev 4/5/etc]
- Debug probe: [Yes/No]
- Guitar: [Model]
- Amp/Monitor: [Model]

### Functional Tests
- Audio passthrough: [PASS/FAIL]
- True bypass: [PASS/FAIL]
- NAM toggle: [PASS/FAIL]
- Reverb toggle: [PASS/FAIL]
- All knobs: [PASS/FAIL]
- Encoder: [PASS/FAIL]
- Persistence: [PASS/FAIL]

### Performance Tests
- CPU usage: [X% avg, Y% max]
- No dropouts: [PASS/FAIL]
- Stability (30min): [PASS/FAIL]

### Audio Quality
- EQ response: [PASS/FAIL]
- Reverb quality: [PASS/FAIL]
- No zipper noise: [PASS/FAIL]
- No relay pops: [PASS/FAIL]

### Issues Found
1. [Description]
   - Steps to reproduce
   - Expected behavior
   - Actual behavior

### Notes
[Any additional observations]
```

## Automated Testing (Future Enhancement)

Currently, all tests are manual. Future improvements could include:

1. **Unit tests**: Google Test framework for desktop testing
2. **Integration tests**: Automated hardware testing with audio interface
3. **Performance regression**: CI pipeline to catch CPU/memory regressions
4. **Audio analysis**: Automated FFT analysis for frequency response
5. **Load testing**: Automated stress tests with various models

## References

- [Daisy Testing Guide](https://github.com/electro-smith/libDaisy/tree/master/tests)
- [pico-neural-amp-modeler-demo Results](https://github.com/oyama/pico-neural-amp-modeler-demo/blob/main/RESULTS.md)
- [NAM Validation](https://github.com/sdatkinson/NeuralAmpModelerCore)
