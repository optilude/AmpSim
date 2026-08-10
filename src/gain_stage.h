#pragma once
#include <cmath>
#include <cstddef>
#include "constants.h"

// Smoothed gain stage.
// Input knob value is in the -1..+1 range (see KnobToNormalized). Gain
// range is +/- GAIN_RANGE_DB. Smoothing is a simple one-pole IIR with a
// coefficient chosen to give ~10 ms time constant at 48 kHz. The stage
// is stereo/mono-agnostic: call ProcessSample() once per sample-frame
// and multiply into as many channels as you like using CurrentGain().
class GainStage {
public:
    void Init(float sampleRate = 48000.0f) {
        // Time constant in samples for one-pole IIR: y += alpha*(x - y)
        // We want ~10 ms glide. alpha = 1 - exp(-1/(tau*fs)).
        // For fs=48000, tau=0.010, that's ~0.00478. Keep the simple formula
        // in the update path for cost.
        const float tau = 0.010f;
        smoothCoef_ = 1.0f - std::exp(-1.0f / (tau * sampleRate));
        gain_ = 1.0f;
        targetGain_ = 1.0f;
    }

    // Set target gain from a normalized knob value (-1..+1).
    // -1 = -GAIN_RANGE_DB, 0 = 0 dB, +1 = +GAIN_RANGE_DB.
    void SetGain(float knobValueNorm) {
        const float db = knobValueNorm * GAIN_RANGE_DB;
        targetGain_ = std::pow(10.0f, db / 20.0f);
    }

    // Advance the smoother once per sample frame and return current gain.
    // Prefer this over Process() when you need to multiply the same gain
    // into more than one channel.
    inline float Tick() {
        gain_ += (targetGain_ - gain_) * smoothCoef_;
        return gain_;
    }

    // Convenience: tick + multiply for a single mono sample.
    inline float Process(float in) {
        return in * Tick();
    }

    // Block-process a mono signal in place (or to a separate output).
    void ProcessBlock(const float* in, float* out, size_t n) {
        for (size_t i = 0; i < n; ++i) out[i] = in[i] * Tick();
    }

    float GetGainLinear() const { return gain_; }
    float GetGainDb() const { return 20.0f * std::log10(gain_); }

private:
    float gain_ = 1.0f;
    float targetGain_ = 1.0f;
    float smoothCoef_ = 0.005f;
};
