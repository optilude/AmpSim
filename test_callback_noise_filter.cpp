#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "callback_noise_filter.h"

static constexpr float kSampleRate = 48000.0f;

static float MeasureGainDb(float frequency) {
    CallbackNoiseFilter filter;
    filter.Init(kSampleRate);

    double inputPower = 0.0;
    double outputPower = 0.0;
    constexpr int kSamples = 48000;
    constexpr int kSettleSamples = 24000;
    for (int i = 0; i < kSamples; ++i) {
        const float input = std::sin(2.0f * float(M_PI) * frequency * i / kSampleRate);
        const float output = filter.Process(input);
        if (i >= kSettleSamples) {
            inputPower += double(input) * input;
            outputPower += double(output) * output;
        }
    }
    return 10.0f * std::log10(float(outputPower / inputPower));
}

int main() {
    for (float frequency : {1000.0f, 2000.0f, 3000.0f}) {
        const float gainDb = MeasureGainDb(frequency);
        if (gainDb > -50.0f) {
            std::fprintf(stderr, "[FAIL] %.0f Hz rejection was only %.2f dB\n",
                         frequency, gainDb);
            return 1;
        }
    }

    for (float frequency : {900.0f, 1100.0f, 1900.0f, 2100.0f, 2900.0f, 3100.0f}) {
        const float gainDb = MeasureGainDb(frequency);
        if (gainDb < -1.0f) {
            std::fprintf(stderr, "[FAIL] %.0f Hz adjacent loss was %.2f dB\n",
                         frequency, gainDb);
            return 1;
        }
    }

    CallbackNoiseFilter filter;
    CallbackNoiseFilter freshFilter;
    filter.Init(kSampleRate);
    freshFilter.Init(kSampleRate);
    for (int i = 0; i < 1000; ++i) filter.Process(i == 0 ? 1.0f : 0.0f);
    filter.Reset();
    for (int i = 0; i < 100; ++i) {
        const float input = std::sin(2.0f * float(M_PI) * 1234.0f * i / kSampleRate);
        if (filter.Process(input) != freshFilter.Process(input)) {
            std::fprintf(stderr, "[FAIL] Reset did not restore fresh filter state\n");
            return 1;
        }
    }

    std::printf("[PASS] Callback-noise notches reject 1/2/3 kHz, preserve adjacent frequencies, and reset cleanly\n");
    return 0;
}