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

inline constexpr int kLayerCols[kNumLayers] = {
    7, 17, 37, 87, 207, 507, 1197, 7, 17, 37, 87, 207, 507, 1197,
    16, 184, 7, 17, 37, 87, 207, 507, 1197
};

inline constexpr int kLayerHistoryOffset[kNumLayers] = {
    0, 21, 72, 183, 444, 1065, 2586, 6177, 6198, 6249,
    6360, 6621, 7242, 8763, 12354, 12402, 12954, 12975, 13026, 13137,
    13398, 14019, 15540
};

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
static constexpr int kHistoryFloats = 19131;

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
    for (int i = 0; i < kNumLayers; ++i) st.layerWritePos[i] = kLayerCols[i] - 1;
    hot.headWritePos = kHeadKernel - 1;
}

inline constexpr int prewarm_samples() {
    int total = kHeadKernel - 1;
    for (int i = 0; i < kNumLayers; ++i) total += (kKernelSizes[i] - 1) * kDilations[i];
    return total;
}

inline void acc_tap3(const float* w, const float* s, float& z0, float& z1, float& z2) {
    const float s0 = s[0];
    const float s1 = s[1];
    const float s2 = s[2];
    z0 += s0 * w[0] + s1 * w[3] + s2 * w[6];
    z1 += s0 * w[1] + s1 * w[4] + s2 * w[7];
    z2 += s0 * w[2] + s1 * w[5] + s2 * w[8];
}

inline void process_layer(State& st, HotState& hot, const SharedWeights& sw,
                          int li, const float* cond, const float* in, float* out) {
    const LayerRuntime& L = sw.layer[li];
    int wp = st.layerWritePos[li];
    const int cols = kLayerCols[li];
    const int dilation = kDilations[li];
    const int kernelSize = kKernelSizes[li];
    float* const hist = st.history + kLayerHistoryOffset[li];
    const float* const wAll = sw.convW + kLayerConvOffset[li];
    const float* const lx = sw.l1x1W + kLayerL1Offset[li];
    const bool precombineCurrent = (li == 0 && kernelSize == 6);

    for (int n = 0; n < kBlockSize; ++n) {
        const float* src = in + n * 3;
        float* histCol = hist + wp * 3;
        const float x0 = src[0];
        const float x1 = src[1];
        const float x2 = src[2];
        histCol[0] = x0; histCol[1] = x1; histCol[2] = x2;

        const float c = cond[n];
        float z0 = L.convB[0] + (precombineCurrent ? L.preCurrent[0] : L.mixinW[0]) * c;
        float z1 = L.convB[1] + (precombineCurrent ? L.preCurrent[1] : L.mixinW[1]) * c;
        float z2 = L.convB[2] + (precombineCurrent ? L.preCurrent[2] : L.mixinW[2]) * c;

        for (int k = 0; k < kernelSize; ++k) {
            if (precombineCurrent && k == kernelSize - 1) continue;
            int col = wp - (kernelSize - 1 - k) * dilation;
            while (col < 0) col += cols;
            acc_tap3(wAll + k * 9, hist + col * 3, z0, z1, z2);
        }

        const float a0 = leaky(z0);
        const float a1 = leaky(z1);
        const float a2 = leaky(z2);
        float* hs = hot.headSum + n * 3;
        hs[0] += a0; hs[1] += a1; hs[2] += a2;

        float* dst = out + n * 3;
        dst[0] = x0 + L.l1x1B[0] + a0 * lx[0] + a1 * lx[3] + a2 * lx[6];
        dst[1] = x1 + L.l1x1B[1] + a0 * lx[1] + a1 * lx[4] + a2 * lx[7];
        dst[2] = x2 + L.l1x1B[2] + a0 * lx[2] + a1 * lx[5] + a2 * lx[8];

        if (++wp >= cols) wp = 0;
    }
    st.layerWritePos[li] = wp;
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
