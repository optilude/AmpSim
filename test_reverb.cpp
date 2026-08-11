// Desktop test for the Dattorro reverb + ReverbProcessor wet/dry mix.
//
// Verifies:
//   1) The SDRAM arena mechanism carves the delay lines from an external
//      buffer instead of the heap.
//   2) At mix=0.0 the output is exactly the dry input on both channels.
//   3) At mix=1.0 the output is non-zero (wet reverb) after a few blocks.
//   4) A silent input eventually decays to silence.
//
// Compile:
//   g++ -std=c++17 -O2 -o test_reverb test_reverb.cpp \
//       src/dattorro/Dattorro.cpp -Isrc

#include <cmath>
#include <cstdio>
#include <memory>

#include "dattorro/dsp/delays/InterpDelay.hpp"
#include "reverb_processor.h"

// Arena declaration — same layout main uses on device, allocated on the
// heap here for portability (desktop has plenty of room).
static float g_arena[262144];

static int fail(const char* msg) { fprintf(stderr, "[FAIL] %s\n", msg); return 1; }

int main() {
    printf("Reverb desktop test\n");
    printf("===================\n\n");

    // 1) Arena installed before ReverbProcessor::init().
    InterpDelayArena::set(g_arena, sizeof g_arena / sizeof g_arena[0]);
    ReverbProcessor rp;
    rp.init(48000.0f);
    const size_t arenaUsed = InterpDelayArena::used();
    printf("[INFO] Reverb arena used: %zu floats (%.1f KB)\n",
           arenaUsed, arenaUsed * 4.0 / 1024.0);
    if (arenaUsed == 0) return fail("arena not consumed by reverb init");
    if (InterpDelayArena::exhausted()) return fail("reverb arena exhausted and fell back to heap");
    if (arenaUsed > 262144) return fail("arena exhausted");
    printf("[PASS] Reverb allocated from arena\n");

    // Now un-install the arena — any stray allocation would fall back to
    // heap-owned storage, which is what we want after construction.
    InterpDelayArena::set(nullptr, 0);

    // 2) mix=0 => pure dry.
    rp.setMix(0.0f);
    float l = 0, r = 0;
    rp.process(0.7f, &l, &r);
    if (std::fabs(l - 0.7f) > 1e-6f || std::fabs(r - 0.7f) > 1e-6f) {
        printf("l=%.4f r=%.4f expected 0.7\n", l, r);
        return fail("mix=0 must give dry-only output");
    }
    printf("[PASS] mix=0.0 yields pure dry signal\n");

    // 3) mix=1 with an impulse — wet-only, must be non-zero eventually.
    rp.setMix(1.0f);
    rp.clear();
    // Feed an impulse then silence, watch for a non-zero tail within a
    // reasonable window (< 500 ms). Reverb has pre-delay/APF latency so
    // early samples may still be zero.
    float maxAbs = 0.0f;
    for (int i = 0; i < 24000; ++i) {  // 500 ms at 48kHz
        const float x = (i == 0) ? 1.0f : 0.0f;
        rp.process(x, &l, &r);
        maxAbs = std::max(maxAbs, std::max(std::fabs(l), std::fabs(r)));
    }
    if (maxAbs < 1e-4f) {
        printf("maxAbs=%.6f\n", maxAbs);
        return fail("wet reverb tail was silent");
    }
    printf("[PASS] mix=1.0 produces non-zero wet tail (peak %.3f)\n", maxAbs);

    // 4) Silent input for a long time should decay towards zero.
    for (int i = 0; i < 480000; ++i) {  // 10 s
        rp.process(0.0f, &l, &r);
    }
    if (std::fabs(l) > 0.01f || std::fabs(r) > 0.01f) {
        printf("l=%.4f r=%.4f\n", l, r);
        return fail("reverb did not decay after silence");
    }
    printf("[PASS] Silent input eventually decays\n");

    // 5) At mix=0.5 with a steady-state 1.0 dry input, the dry contribution
    //    to the output should be exactly 0.5 (the wet contribution adds a
    //    slowly-varying reverb signal on top). We check that the dry math
    //    is applied by comparing mix=1.0 vs mix=0.5 outputs — the difference
    //    should be exactly 0.5 * dry on average.
    rp.clear();
    rp.setMix(1.0f);
    float wetOnly = 0.0f;
    for (int i = 0; i < 4800; ++i) {  // 100 ms warmup
        rp.process(1.0f, &l, &r);
        wetOnly = l;
    }
    rp.clear();
    rp.setMix(0.5f);
    float halfMix = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        rp.process(1.0f, &l, &r);
        halfMix = l;
    }
    const float expectedDryContribution = 0.5f * 1.0f + 0.5f * wetOnly;
    if (std::fabs(halfMix - expectedDryContribution) > 0.02f) {
        printf("wetOnly=%.4f halfMix=%.4f expected~%.4f\n",
               wetOnly, halfMix, expectedDryContribution);
        return fail("mix=0.5 must be 0.5*dry + 0.5*wet");
    }
    printf("[PASS] mix=0.5 is 0.5*dry + 0.5*wet within tolerance\n");

    printf("\nAll reverb tests passed.\n");
    return 0;
}
