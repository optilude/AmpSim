#!/bin/bash
set -euo pipefail
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

echo "Build Sections:"
if [ ! -f build/AmpSim.elf ]; then
    echo "  build/AmpSim.elf not found; run make first"
else
    arm-none-eabi-size build/AmpSim.elf
    SDRAM_BSS_HEX=$(arm-none-eabi-objdump -h build/AmpSim.elf | awk '/\.sdram_bss/ {print $3}')
    SDRAM_BSS_BYTES=$((16#${SDRAM_BSS_HEX:-0}))
    TOTAL_BSS=$(arm-none-eabi-size build/AmpSim.elf | awk 'NR==2 {print $3}')
    SRAM_BSS_BYTES=$((TOTAL_BSS - SDRAM_BSS_BYTES))
    echo ""
    echo "Interpreted memory:"
    echo "  SRAM BSS excluding SDRAM arena: $((SRAM_BSS_BYTES / 1024)) KB"
    echo "  SDRAM arena:                  $((SDRAM_BSS_BYTES / 1024)) KB"
fi

echo ""
echo "WARNINGS"
echo "-----------"
echo "1. CPU figures are estimates until measured on hardware."
echo "2. NAM model load still uses SRAM heap for JSON parse and tensors."
echo "3. Settings persistence is disabled until a BOOT_QSPI-safe backend exists."
echo ""

echo "Recommendations"
echo "------------------"
echo "1. TEST CPU FIRST: Flash and check for audio dropouts"
echo "2. MONITOR HEAP: Check for allocation failures"
echo "3. FALLBACK: Simplified reverb if CPU/memory issues arise"
echo "4. Persistence: use internal flash or another BOOT_QSPI-safe backend"
