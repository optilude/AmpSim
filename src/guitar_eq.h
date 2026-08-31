#pragma once
#include <cmath>
#include "constants.h"

// Three-band tone stack: low shelf, mid peaking, high shelf.
// Uses direct-form-I biquads with coefficients from the RBJ audio-EQ
// cookbook. Coefficients are recomputed only when the gain in dB changes
// (float equality is fine here — the change originates from the knob's
// deadband filter so consecutive identical values are the norm).
//
// Frequencies and Q are fixed at compile time (see constants.h). The
// range is +/- EQ_RANGE_DB (default 12 dB).

class GuitarEQ {
public:
    void Init(float sampleRate) {
        sampleRate_ = sampleRate;
        bass_.init(sampleRate, BASS_FREQ, BASS_Q, BiquadKind::LowShelf, 0.0f);
        mid_.init(sampleRate, MID_FREQ, MID_Q, BiquadKind::Peaking, 0.0f);
        treble_.init(sampleRate, TREBLE_FREQ, TREBLE_Q, BiquadKind::HighShelf, 0.0f);
    }

    // Gain is normalized -1..+1 (matches KnobToNormalized output).
    // -1 = -EQ_RANGE_DB, 0 = flat, +1 = +EQ_RANGE_DB.
    void SetBass(float gainNorm) { bass_.setGainDb(gainNorm * EQ_RANGE_DB); }
    void SetMid(float gainNorm) { mid_.setGainDb(gainNorm * EQ_RANGE_DB); }
    void SetTreble(float gainNorm) { treble_.setGainDb(gainNorm * EQ_RANGE_DB); }

    // Rewind the filter state without touching the coefficients. DF-I biquads
    // feed their own output back, so one non-finite sample latches the band
    // permanently; this is the only way back short of a reboot.
    void Reset() {
        bass_.resetState();
        mid_.resetState();
        treble_.resetState();
    }

    float Process(float in) {
        float x = bass_.process(in);
        x = mid_.process(x);
        x = treble_.process(x);
        return x;
    }

    void ProcessBlock(const float* in, float* out, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            out[i] = Process(in[i]);
        }
    }

private:
    enum class BiquadKind { LowShelf, HighShelf, Peaking };

    struct Biquad {
        // Coefficients (normalized by a0).
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        // State (DF-I: two-sample x/y history).
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;

        float sampleRate = 48000.0f;
        float freq = 1000.0f;
        float q = 0.707f;
        float gainDb = 0.0f;
        BiquadKind kind = BiquadKind::Peaking;
        bool bypass = true;

        void init(float sr, float f, float qFactor, BiquadKind k, float gDb) {
            sampleRate = sr;
            freq = f;
            q = qFactor;
            kind = k;
            gainDb = 0.0f;  // Force setGainDb below to run.
            setGainDb(gDb);
            resetState();
        }

        void resetState() { x1 = x2 = y1 = y2 = 0.0f; }

        void setGainDb(float g) {
            if (g == gainDb) return;
            gainDb = g;
            // Bypass path when the gain is close to zero saves cycles and
            // guarantees perfect unity at knob-noon.
            if (std::fabs(gainDb) < 0.05f) {
                bypass = true;
                b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
                a1 = 0.0f; a2 = 0.0f;
                return;
            }
            bypass = false;
            recompute();
        }

        // RBJ cookbook coefficients.
        //   A     = 10^(gainDb/40)
        //   w0    = 2*pi*freq/sampleRate
        //   alpha = sin(w0) / (2*Q)
        // Reference: https://www.w3.org/TR/audio-eq-cookbook/
        void recompute() {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            const float w0 = 2.0f * float(M_PI) * freq / sampleRate;
            const float cw = std::cos(w0);
            const float sw = std::sin(w0);
            const float alpha = sw / (2.0f * q);

            float b0n, b1n, b2n, a0n, a1n, a2n;
            switch (kind) {
                case BiquadKind::LowShelf: {
                    const float sqA2alpha = 2.0f * std::sqrt(A) * alpha;
                    b0n =        A * ((A + 1) - (A - 1) * cw + sqA2alpha);
                    b1n =  2.0f * A * ((A - 1) - (A + 1) * cw);
                    b2n =        A * ((A + 1) - (A - 1) * cw - sqA2alpha);
                    a0n =             (A + 1) + (A - 1) * cw + sqA2alpha;
                    a1n = -2.0f *    ((A - 1) + (A + 1) * cw);
                    a2n =             (A + 1) + (A - 1) * cw - sqA2alpha;
                    break;
                }
                case BiquadKind::HighShelf: {
                    const float sqA2alpha = 2.0f * std::sqrt(A) * alpha;
                    b0n =        A * ((A + 1) + (A - 1) * cw + sqA2alpha);
                    b1n = -2.0f * A * ((A - 1) + (A + 1) * cw);
                    b2n =        A * ((A + 1) + (A - 1) * cw - sqA2alpha);
                    a0n =             (A + 1) - (A - 1) * cw + sqA2alpha;
                    a1n =  2.0f *    ((A - 1) - (A + 1) * cw);
                    a2n =             (A + 1) - (A - 1) * cw - sqA2alpha;
                    break;
                }
                case BiquadKind::Peaking:
                default: {
                    b0n =  1.0f + alpha * A;
                    b1n = -2.0f * cw;
                    b2n =  1.0f - alpha * A;
                    a0n =  1.0f + alpha / A;
                    a1n = -2.0f * cw;
                    a2n =  1.0f - alpha / A;
                    break;
                }
            }
            const float inv = 1.0f / a0n;
            b0 = b0n * inv;
            b1 = b1n * inv;
            b2 = b2n * inv;
            a1 = a1n * inv;
            a2 = a2n * inv;
        }

        inline float process(float x) {
            if (bypass) return x;
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    float sampleRate_ = 48000.0f;
    Biquad bass_;
    Biquad mid_;
    Biquad treble_;
};
