#pragma once

#include <cmath>
#include <cstddef>

// Narrow notches for callback-periodic input interference measured at
// 1/2/3/4 kHz.
class CallbackNoiseFilter {
public:
    void Init(float sampleRate) {
        notches_[0].Init(sampleRate, 1000.0f, kQ);
        notches_[1].Init(sampleRate, 2000.0f, kQ);
        notches_[2].Init(sampleRate, 3000.0f, kQ);
        notches_[3].Init(sampleRate, 4000.0f, kQ);
    }

    void Reset() {
        for (Notch& notch : notches_) notch.Reset();
    }

    float Process(float input) {
        for (Notch& notch : notches_) input = notch.Process(input);
        return input;
    }

    void ProcessBlock(float* samples, size_t count) {
        for (size_t i = 0; i < count; ++i) samples[i] = Process(samples[i]);
    }

private:
    static constexpr float kQ = 40.0f;

    struct Notch {
        void Init(float sampleRate, float frequency, float q) {
            const float w0 = 2.0f * float(M_PI) * frequency / sampleRate;
            const float alpha = std::sin(w0) / (2.0f * q);
            const float invA0 = 1.0f / (1.0f + alpha);
            b0 = invA0;
            b1 = -2.0f * std::cos(w0) * invA0;
            b2 = invA0;
            a1 = b1;
            a2 = (1.0f - alpha) * invA0;
            Reset();
        }

        void Reset() { x1 = x2 = y1 = y2 = 0.0f; }

        float Process(float input) {
            const float output = b0 * input + b1 * x1 + b2 * x2
                               - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = input;
            y2 = y1;
            y1 = output;
            return output;
        }

        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    Notch notches_[4];
};