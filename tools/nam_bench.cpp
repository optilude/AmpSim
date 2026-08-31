// Host harness for the NAM A2 runtime: correctness and relative speed.
//
// The runtime is the pedal's single most expensive stage (measured at 77% of
// the audio block on hardware, against 29% for the reverb -- the two together
// overrun). Optimising it on the pedal means a flash cycle per experiment and
// a two-digit number on an OLED as the only feedback. This runs the exact same
// header on the host instead.
//
// Two jobs:
//
//   --check   Run real weights from build/capture_data.bin through N blocks of
//             a deterministic signal and print a checksum of the output. An
//             optimisation that only reassociates loads and hoists invariants
//             must not move a single bit, so the checksum is an equality test,
//             not a tolerance test. Compare against a saved baseline.
//
//   --bench   Time the same run. Host timings do not predict Cortex-M7 timings
//             -- no 16 KB D-cache pressure, different issue width -- so treat
//             the ratio between two variants as a direction, not a number.
//
// Build:  make -f tools/nam_bench.mk

#include "../src/nam_a2_runtime.h"

#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// Player's statics are declared in the header and defined exactly once in
// nam_processor.cpp -- see the comment there. This harness does not build that
// file (it drags in the ModelEntry/QSPI world), so it supplies them itself.
nam_a2::SharedWeights nam_a2::Player::weights_;
nam_a2::HotState nam_a2::Player::hot_;

namespace {

// First NamOnly entry in src/capture_index.h, as an offset into
// build/capture_data.bin (entry address minus CAPTURE_DATA_QSPI_BASE).
constexpr long kWeightsOffset = 0x8000;
constexpr size_t kWeightCount = nam_a2::kA2WeightCount;

bool LoadWeights(const char* path, std::vector<float>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    out.resize(kWeightCount);
    bool ok = std::fseek(f, kWeightsOffset, SEEK_SET) == 0
              && std::fread(out.data(), sizeof(float), kWeightCount, f) == kWeightCount;
    std::fclose(f);
    if (!ok) std::fprintf(stderr, "short read from %s\n", path);
    return ok;
}

// A deterministic, broadband, guitar-ish input: a swept sine driven hard
// enough to exercise both sides of the leaky ReLU. No RNG, so the signal is
// identical across machines and compilers.
void FillInput(float* dst, int n, int blockIndex) {
    for (int i = 0; i < n; ++i) {
        const double t = (blockIndex * nam_a2::kBlockSize + i) / 48000.0;
        const double f = 80.0 + 3000.0 * t;
        dst[i] = (float)(0.7 * std::sin(6.283185307179586 * f * t)
                         + 0.2 * std::sin(6.283185307179586 * 3.3 * f * t));
    }
}

// Bitwise-sensitive checksum. FNV-1a over the raw sample bits, so any change
// in the last mantissa bit shows up. Zeros are canonicalised because -0.0f and
// 0.0f compare equal but hash differently.
uint64_t Checksum(const float* v, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        float x = v[i];
        if (x == 0.0f) x = 0.0f;
        uint32_t bits;
        std::memcpy(&bits, &x, 4);
        for (int b = 0; b < 4; ++b) {
            h ^= (bits >> (b * 8)) & 0xff;
            h *= 1099511628211ull;
        }
    }
    return h;
}

}  // namespace

int main(int argc, char** argv) {
    const char* blob = "build/capture_data.bin";
    int blocks = 2000;
    bool bench = false, check = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--bench") bench = true;
        else if (a == "--check") check = true;
        else if (a == "--blocks" && i + 1 < argc) blocks = std::atoi(argv[++i]);
        else if (a == "--blob" && i + 1 < argc) blob = argv[++i];
        else { std::fprintf(stderr, "usage: %s [--check] [--bench] [--blocks N] [--blob PATH]\n", argv[0]); return 2; }
    }
    if (!bench && !check) { check = bench = true; }

    std::vector<float> weights;
    if (!LoadWeights(blob, weights)) return 1;

    static nam_a2::Player player;
    if (!player.load_weights(weights.data(), weights.size())) {
        std::fprintf(stderr, "load_weights rejected the blob\n");
        return 1;
    }

    std::vector<float> in(nam_a2::kBlockSize), out(nam_a2::kBlockSize);
    std::vector<float> collected;
    collected.reserve((size_t)blocks * nam_a2::kBlockSize);

    // Precompute the input so signal generation is not inside the timed loop,
    // and take the best of several passes: on a laptop the first pass pays for
    // page faults and frequency ramp, and the spread between passes was larger
    // than the effect being measured.
    std::vector<std::vector<float>> inputs((size_t)blocks,
                                           std::vector<float>(nam_a2::kBlockSize));
    for (int b = 0; b < blocks; ++b) FillInput(inputs[b].data(), nam_a2::kBlockSize, b);

    // Only the first pass starts from the freshly prewarmed model; later
    // passes inherit the previous one's recurrent state. Snapshot the first so
    // the checksum means "this model from a clean start", not "after six
    // passes of whatever".
    uint64_t sum = 0;
    double peak = 0.0;
    bool finite = true;

    constexpr int kPasses = 7;
    double secs = 1e30;
    for (int pass = 0; pass < kPasses; ++pass) {
        collected.clear();
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            player.process_block_48(inputs[b].data(), out.data());
            collected.insert(collected.end(), out.begin(), out.end());
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double d = std::chrono::duration<double>(t1 - t0).count();
        if (d < secs) secs = d;

        if (pass == 0) {
            sum = Checksum(collected.data(), collected.size());
            for (float v : collected) {
                if (!std::isfinite(v)) finite = false;
                else if (std::fabs(v) > peak) peak = std::fabs(v);
            }
        }
    }
    const double audioSecs = (double)blocks * nam_a2::kBlockSize / 48000.0;

    if (check) {
        std::printf("checksum %016" PRIx64 "  blocks %d  peak %.9g  finite %s\n",
                    sum, blocks, peak, finite ? "yes" : "NO");
    }
    if (bench) {
        std::printf("time %.4f s for %.2f s of audio  (%.1fx realtime, %.3f us/block)\n",
                    secs, audioSecs, audioSecs / secs, secs * 1e6 / blocks);
    }
    return 0;
}
