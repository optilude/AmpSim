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
    rp.process(0.7f, 0.7f, &l, &r);
    if (std::fabs(l - 0.7f) > 1e-6f || std::fabs(r - 0.7f) > 1e-6f) {
        printf("l=%.4f r=%.4f expected 0.7\n", l, r);
        return fail("mix=0 must give dry-only output");
    }
    printf("[PASS] mix=0.0 yields pure dry signal\n");

    // 3) mix=1 with an impulse — the wet tail must be non-zero eventually.
    rp.setMix(1.0f);
    rp.clear();
    // Feed an impulse then silence, watch for a non-zero tail within a
    // reasonable window (< 500 ms). Reverb has pre-delay/APF latency so
    // early samples may still be zero.
    //
    // Skip sample 0: dry does not fade out completely at full wet (it floors
    // at kMinDryAtFullWet), so the impulse itself appears in the output and
    // would dominate the peak, making this measure the dry path rather than
    // the tail it claims to.
    float maxAbs = 0.0f;
    for (int i = 0; i < 24000; ++i) {  // 500 ms at 48kHz
        const float x = (i == 0) ? 1.0f : 0.0f;
        rp.process(x, x, &l, &r);
        if (i == 0) continue;
        maxAbs = std::max(maxAbs, std::max(std::fabs(l), std::fabs(r)));
    }
    if (maxAbs < 1e-4f) {
        printf("maxAbs=%.6f\n", maxAbs);
        return fail("wet reverb tail was silent");
    }
    printf("[PASS] mix=1.0 produces non-zero wet tail (peak %.3f)\n", maxAbs);

    // 4) Silent input for a long time should decay towards zero.
    for (int i = 0; i < 480000; ++i) {  // 10 s
        rp.process(0.0f, 0.0f, &l, &r);
    }
    if (std::fabs(l) > 0.01f || std::fabs(r) > 0.01f) {
        printf("l=%.4f r=%.4f\n", l, r);
        return fail("reverb did not decay after silence");
    }
    printf("[PASS] Silent input eventually decays\n");

    // 5) Pin the mix law (see ReverbProcessor::setMix). For knob k:
    //        m   = k*k                                   (squared taper)
    //        wet = m
    //        dry = 1 - (1 - kMinDryAtFullWet) * m
    //
    //    Drive both knob positions with the same steady 1.0 input so the tank
    //    reaches the same wet signal W, then solve for W from the k=1 reading
    //    and predict the k=0.5 one. W cannot be read directly because dry does
    //    not vanish at full wet.
    //
    //    At k=0.5, m=0.25: dry 0.8, wet 0.25. Drop the squaring and this reads
    //    0.6/0.5 instead, which is the regression worth catching -- the taper
    //    is what keeps the bottom of the sweep usable.
    constexpr float kDryFloor = ReverbProcessor::kMinDryAtFullWet;
    rp.clear();
    rp.setMix(1.0f);
    float fullWet = 0.0f;
    for (int i = 0; i < 4800; ++i) {  // 100 ms warmup
        rp.process(1.0f, 1.0f, &l, &r);
        fullWet = l;
    }
    rp.clear();
    rp.setMix(0.5f);
    float halfMix = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        rp.process(1.0f, 1.0f, &l, &r);
        halfMix = l;
    }
    // fullWet = kDryFloor * 1.0 + 1.0 * W  =>  W = fullWet - kDryFloor
    const float W = fullWet - kDryFloor;
    const float m = 0.5f * 0.5f;
    const float expected = (1.0f - (1.0f - kDryFloor) * m) * 1.0f + m * W;
    if (std::fabs(halfMix - expected) > 0.02f) {
        printf("fullWet=%.4f W=%.4f halfMix=%.4f expected~%.4f\n",
               fullWet, W, halfMix, expected);
        return fail("knob=0.5 must give 0.8*dry + 0.25*wet (squared taper)");
    }
    printf("[PASS] knob=0.5 gives 0.8*dry + 0.25*wet (squared taper)\n");

    // 6) The dry floor itself: fully clockwise must still pass audible dry,
    //    which is the whole point of not using MuleBox's full fade-out.
    //    Feed silence until the tank is empty, then check a DC input comes
    //    through at the floor level.
    rp.clear();
    rp.setMix(1.0f);
    for (int i = 0; i < 4800; ++i) rp.process(0.0f, 1.0f, &l, &r);
    if (std::fabs(l - kDryFloor) > 1e-4f) {
        printf("l=%.4f expected %.4f\n", l, kDryFloor);
        return fail("full wet must still pass dry at kMinDryAtFullWet");
    }
    printf("[PASS] full wet still passes dry at %.2f\n", kDryFloor);

    printf("\nAll reverb tests passed.\n");
    return 0;
}
