# Control Scheme Analysis for AmpSim

## Hardware Overview

### Inputs
- **2 Footswitches**: Left (switch 0) and Right (switch 1)
- **6 Knobs**: Top row (0, 1, 2), Bottom row (3, 4, 5)
- **1 Rotary Encoder**: For model selection
- **True Bypass**: Relay on D1, Mute on D12

### Outputs
- **2 LEDs**: LED 0 (left, D22), LED 1 (right, D23)
- **1 LCD Display**: 128x64 OLED
- **True Bypass Relay**: Hardware bypass circuit

## Proposed Control Mapping

### Footswitches
| Switch | Function | LED | Behavior |
|--------|----------|-----|----------|
| **Left (0)** | Reverb on/off | LED 0 (left) | Toggle, LED = reverb status |
| **Right (1)** | NAM capture on/off | LED 1 (right) | Toggle, LED = NAM status |

**Critical Decision**: What happens with true bypass relay?
- Option A: Relay follows NAM switch (true bypass when NAM off)
- Option B: Relay always engaged (DSP always in signal path)
- Option C: Separate bypass footswitch (but we only have 2)

**Recommendation**: Use Option A - Relay follows NAM switch
- When NAM is OFF: True bypass engaged (hardware relay)
- When NAM is ON: DSP path active
- This provides true analog bypass when NAM is off
- Reverb can only be used when NAM is on (makes sense)

### Knobs
| Position | Knob # | Function | Range | Noon Position |
|----------|--------|----------|-------|---------------|
| **Top-Left** | 0 | Input Gain | -20dB to +20dB | Unity (0dB) |
| **Top-Middle** | 1 | Output Volume | -20dB to +20dB | Unity (0dB) |
| **Top-Right** | 2 | Reverb Mix | 0% to 100% | ~30% |
| **Bottom-Left** | 3 | Bass EQ | ±12dB | Flat (0dB) |
| **Bottom-Middle** | 4 | Mid EQ | ±12dB | Flat (0dB) |
| **Bottom-Right** | 5 | Treble EQ | ±12dB | Flat (0dB) |

### Rotary Encoder
- **Function**: NAM model selection
- **Behavior**: Cycle through models alphabetically
- **Display**: Show model name and variant on LCD
- **Encoder Click**: Reserved for future use (e.g., save settings)

## Signal Chain

```
Input → [Input Gain] → [NAM Capture] → [3-Band EQ] → [Reverb] → [Output Volume] → Output
                          ↓                              ↓
                      [On/Off FS]                    [On/Off FS]
                          ↓
                    [True Bypass Relay]
```

**Ordering Rationale**:
1. **Input Gain** first: Optimizes signal level for NAM model
2. **NAM**: Main amp modeling
3. **EQ after NAM**: Tones the amp sound
4. **Reverb after EQ**: Better to EQ dry signal before reverb
5. **Output Volume last**: Final level control

## State Persistence

### What to Persist
1. **NAM model index** (0 to N-1)
2. **NAM enabled** (on/off)
3. **Reverb enabled** (on/off)
4. **Reverb mix** (0-100%)
5. **Input gain** (knob position)
6. **Output volume** (knob position)
7. **EQ settings** (3 knobs)

### Storage Method
- **Daisy PersistentStorage**: Uses QSPI flash
- **Size**: ~64 bytes (small struct)
- **Location**: QSPI flash sector (wear leveling built-in)
- **Lifetime**: 10,000+ write cycles (plenty for user settings)

### Persistence Strategy
- **Save on**: Any knob change (debounced, after 2 seconds)
- **Save on**: Footswitch press
- **Save on**: Encoder change
- **Restore on**: Boot (before audio starts)

## Critical Design Questions

### 1. True Bypass Behavior
**User's Concern**: How does relay-based bypass work with state?

**Answer**: The relay is a hardware switch controlled by firmware:
- `hw.SetAudioBypass(true)` = Relay OFF (signal goes through DSP)
- `hw.SetAudioBypass(false)` = Relay ON (true hardware bypass)
- The relay state is NOT persisted automatically
- We need to restore relay state on boot based on saved NAM on/off state

**Recommended Implementation**:
```cpp
// On boot:
PersistentSettings settings = loadSettings();
if (settings.namEnabled) {
    hw.SetAudioBypass(false);  // Engage DSP path
    namEnabled = true;
} else {
    hw.SetAudioBypass(true);   // True bypass
    namEnabled = false;
}
```

### 2. Reverb Availability
**Question**: Can reverb be used when NAM is bypassed?

**Recommendation**: NO - Reverb should be disabled when NAM is off
- Rationale: True bypass means completely analog path
- If user wants reverb without NAM, they can use NAM with clean/clean-ish model
- Simpler UX: Clear relationship between NAM and reverb

### 3. EQ Placement
**Question**: EQ before or after NAM?

**Analysis**:
- **Before NAM**: Shapes input to model (like amp's preamp EQ)
- **After NAM**: Tones the amp sound (like amp's tone stack or post-processing)

**Recommendation**: **After NAM** (as proposed)
- More intuitive: "EQ the amp sound"
- Less likely to cause modeling artifacts
- Similar to real amp tone stack placement

### 4. Input/Output Gain
**Question**: What's unity gain?

**Implementation**:
```cpp
// Knob value: 0.0 to 1.0
// Noon position: 0.5
float gain = 1.0f;  // Unity

if (knobValue < 0.5f) {
    // Attenuation: 0dB to -20dB
    gain = powf(10.0f, (knobValue - 0.5f) * 40.0f / 20.0f);
} else {
    // Boost: 0dB to +20dB
    gain = powf(10.0f, (knobValue - 0.5f) * 40.0f / 20.0f);
}
```

### 5. LCD Display Layout
**128x64 pixels, current font sizes**:
- Font_7x10: ~18 chars per line, 6 lines
- Font_6x8: ~21 chars per line, 8 lines

**Proposed Layout**:
```
Line 0 (Font_7x10): Model Name
Line 1 (Font_6x8): Variant Name
Line 2 (Font_6x8): [NAM: ON] [REV: OFF]
Line 3 (Font_6x8): In: +0.0dB Out: +2.5dB
Line 4 (Font_6x8): Rev: 30% EQ: B+2 M-1 T+4
Line 5 (Font_6x8): [Encoder: Change Model]
```

## Potential Issues & Solutions

### Issue 1: Encoder Browsing Noise
**Problem**: Changing models makes audio artifacts

**Solution**:
- Load model in background thread
- Show "Loading..." on LCD
- Crossfade between models (optional, complex)
- Mute audio briefly during model change

### Issue 2: Knob Jitter
**Problem**: Knob values fluctuate slightly, causing unwanted saves

**Solution**:
- Implement hysteresis/dead zone
- Only save if knob changed > 0.05 (5%)
- Debounce save operation (wait 2 seconds after last change)

### Issue 3: Flash Wear
**Problem**: Constant saving wears out QSPI flash

**Solution**:
- Daisy's PersistentStorage uses wear leveling
- Only save when value actually changes
- 10,000 write cycles = 27 years at 1 save/day
- Implement "dirty flag" to only save when needed

### Issue 4: True Bypass Pop
**Problem**: Relay switching can cause audio pop

**Solution**:
- Use `SetAudioMute(true)` before relay switch
- Wait 10ms for mute to take effect
- Switch relay
- Wait 10ms for relay to settle
- Use `SetAudioMute(false)` to unmute

### Issue 5: Boot Time
**Problem**: Loading NAM model takes time on boot

**Solution**:
- Show "Loading..." on LCD immediately
- Load model in stages (parse JSON, then initialize)
- Keep user informed on LCD
- Expected boot time: ~500ms

### Issue 6: Memory with EQ
**Problem**: Adding 3-band EQ increases memory/CPU

**Analysis**:
- 3x Biquad filters: ~300 bytes state, ~300 cycles/sample
- Total CPU: 64-70% + 4% = 68-74%
- Should be OK, but test on hardware

**Implementation**: Use DaisySP's `Tone` or `Svf` for EQ
- Simpler: 3x `Tone` (high-shelf, peak, high-shelf)
- Better: 3x `Svf` (parametric EQ bands)

## Implementation Plan

### Phase 1: State Persistence (Critical)
1. Create `PersistentSettings` struct
2. Implement save/load with PersistentStorage
3. Test state restoration on boot
4. Verify QSPI flash writes

### Phase 2: UI Controls
1. Implement knob reading with dead zones
2. Add rotary encoder handling
3. Implement footswitch debouncing
4. Test all controls

### Phase 3: Audio Chain
1. Add input gain stage
2. Add 3-band EQ (DaisySP Svf)
3. Add output volume stage
4. Test signal chain ordering

### Phase 4: Display
1. Design LCD layout
2. Implement real-time parameter display
3. Add "Loading..." screen for model changes
4. Test display updates

### Phase 5: Integration
1. Wire up true bypass relay logic
2. Implement mute-before-switch
3. Add persistence triggers
4. Test full system

### Phase 6: Polish
1. Add parameter smoothing (prevent zipper noise)
2. Optimize LCD update rate (don't refresh every frame)
3. Add diagnostics display (CPU usage, heap)
4. Final testing

## Questions for User

1. **EQ Type**: Do you want simple tone controls (bass/mid/treble shelving) or parametric EQ with frequency control?

2. **Input Gain Range**: Is ±20dB sufficient, or do you need more range (e.g., ±40dB)?

3. **Encoder Click**: Should the encoder button have a function (e.g., toggle reverb, save settings)?

4. **Model Loading**: Is a brief mute during model change acceptable, or do you need seamless transition?

5. **Display Information**: What's most important to show? Current proposal:
   - Model name/variant
   - NAM/Reverb status
   - Input/Output levels
   - EQ settings
   - Any other preferences?

