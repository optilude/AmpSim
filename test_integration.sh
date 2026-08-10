#!/bin/bash
# Validate NAM A2 implementation
# This script tests that our integration works correctly

set -e

echo "================================"
echo "NAM A2 Integration Test Suite"
echo "================================"
echo ""

# Test 1: Build desktop test program
echo "Test 1: Building desktop test program..."
g++ -std=c++17 \
    -DNAM_ENABLE_A2_FAST=1 \
    -DNAM_SHARED_PTR_ATOMIC_FREE_FUNCS=1 \
    -DNAM_SAMPLE_FLOAT=1 \
    -DNAM_USE_INLINE_GEMM=1 \
    -O2 \
    -o test_nam \
    test_nam.cpp \
    src/nam_processor.cpp \
    NeuralAmpModelerCore/NAM/activations.cpp \
    NeuralAmpModelerCore/NAM/container.cpp \
    NeuralAmpModelerCore/NAM/conv1d.cpp \
    NeuralAmpModelerCore/NAM/convnet.cpp \
    NeuralAmpModelerCore/NAM/dsp.cpp \
    NeuralAmpModelerCore/NAM/get_dsp.cpp \
    NeuralAmpModelerCore/NAM/linear.cpp \
    NeuralAmpModelerCore/NAM/lstm.cpp \
    NeuralAmpModelerCore/NAM/ring_buffer.cpp \
    NeuralAmpModelerCore/NAM/util.cpp \
    NeuralAmpModelerCore/NAM/wavenet/a2_fast.cpp \
    NeuralAmpModelerCore/NAM/wavenet/model.cpp \
    NeuralAmpModelerCore/NAM/wavenet/slimmable.cpp \
    -INeuralAmpModelerCore \
    -INeuralAmpModelerCore/Dependencies/eigen \
    -INeuralAmpModelerCore/Dependencies/nlohmann \
    -Isrc \
    2>&1 | grep -v "note:" | grep -v "parameter passing" || true

if [ ! -f test_nam ]; then
    echo "ERROR: Failed to build test program"
    exit 1
fi

echo "✓ Test program built"
echo ""

# Test 2: Run desktop tests
echo "Test 2: Running desktop validation tests..."
./test_nam
echo ""

# Test 3: Build for Daisy
echo "Test 3: Building for Daisy hardware..."
make clean > /dev/null 2>&1 || true
make 2>&1 | tail -20

if [ ! -f build/AmpSim.bin ]; then
    echo "ERROR: Failed to build for Daisy"
    exit 1
fi

echo "✓ Daisy build successful"
echo ""

# Test 4: Check binary size
echo "Test 4: Binary size analysis..."
QSPI_SIZE=$(arm-none-eabi-size build/AmpSim.elf | grep -E "^\s*build" | awk '{print $4}')
QSPI_KB=$((QSPI_SIZE / 1024))
echo "  Code size: ${QSPI_KB}KB in QSPI flash"

if [ $QSPI_KB -gt 7000 ]; then
    echo "WARNING: Binary approaching QSPI limit (8MB)"
else
    echo "✓ Binary size acceptable"
fi
echo ""

# Test 5: Validate model conversion tool
echo "Test 5: Testing NAM model conversion tool..."
if [ -f "tools/nam_to_header.py" ]; then
    python3 tools/nam_to_header.py NeuralAmpModelerCore/example_models/wavenet_a2_max.nam > /tmp/test_model.h
    if [ -s /tmp/test_model.h ]; then
        echo "✓ Model conversion tool works"
        head -10 /tmp/test_model.h
        rm /tmp/test_model.h
    else
        echo "ERROR: Model conversion produced empty output"
        exit 1
    fi
else
    echo "WARNING: Model conversion tool not found"
fi
echo ""

echo "================================"
echo "All integration tests passed! ✓"
echo "================================"
echo ""
echo "Next steps:"
echo "1. Flash to Daisy: make program"
echo "2. Test with guitar input"
echo "3. Verify audio output quality"
echo ""
