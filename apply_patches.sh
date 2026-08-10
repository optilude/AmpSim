#!/bin/bash
# Apply patches to git submodules for bare-metal compatibility
# Run this after: git submodule update --init --recursive

set -e

echo "Applying patches to submodules..."

# Patch 1: Remove thread_local from NeuralAmpModelerCore
# Required for bare-metal ARM toolchain (no thread-local storage)
if [ -f "patches/remove_thread_local.patch" ]; then
    echo "  Applying remove_thread_local.patch..."
    cd NeuralAmpModelerCore
    if git apply --check ../patches/remove_thread_local.patch 2>/dev/null; then
        git apply ../patches/remove_thread_local.patch
        echo "  ✓ Patch applied successfully"
    else
        echo "  ℹ Patch already applied or not needed"
    fi
    cd ..
fi

echo "✓ All patches applied"
