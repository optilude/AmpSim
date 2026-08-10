# Patches for Git Submodules

This directory contains patches that must be applied to git submodules for bare-metal compatibility.

## Why Patches Are Needed

The NeuralAmpModelerCore library targets desktop systems (Windows/macOS/Linux) with full C++ runtime support. Running on bare-metal ARM (Daisy Seed) requires modifications that won't be accepted upstream.

## Patches

### `remove_thread_local.patch`

**Problem**: Bare-metal ARM toolchain doesn't support `thread_local` storage (no OS threads).

**Solution**: Replace `thread_local bool` with regular `bool` global variable.

**Impact**: Safe for single-threaded firmware. The variable `gPrewarmOnResetDefault` is only used during model initialization (not in real-time audio path).

**Upstream Issue**: https://github.com/sdatkinson/NeuralAmpModelerCore/issues/XXX (thread_local prevents bare-metal use)

## Applying Patches

After cloning or updating submodules, run:

```bash
./apply_patches.sh
```

This script is idempotent (safe to run multiple times).

## Verification

To verify patches are applied:

```bash
cd NeuralAmpModelerCore
git status
# Should show: modified: NAM/dsp.cpp
```

## Updating Patches

If you need to modify a submodule:

1. Make your changes in the submodule directory
2. Generate a new patch:
   ```bash
   cd NeuralAmpModelerCore
   git diff NAM/dsp.cpp > ../patches/remove_thread_local.patch
   ```
3. Test that the patch applies cleanly:
   ```bash
   git checkout NAM/dsp.cpp
   git apply ../patches/remove_thread_local.patch
   ```
