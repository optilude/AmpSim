#!/bin/bash
echo "=== Resource Analysis: NAM + Dattorro Reverb on Daisy Seed ==="
echo ""
echo "CPU Analysis"
echo "------------"
echo "Daisy Seed: Cortex-M7 @ 400MHz, double-precision FPU"
echo ""
echo "NAM A2-Lite Processing:"
echo "  - RP2350 (Cortex-M33 @ 300MHz): 4,533 cycles/sample (73% CPU @ 48kHz)"
echo "  - Daisy (Cortex-M7 @ 400MHz): ~3,400 cycles/sample (estimated)"
echo "  - Expected CPU: ~52% at 48kHz"
echo ""
echo "Dattorro Reverb Processing:"
echo "  - Per sample: 4 LFOs, 8 allpass filters, 4 delays, 6 filters"
echo "  - Estimated: 800-1,200 cycles/sample"
echo "  - Expected CPU: ~12-18% at 48kHz"
echo ""
echo "Total Estimated CPU: 64-70%"
echo ""

echo "Memory Analysis"
echo "--------------"
echo "Daisy Seed: 512KB SRAM, 8MB QSPI flash"
echo ""

# Calculate reverb delay line sizes
cat << 'PYTHON' | python3
# Dattorro delay line sizes at 48kHz
sample_rate = 48000
dattorro_rate = 29761
scale = sample_rate / dattorro_rate

delays = {
    'Input pre-delay': 192010,  # Max pre-delay
    'Input APF1': 141 * 8,
    'Input APF2': 107 * 8,
    'Input APF3': 379 * 8,
    'Input APF4': 277 * 8,
    'Left APF1': 672,
    'Left Delay1': 4453,
    'Left APF2': 1800,
    'Left Delay2': 3720,
    'Right APF1': 908,
    'Right Delay1': 4217,
    'Right APF2': 2656,
    'Right Delay2': 3163,
}

total_samples = 0
print("Delay Line Memory Requirements:")
print("-------------------------------")
for name, size in delays.items():
    scaled = int(size * scale * 1.1)  # 1.1 for safety margin
    total_samples += scaled
    kb = (scaled * 4) / 1024  # 4 bytes per float
    if kb > 10:
        print(f"  {name:20s}: {scaled:6d} samples ({kb:6.1f} KB)")

total_kb = (total_samples * 4) / 1024
print(f"\nTotal reverb delay memory: {total_kb:.1f} KB")
print(f"Plus filter states, LFOs: ~{total_kb * 1.2:.1f} KB total")
PYTHON

echo ""
echo "Current Build:"
arm-none-eabi-size build/AmpSim.elf | grep -E "text|data|bss" | awk '{
    printf "  Code (QSPI):     %.1f KB\n", $1/1024
    printf "  Initialized data: %.1f KB\n", $2/1024
    printf "  BSS (SRAM):       %.1f KB\n", $3/1024
}'
echo ""

echo "⚠️  WARNINGS"
echo "-----------"
echo "1. Pre-delay buffer is huge (192010 samples = 750KB!)"
echo "   This is allocated in heap, not SRAM, but still significant"
echo "2. Total reverb memory: ~250-300KB"
echo "3. With NAM model state, total heap usage: ~400KB+"
echo "4. Cortex-M7 has limited heap fragmentation tolerance"
echo ""

echo "💡 Recommendations"
echo "------------------"
echo "1. TEST CPU FIRST: Flash and check for audio dropouts"
echo "2. MONITOR HEAP: Check for allocation failures"
echo "3. CONSIDER: Reduce pre-delay max size (currently 4 seconds!)"
echo "4. ALTERNATIVE: Use static allocation for delay lines"
echo "5. FALLBACK: Simplified reverb if CPU/memory issues arise"
