#!/bin/bash
# Desktop + build integration tests.
#
# Runs three desktop test suites (NAM, reverb, EQ), builds the Daisy
# firmware, and checks binary sizes. Meant to be run before every commit.

set -euo pipefail

REPO=$(cd "$(dirname "$0")" && pwd)
cd "$REPO"

echo "================================"
echo "AmpSim Integration Test Suite"
echo "================================"
echo ""

DESKTOP_CXXFLAGS="-std=c++17 -O2 -Isrc"
NAM_CXXFLAGS="$DESKTOP_CXXFLAGS \
  -DNAM_ENABLE_A2_FAST=1 -DNAM_SHARED_PTR_ATOMIC_FREE_FUNCS=1 \
  -DNAM_SAMPLE_FLOAT=1 -DNAM_USE_INLINE_GEMM=1 \
  -INeuralAmpModelerCore \
  -INeuralAmpModelerCore/Dependencies/eigen \
  -INeuralAmpModelerCore/Dependencies/nlohmann"

echo "Test 0: Capture conversion"
rm -f test_nam test_reverb test_eq build/AmpSim.bin build/AmpSim.elf
python3 tools/build_capture_blob.py Captures/ --out-bin build/capture_data.bin --out-header src/capture_index.h --out-map build/capture_data.map
echo ""

# 1. NAM desktop test ---------------------------------------------------------
echo "Test 1: NAM desktop"
g++ $NAM_CXXFLAGS -o test_nam test_nam.cpp \
    src/nam_processor.cpp \
    NeuralAmpModelerCore/NAM/*.cpp \
    NeuralAmpModelerCore/NAM/wavenet/*.cpp
[ -f test_nam ] || { echo "ERROR: failed to build test_nam"; exit 1; }
./test_nam
echo ""

# 2. Reverb desktop test ------------------------------------------------------
echo "Test 2: Reverb desktop"
g++ $DESKTOP_CXXFLAGS -o test_reverb test_reverb.cpp src/dattorro/Dattorro.cpp
./test_reverb
echo ""

# 3. EQ desktop test ----------------------------------------------------------
echo "Test 3: EQ desktop"
g++ $DESKTOP_CXXFLAGS -o test_eq test_eq.cpp
./test_eq
echo ""

# 3b. IR desktop test ---------------------------------------------------------
echo "Test 3b: IR desktop"
g++ $DESKTOP_CXXFLAGS \
    -IlibDaisy/Drivers/CMSIS-DSP/Include \
    -o test_ir test_ir.cpp \
    libDaisy/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_rfft_fast_f32.c \
    libDaisy/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c \
    libDaisy/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_cfft_f32.c \
    libDaisy/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_cfft_init_f32.c \
    libDaisy/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_cfft_radix8_f32.c \
    libDaisy/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_bitreversal2.c \
    libDaisy/Drivers/CMSIS-DSP/Source/CommonTables/arm_common_tables.c \
    libDaisy/Drivers/CMSIS-DSP/Source/CommonTables/arm_const_structs.c \
    libDaisy/Drivers/CMSIS-DSP/Source/ComplexMathFunctions/arm_cmplx_mult_cmplx_f32.c \
    libDaisy/Drivers/CMSIS-DSP/Source/BasicMathFunctions/arm_add_f32.c
./test_ir
echo ""

# 4. Daisy build --------------------------------------------------------------
echo "Test 4: Daisy build"
make clean > /dev/null 2>&1 || true
make
[ -f build/AmpSim.bin ] || { echo "ERROR: Daisy build failed"; exit 1; }
echo "OK build/AmpSim.bin exists"
echo ""

# 5. Binary size --------------------------------------------------------------
# `size` lumps all .bss (SRAM + SDRAM arena) together, so we can't use it
# directly. Parse the linker's own memory report from the build log we
# already captured, or query the ELF sections directly.
echo "Test 5: Binary size"
QSPI_BYTES=$(arm-none-eabi-size build/AmpSim.elf | awk 'NR==2 {print $1 + $2}')
QSPI_KB=$((QSPI_BYTES / 1024))

# SRAM = .bss + .data (excluding .sdram_bss). Use objdump to be precise.
SDRAM_BSS_HEX=$(arm-none-eabi-objdump -h build/AmpSim.elf | awk '/\.sdram_bss/ {print $3}')
SDRAM_BSS_BYTES=$((16#${SDRAM_BSS_HEX:-0}))
TOTAL_BSS=$(arm-none-eabi-size build/AmpSim.elf | awk 'NR==2 {print $3}')
SRAM_BSS_BYTES=$((TOTAL_BSS - SDRAM_BSS_BYTES))
SRAM_KB=$((SRAM_BSS_BYTES / 1024))
SDRAM_KB=$((SDRAM_BSS_BYTES / 1024))

echo "  QSPI (text+data):  ${QSPI_KB} KB (limit 7936)"
echo "  SRAM (bss):         ${SRAM_KB} KB (limit ~450 free)"
echo "  SDRAM (arena etc):  ${SDRAM_KB} KB (limit 65536)"
[ "$QSPI_KB" -lt 7000 ] || { echo "ERROR: binary too large"; exit 1; }
[ "$SRAM_KB" -lt 300 ]  || { echo "ERROR: SRAM too full — heap won't fit reverb+NAM"; exit 1; }
echo ""

# 6. Model conversion tool ----------------------------------------------------
echo "Test 6: NAM model conversion tool"
python3 tools/build_capture_blob.py Captures/ --out-bin /tmp/test_capture_data.bin --out-header /tmp/test_capture_index.h --out-map /tmp/test_capture_data.map
[ -s /tmp/test_capture_data.bin ] || { echo "ERROR: capture conversion produced empty data"; exit 1; }
grep -q 'CAPTURE_COUNT' /tmp/test_capture_index.h || { echo "ERROR: header missing CAPTURE_COUNT"; exit 1; }
rm -f /tmp/test_capture_data.bin /tmp/test_capture_index.h /tmp/test_capture_data.map
echo "OK model conversion works on Captures/"
echo ""

echo "================================"
echo "All integration tests passed"
echo "================================"
echo ""
echo "Next steps:"
echo "  make program        # Flash via debug probe"
echo "  make program-dfu    # Flash via USB DFU"
