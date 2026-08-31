#pragma once

// Non-finite detection that survives -ffast-math.
//
// The project builds with -Ofast, which implies -ffinite-math-only: the
// compiler is then entitled to fold std::isnan/std::isfinite to a constant
// false. Every guard written that way silently disappears. Inspect the bit
// pattern instead, which no floating-point optimisation can reason away.
//
// This matters because a single non-finite sample is unrecoverable downstream:
// the EQ's direct-form-I biquads and the reverb tank both feed their output
// back into their state, so NaN latches until the state is explicitly cleared.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fguard {

// True for inf and NaN (IEEE-754 binary32: exponent field all ones).
inline bool IsNonFinite(float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    return (bits & 0x7f800000u) == 0x7f800000u;
}

// Scan a block for non-finite samples. Returns true (and silences the block)
// if any were found, so the caller can tear down and rebuild DSP state rather
// than letting the poison recirculate.
inline bool SilenceIfNonFinite(float* buf, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (IsNonFinite(buf[i])) {
            for (size_t j = 0; j < n; ++j) buf[j] = 0.0f;
            return true;
        }
    }
    return false;
}

}  // namespace fguard
