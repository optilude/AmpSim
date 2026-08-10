# Resource Analysis: NAM + Dattorro Reverb

## 🚨 Critical Problem

**The pre-delay buffer is 1.5MB (192010 samples = 4 seconds!)**

This is far too large for embedded systems. The MuleBox uses this for long delay effects, but for guitar amp reverb, we need much less.

## CPU Usage

### Estimated Load at 48kHz

| Component | Cycles/Sample | CPU @ 400MHz |
|-----------|---------------|--------------|
| NAM A2-Lite | ~3,400 | 52% |
| Dattorro Reverb | ~800-1,200 | 12-18% |
| **Total** | **~4,200-4,600** | **~64-70%** |

**Status**: ✅ Should be OK, but needs testing

Available cycles: 8,333/sample at 48kHz
Safety margin: ~30% headroom

## Memory Usage

### Current (PROBLEMATIC)

| Component | Memory | Location |
|-----------|--------|----------|
| Pre-delay buffer | **1.5 MB** | Heap ⚠️ |
| Other delay lines | ~300 KB | Heap |
| NAM model state | ~200 KB | Heap |
| **Total** | **~2 MB** | **Too much!** |

**Status**: ❌ Will cause heap allocation failures

### Problem Details

The `preDelay` in Dattorro.cpp is initialized with:
```cpp
preDelay = InterpDelay(192010, 0.0f);  // 4 seconds of delay!
```

This creates a 1.5MB buffer that's completely unnecessary for guitar reverb.

## 🔧 Fixes Required

### Fix 1: Reduce Pre-delay Maximum

Change from 4 seconds to 200ms:
```cpp
// In Dattorro.cpp constructor:
preDelay = InterpDelay(9600, 0.0f);  // 200ms @ 48kHz
```

**Savings**: 1.4 MB → 75 KB

### Fix 2: Optimize Delay Line Sizes

Scale delay lines to actual needs:
- Max pre-delay: 200ms (9600 samples)
- Tank delays: Keep as-is (they're needed for reverb quality)

**Total after fix**: ~350 KB (manageable)

### Fix 3: Static Allocation (Optional)

Convert `std::vector` to fixed-size arrays:
```cpp
float delayBuffer[MAX_DELAY_SIZE];
```

This eliminates heap fragmentation risk.

## 💡 Recommendations

### Immediate Actions

1. **Fix pre-delay size** (required)
2. **Test CPU usage** on hardware
3. **Monitor heap allocation** during init

### Testing Strategy

```cpp
// Add CPU measurement to AudioCallback:
#include "daisy_core.h"

uint32_t start = DWT->CYCCNT;
// ... audio processing ...
uint32_t cycles = DWT->CYCCNT - start;

// Display on OLED:
// "CPU: 4500/8333 cyc (54%)"
```

### Fallback Plan

If CPU/memory issues persist:

1. **Reduce reverb quality**:
   - Use smaller delay lines
   - Reduce LFO complexity
   
2. **Use simpler reverb**:
   - DaisySP's `ReverbSc` uses much less CPU/memory
   
3. **Disable reverb option**:
   - Make reverb optional at compile time
   - `#define ENABLE_REVERB 0`

## Expected Results After Fix

| Resource | Before | After | Status |
|----------|--------|-------|--------|
| CPU | 64-70% | 64-70% | ✅ OK |
| Heap Memory | 2 MB | 350 KB | ✅ OK |
| SRAM | 62 KB | 62 KB | ✅ OK |
| QSPI Flash | 742 KB | 740 KB | ✅ OK |

## Next Steps

1. Apply pre-delay fix
2. Rebuild and test
3. Measure actual CPU usage
4. Test for audio dropouts
5. Verify heap allocation succeeds
