#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef NAM_A2_ALIGN32
#define NAM_A2_ALIGN32 alignas(32)
#endif

#ifndef NAM_A2_HOT_DATA
#if defined(__APPLE__)
#define NAM_A2_HOT_DATA
#else
#define NAM_A2_HOT_DATA __attribute__((section(".dtcmram_bss")))
#endif
#endif

#ifndef NAM_A2_HOT_STATE_DATA
#if defined(__APPLE__)
#define NAM_A2_HOT_STATE_DATA
#else
#define NAM_A2_HOT_STATE_DATA __attribute__((section(".dtcmram_bss")))
#endif
#endif

#ifndef NAM_A2_STATE_DATA
#if defined(__APPLE__)
#define NAM_A2_STATE_DATA
#else
#define NAM_A2_STATE_DATA __attribute__((section(".sram_d2_bss")))
#endif
#endif

namespace nam_a2 {

static constexpr int kBlockSize = 48;
static constexpr int kChannels = 3;
static constexpr int kNumLayers = 23;
static constexpr int kHeadKernel = 16;
static constexpr int kA2WeightCount = 1871;
static constexpr float kLeakySlope = 0.01f;

inline constexpr int kKernelSizes[kNumLayers] = {
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6
};

inline constexpr int kDilations[kNumLayers] = {
    1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239
};

// How many samples process_layer handles at once. The dilated convolution is
// 9 multiply-accumulates per tap against 9 weights; done one sample at a time
// that is 12 loads (9 weights + 3 history) to feed 9 FMAs, so the loop is
// load-bound rather than FPU-bound. Handling a group of samples per tap loads
// the 9 weights once for the whole group: at 4, 21 loads feed 36 FMAs.
//
// 4 is the sweet spot on Cortex-M7's 32 single-precision registers: 12
// accumulators plus 9 weights plus 3 history values is 24, which stays in
// registers. 8 would want 33 and spill. Must divide kBlockSize.
static constexpr int kSampleGroup = 4;
static_assert(kBlockSize % kSampleGroup == 0, "group must tile the block");

// A tap reads kSampleGroup consecutive columns starting anywhere in
// [0, cols), so the tail of a group can run off the end of the ring. Testing
// for that costs a compare, a branch and a fresh base-address multiply on
// every sample of every tap -- which was the bulk of the non-arithmetic work
// in the inner loop. Instead the ring is allocated with that many spare
// columns on the end, and the write step mirrors into them; the tap loop then
// walks a plain pointer with no wrap logic at all.
static constexpr int kGuardCols = kSampleGroup - 1;

// Per-layer history ring geometry, derived rather than tabulated so the
// +kSampleGroup below cannot drift out of sync with the loop that needs it.
//
// A layer's convolution reaches back (kernelSize-1)*dilation samples, so that
// many columns plus the current one are live; +1 more is the classic ring
// slack. The grouped loop then writes a whole group of new input samples
// before reading any taps, so the oldest tap of the group's first sample must
// survive kSampleGroup writes -- hence the extra headroom. Without it, layer 0
// (7 columns, reach 5) would overwrite its own history mid-group.
//
// cols is the modulus; the allocation is cols + kGuardCols columns wide.
struct HistoryLayout {
    int cols[kNumLayers];
    int offset[kNumLayers];
    int total;
};

inline constexpr HistoryLayout MakeHistoryLayout() {
    HistoryLayout h{};
    int off = 0;
    for (int i = 0; i < kNumLayers; ++i) {
        h.cols[i] = (kKernelSizes[i] - 1) * kDilations[i] + 2 + kSampleGroup;
        h.offset[i] = off;
        off += (h.cols[i] + kGuardCols) * kChannels;
    }
    h.total = off;
    return h;
}

inline constexpr HistoryLayout kHistory = MakeHistoryLayout();

inline constexpr int kLayerConvOffset[kNumLayers] = {
    0, 54, 108, 162, 216, 270, 324, 378, 432, 486, 540, 594,
    648, 702, 756, 891, 1026, 1080, 1134, 1188, 1242, 1296, 1350
};

inline constexpr int kLayerL1Offset[kNumLayers] = {
    0, 9, 18, 27, 36, 45, 54, 63, 72, 81, 90, 99,
    108, 117, 126, 135, 144, 153, 162, 171, 180, 189, 198
};

static constexpr int kKernelSum = 156;
static constexpr int kConvWeightCount = kKernelSum * kChannels * kChannels;
static constexpr int kLayer1x1WeightCount = kNumLayers * kChannels * kChannels;
static constexpr int kHistoryFloats = kHistory.total;

struct LayerRuntime {
    NAM_A2_ALIGN32 float convB[3];
    NAM_A2_ALIGN32 float mixinW[3];
    NAM_A2_ALIGN32 float preCurrent[3];
    NAM_A2_ALIGN32 float l1x1B[3];
};

struct SharedWeights {
    NAM_A2_ALIGN32 float rechannel[3];
    NAM_A2_ALIGN32 float convW[kConvWeightCount];
    NAM_A2_ALIGN32 float l1x1W[kLayer1x1WeightCount];
    NAM_A2_ALIGN32 float headW[kHeadKernel][3];
    float headB;
    float headScale;
    LayerRuntime layer[kNumLayers];
    bool loaded;
};

struct State {
    NAM_A2_ALIGN32 float history[kHistoryFloats];
    int layerWritePos[kNumLayers];
};

struct HotState {
    NAM_A2_ALIGN32 float headHistory[kHeadKernel * 3];
    NAM_A2_ALIGN32 float bufA[kBlockSize * 3];
    NAM_A2_ALIGN32 float bufB[kBlockSize * 3];
    NAM_A2_ALIGN32 float headSum[kBlockSize * 3];
    int headWritePos = 0;
};

inline float leaky(float x) {
    return x >= 0.0f ? x : x * kLeakySlope;
}

inline bool load_weights(SharedWeights& dst, const float* src, size_t count) {
    if (!src || count != static_cast<size_t>(kA2WeightCount)) return false;
    std::memset(&dst, 0, sizeof(dst));

    size_t p = 0;
    auto take = [&]() { return src[p++]; };

    for (int c = 0; c < 3; ++c) dst.rechannel[c] = take();

    int convOff = 0;
    int l1Off = 0;
    for (int li = 0; li < kNumLayers; ++li) {
        LayerRuntime& L = dst.layer[li];
        const int kernelSize = kKernelSizes[li];

        for (int out = 0; out < 3; ++out)
            for (int in = 0; in < 3; ++in)
                for (int k = 0; k < kernelSize; ++k)
                    dst.convW[convOff + k * 9 + in * 3 + out] = take();

        for (int out = 0; out < 3; ++out) L.convB[out] = take();
        for (int out = 0; out < 3; ++out) L.mixinW[out] = take();

        for (int out = 0; out < 3; ++out)
            for (int in = 0; in < 3; ++in)
                dst.l1x1W[l1Off + in * 3 + out] = take();

        for (int out = 0; out < 3; ++out) L.l1x1B[out] = take();

        if (li == 0 && kernelSize == 6) {
            const float* wc = dst.convW + convOff + 5 * 9;
            for (int out = 0; out < 3; ++out) {
                L.preCurrent[out] = L.mixinW[out]
                    + dst.rechannel[0] * wc[0 * 3 + out]
                    + dst.rechannel[1] * wc[1 * 3 + out]
                    + dst.rechannel[2] * wc[2 * 3 + out];
            }
        }

        convOff += kernelSize * 9;
        l1Off += 9;
    }

    for (int in = 0; in < 3; ++in)
        for (int k = 0; k < kHeadKernel; ++k)
            dst.headW[k][in] = take();

    dst.headB = take();
    dst.headScale = take();
    dst.loaded = (p == count && convOff == kConvWeightCount && l1Off == kLayer1x1WeightCount);
    return dst.loaded;
}

inline void reset_state(State& st, HotState& hot) {
    std::fill(st.history, st.history + kHistoryFloats, 0.0f);
    std::fill(hot.headHistory, hot.headHistory + kHeadKernel * 3, 0.0f);
    std::fill(hot.bufA, hot.bufA + kBlockSize * 3, 0.0f);
    std::fill(hot.bufB, hot.bufB + kBlockSize * 3, 0.0f);
    std::fill(hot.headSum, hot.headSum + kBlockSize * 3, 0.0f);
    for (int i = 0; i < kNumLayers; ++i) st.layerWritePos[i] = kHistory.cols[i] - 1;
    hot.headWritePos = kHeadKernel - 1;
}

inline constexpr int prewarm_samples() {
    int total = kHeadKernel - 1;
    for (int i = 0; i < kNumLayers; ++i) total += (kKernelSizes[i] - 1) * kDilations[i];
    return total;
}

// One dilated-convolution layer, kSampleGroup samples at a time.
//
// The loop nest is (group, tap, sample-within-group) rather than the obvious
// (sample, tap). Two things fall out of that:
//
//   - the tap's 9 weights are loaded once per group instead of once per
//     sample, which is what takes the loop from load-bound towards FPU-bound;
//   - within a tap the history column advances by one per sample, so the reads
//     are consecutive. Sample-major order instead strides by dilation*3 floats
//     -- for the dilation-239 layers that is a fresh cache line on every
//     single tap.
//
// Taps are still accumulated in ascending k for each sample, so the result is
// bit-identical to the sample-major version, not merely close. tools/nam_bench
// checks exactly that.
//
// `templatedPrecombine` is the li == 0 special case, passed as a template
// argument so the test leaves the inner loops entirely.
template <bool kPrecombine>
inline void process_layer_impl(State& st, HotState& hot, const SharedWeights& sw,
                               int li, const float* cond, const float* in, float* out) {
    const LayerRuntime& L = sw.layer[li];
    int wp = st.layerWritePos[li];
    const int cols = kHistory.cols[li];
    const int dilation = kDilations[li];
    const int kernelSize = kKernelSizes[li];
    float* const hist = st.history + kHistory.offset[li];
    const float* const wAll = sw.convW + kLayerConvOffset[li];
    const float* const lx = sw.l1x1W + kLayerL1Offset[li];

    // The per-sample conditioning gain: layer 0 folds the rechannel stage and
    // its own last tap into one coefficient, so it also skips that tap below.
    const float* const mix = kPrecombine ? L.preCurrent : L.mixinW;
    const int taps = kPrecombine ? kernelSize - 1 : kernelSize;

    for (int n0 = 0; n0 < kBlockSize; n0 += kSampleGroup) {
        // 1. Publish this group's inputs into the ring before any tap reads,
        //    because the newest taps read them back. Safe only because the
        //    ring carries kSampleGroup columns of headroom; see kHistory.
        //
        //    The first kGuardCols columns are mirrored to the guard copies at
        //    the end, so a tap whose group straddles the wrap point still
        //    reads current data from a straight run of memory.
        for (int j = 0; j < kSampleGroup; ++j) {
            const float* src = in + (n0 + j) * 3;
            int col = wp + j;
            if (col >= cols) col -= cols;
            float* histCol = hist + col * 3;
            histCol[0] = src[0]; histCol[1] = src[1]; histCol[2] = src[2];
            if (col < kGuardCols) {
                float* guard = histCol + cols * 3;
                guard[0] = src[0]; guard[1] = src[1]; guard[2] = src[2];
            }
        }

        // 2. Seed the accumulators. Small enough to stay in registers once the
        //    constant-bound loops are unrolled.
        float z[kSampleGroup][3];
        for (int j = 0; j < kSampleGroup; ++j) {
            const float c = cond[n0 + j];
            z[j][0] = L.convB[0] + mix[0] * c;
            z[j][1] = L.convB[1] + mix[1] * c;
            z[j][2] = L.convB[2] + mix[2] * c;
        }

        // 3. Taps outer, samples inner: 9 weight loads per tap per group.
        for (int k = 0; k < taps; ++k) {
            const float* const w = wAll + k * 9;
            const float w0 = w[0], w1 = w[1], w2 = w[2];
            const float w3 = w[3], w4 = w[4], w5 = w[5];
            const float w6 = w[6], w7 = w[7], w8 = w[8];

            // The reach is at most cols - 2, and wp is in [0, cols), so one
            // correction is always enough -- no loop needed.
            int col = wp - (kernelSize - 1 - k) * dilation;
            if (col < 0) col += cols;

            // col is at most cols-1 and the run is kSampleGroup long, so the
            // last read is at cols + kGuardCols - 1: the last guard column.
            const float* s = hist + col * 3;
            for (int j = 0; j < kSampleGroup; ++j, s += 3) {
                const float s0 = s[0], s1 = s[1], s2 = s[2];
                z[j][0] += s0 * w0 + s1 * w3 + s2 * w6;
                z[j][1] += s0 * w1 + s1 * w4 + s2 * w7;
                z[j][2] += s0 * w2 + s1 * w5 + s2 * w8;
            }
        }

        // 4. Activation, head accumulation and the 1x1 mix-out.
        for (int j = 0; j < kSampleGroup; ++j) {
            const int n = n0 + j;
            const float a0 = leaky(z[j][0]);
            const float a1 = leaky(z[j][1]);
            const float a2 = leaky(z[j][2]);
            float* hs = hot.headSum + n * 3;
            hs[0] += a0; hs[1] += a1; hs[2] += a2;

            const float* src = in + n * 3;
            float* dst = out + n * 3;
            dst[0] = src[0] + L.l1x1B[0] + a0 * lx[0] + a1 * lx[3] + a2 * lx[6];
            dst[1] = src[1] + L.l1x1B[1] + a0 * lx[1] + a1 * lx[4] + a2 * lx[7];
            dst[2] = src[2] + L.l1x1B[2] + a0 * lx[2] + a1 * lx[5] + a2 * lx[8];
        }

        wp += kSampleGroup;
        if (wp >= cols) wp -= cols;
    }
    st.layerWritePos[li] = wp;
}

inline void process_layer(State& st, HotState& hot, const SharedWeights& sw,
                          int li, const float* cond, const float* in, float* out) {
    if (li == 0 && kKernelSizes[0] == 6) {
        process_layer_impl<true>(st, hot, sw, li, cond, in, out);
    } else {
        process_layer_impl<false>(st, hot, sw, li, cond, in, out);
    }
}

inline void process_head(HotState& hot, const SharedWeights& sw, float* output) {
    int wp = hot.headWritePos;
    for (int n = 0; n < kBlockSize; ++n) {
        const float* hs = hot.headSum + n * 3;
        float* hcol = hot.headHistory + wp * 3;
        hcol[0] = hs[0]; hcol[1] = hs[1]; hcol[2] = hs[2];
        float y = sw.headB;
        for (int tap = 0; tap < kHeadKernel; ++tap) {
            const int col = (wp - (kHeadKernel - 1 - tap)) & (kHeadKernel - 1);
            const float* s = hot.headHistory + col * 3;
            y += sw.headW[tap][0] * s[0] + sw.headW[tap][1] * s[1] + sw.headW[tap][2] * s[2];
        }
        output[n] = y * sw.headScale;
        wp = (wp + 1) & (kHeadKernel - 1);
    }
    hot.headWritePos = wp;
}

inline void process_block_48(State& st, HotState& hot, const SharedWeights& sw,
                             const float* input, float* output) {
    if (!sw.loaded || !input || !output) {
        if (output) std::fill(output, output + kBlockSize, 0.0f);
        return;
    }
    float* in = hot.bufA;
    float* out = hot.bufB;
    for (int n = 0; n < kBlockSize; ++n) {
        const float x = input[n];
        float* dst = in + n * 3;
        dst[0] = sw.rechannel[0] * x;
        dst[1] = sw.rechannel[1] * x;
        dst[2] = sw.rechannel[2] * x;
        float* hs = hot.headSum + n * 3;
        hs[0] = hs[1] = hs[2] = 0.0f;
    }
    for (int li = 0; li < kNumLayers; ++li) {
        process_layer(st, hot, sw, li, input, in, out);
        float* tmp = in; in = out; out = tmp;
    }
    process_head(hot, sw, output);
}

class Player {
public:
    bool load_weights(const float* weights, size_t count) {
        const bool ok = load_weights_impl(weights, count);
        prewarm();
        return ok;
    }
    void process_block_48(const float* input, float* output) {
        nam_a2::process_block_48(state_, hot_, weights_, input, output);
    }
    bool is_loaded() const { return weights_.loaded; }
    void reset() { prewarm(); }

private:
    bool load_weights_impl(const float* weights, size_t count) {
        return nam_a2::load_weights(weights_, weights, count);
    }
    void prewarm() {
        reset_state(state_, hot_);
        float zeros[kBlockSize]{};
        float discard[kBlockSize]{};
        int remaining = prewarm_samples();
        while (remaining > 0) {
            nam_a2::process_block_48(state_, hot_, weights_, zeros, discard);
            remaining -= kBlockSize;
        }
    }

    // Declared here, defined exactly once in nam_processor.cpp. These must not
    // be `static inline`: a section attribute on a vague-linkage member defeats
    // per-variable COMDAT grouping, so each translation unit emits one plain
    // named section holding all of its .dtcmram_bss data. The linker then keeps
    // whichever copy it saw first and discards the rest, silently resolving
    // symbols that only existed in a discarded section to address 0.
    static SharedWeights weights_;
    static HotState hot_;
    State state_;
};

}  // namespace nam_a2
