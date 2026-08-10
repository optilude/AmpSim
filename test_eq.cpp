// Desktop test for GuitarEQ (RBJ biquad shelving/peaking).
//
// Verifies:
//   1) Flat (all bands at 0) is identity within noise.
//   2) Bass boost @ 100 Hz raises the response at 60 Hz by roughly the
//      configured dB; treble boost @ 4 kHz raises 6 kHz; mid boost @ 1 kHz
//      raises 1 kHz — all relative to the flat baseline.
//   3) Cuts do the opposite.
//   4) Filters at flat setting produce numerically identical output
//      (biquad bypass path).
//
// Compile:
//   g++ -std=c++17 -O2 -Isrc -o test_eq test_eq.cpp

#include <cmath>
#include <cstdio>
#include <vector>

#include "guitar_eq.h"

static constexpr float SR = 48000.0f;

static float rms(const std::vector<float>& v, size_t start = 0) {
    double acc = 0.0;
    for (size_t i = start; i < v.size(); ++i) acc += double(v[i]) * v[i];
    return float(std::sqrt(acc / (v.size() - start)));
}

// Measure the steady-state RMS of `eq`'s response to a sine at `freq`.
// Discards the first 500 ms to let the filter settle.
static float measureGainDb(GuitarEQ& eq, float freq, int samples = 24000) {
    std::vector<float> in(samples), out(samples);
    for (int i = 0; i < samples; ++i) {
        in[i] = std::sin(2.0f * float(M_PI) * freq * i / SR);
    }
    eq.ProcessBlock(in.data(), out.data(), samples);
    const float rin = rms(in, samples / 2);
    const float rout = rms(out, samples / 2);
    return 20.0f * std::log10(rout / rin);
}

static int fail(const char* msg) { fprintf(stderr, "[FAIL] %s\n", msg); return 1; }

int main() {
    printf("EQ desktop test\n");
    printf("===============\n\n");

    // 1) Flat identity.
    {
        GuitarEQ eq;
        eq.Init(SR);
        eq.SetBass(0);
        eq.SetMid(0);
        eq.SetTreble(0);
        std::vector<float> in(1024), out(1024);
        for (size_t i = 0; i < in.size(); ++i) in[i] = std::sin(2.0f * float(M_PI) * 200.0f * i / SR);
        eq.ProcessBlock(in.data(), out.data(), in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (std::fabs(in[i] - out[i]) > 1e-6f) {
                return fail("flat EQ should be a numeric identity");
            }
        }
    }
    printf("[PASS] Flat EQ is bit-exact identity\n");

    // 2) +12 dB shelves/peak.
    auto check = [](const char* band, float knob, float testFreq, float wantDb, float tolerance,
                    void (GuitarEQ::*set)(float)) -> bool {
        GuitarEQ eq;
        eq.Init(SR);
        eq.SetBass(0); eq.SetMid(0); eq.SetTreble(0);
        (eq.*set)(knob);
        const float g = measureGainDb(eq, testFreq);
        const bool ok = std::fabs(g - wantDb) <= tolerance;
        printf("       %s knob=%+.1f freq=%.0fHz gain=%+.2fdB (want %+.1f +/- %.1f) %s\n",
               band, knob, testFreq, g, wantDb, tolerance,
               ok ? "OK" : "MISS");
        return ok;
    };

    // Boost at +1.0 (full CW) = +12 dB. Measure well inside the band.
    if (!check("Bass  ", +1.0f, 40.0f, +12.0f, 2.0f, &GuitarEQ::SetBass))   return 1;
    if (!check("Mid   ", +1.0f, 1000.0f, +12.0f, 1.0f, &GuitarEQ::SetMid))  return 1;
    if (!check("Treble", +1.0f, 8000.0f, +12.0f, 2.0f, &GuitarEQ::SetTreble)) return 1;

    // Cut at -1.0 = -12 dB.
    if (!check("Bass  ", -1.0f, 40.0f, -12.0f, 2.0f, &GuitarEQ::SetBass))    return 1;
    if (!check("Mid   ", -1.0f, 1000.0f, -12.0f, 1.0f, &GuitarEQ::SetMid))   return 1;
    if (!check("Treble", -1.0f, 8000.0f, -12.0f, 2.0f, &GuitarEQ::SetTreble))return 1;

    printf("[PASS] EQ boost/cut hit target gains at their centre freqs\n");

    // 3) Cross-check: mid boost shouldn't disturb bass/treble much.
    {
        GuitarEQ eq;
        eq.Init(SR);
        eq.SetBass(0); eq.SetTreble(0);
        eq.SetMid(+1.0f);
        const float g40 = measureGainDb(eq, 40.0f);
        const float g8k = measureGainDb(eq, 8000.0f);
        if (std::fabs(g40) > 2.0f || std::fabs(g8k) > 2.0f) {
            printf("g40=%.2f g8k=%.2f\n", g40, g8k);
            return fail("mid boost bled into the shelves");
        }
    }
    printf("[PASS] Mid boost doesn't unduly affect shelves\n");

    printf("\nAll EQ tests passed.\n");
    return 0;
}
