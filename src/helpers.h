#pragma once
#include <cmath>
#include "constants.h"

// Convert knob value (0.0..1.0) to normalized -1..+1.
// Noon (0.5) = 0.0, CCW = negative, CW = positive.
inline float KnobToNormalized(float knob) {
    return (knob - 0.5f) * 2.0f;
}

// Convert knob value (0.0..1.0) to a linear gain multiplier for +/- GAIN_RANGE_DB.
// Noon (0.5) = unity, CCW = attenuation, CW = boost.
inline float KnobToGain(float knob) {
    float db = KnobToNormalized(knob) * GAIN_RANGE_DB;
    return std::pow(10.0f, db / 20.0f);
}

// Convert knob value (0.0..1.0) to EQ gain in dB (+/- EQ_RANGE_DB).
inline float KnobToEqDb(float knob) {
    return KnobToNormalized(knob) * EQ_RANGE_DB;
}

// Convert dB to linear multiplier.
inline float DbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}
