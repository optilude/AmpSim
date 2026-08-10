// Standalone desktop test for NAM A2 processing.
//
// Compile with test_integration.sh, or manually:
//   g++ -std=c++17 -DNAM_ENABLE_A2_FAST -DNAM_SHARED_PTR_ATOMIC_FREE_FUNCS \
//       -DNAM_SAMPLE_FLOAT -DNAM_USE_INLINE_GEMM \
//       -o test_nam test_nam.cpp src/nam_processor.cpp \
//       NeuralAmpModelerCore/NAM/*.cpp NeuralAmpModelerCore/NAM/wavenet/*.cpp \
//       -INeuralAmpModelerCore -INeuralAmpModelerCore/Dependencies/eigen \
//       -INeuralAmpModelerCore/Dependencies/nlohmann -Isrc

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "nam_processor.h"

static bool loadJson(const char* path, std::string& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream b;
    b << f.rdbuf();
    out = b.str();
    return true;
}

int main() {
    printf("NAM A2 desktop test\n");
    printf("===================\n\n");

    std::string modelJson;
    if (!loadJson("NeuralAmpModelerCore/example_models/wavenet_a2_max.nam", modelJson)) {
        fprintf(stderr, "ERROR: couldn't open example model. Run from repo root.\n");
        return 1;
    }

    NAMProcessor proc;
    proc.setSampleRate(48000.0);
    if (!proc.loadModel(modelJson.c_str(), modelJson.size())) {
        fprintf(stderr, "ERROR: loadModel returned false\n");
        return 1;
    }
    printf("[PASS] Model loaded\n");
    printf("       Loudness available: %s (%.2f dB)\n",
           proc.hasLoudness() ? "yes" : "no",
           proc.getModelLoudness());
    printf("       Output gain: %.3fx (%.2f dB)\n",
           proc.getOutputGain(), 20.0f * std::log10(proc.getOutputGain()));

    // 1kHz sine, one 48-sample block.
    constexpr int kBlock = 48;
    float in[kBlock], out[kBlock];
    for (int i = 0; i < kBlock; ++i) {
        in[i] = 0.1f * std::sin(2.0f * float(M_PI) * 1000.0f * i / 48000.0f);
    }

    proc.process(in, out, kBlock);
    float mn = out[0], mx = out[0];
    for (int i = 0; i < kBlock; ++i) {
        if (out[i] < mn) mn = out[i];
        if (out[i] > mx) mx = out[i];
    }
    printf("[PASS] Block processed, output range [%.3f, %.3f]\n", mn, mx);

    // Test bypass when no model loaded.
    NAMProcessor unloaded;
    float in2[kBlock], out2[kBlock];
    for (int i = 0; i < kBlock; ++i) in2[i] = 0.5f;
    unloaded.process(in2, out2, kBlock);
    for (int i = 0; i < kBlock; ++i) {
        if (std::fabs(out2[i] - 0.5f) > 1e-6f) {
            fprintf(stderr, "[FAIL] unloaded processor didn't pass through\n");
            return 1;
        }
    }
    printf("[PASS] Unloaded processor passes signal through\n");

    // Test loudness gain application.
    proc.setLoudnessTarget(std::nanf(""));  // disable
    if (std::fabs(proc.getOutputGain() - 1.0f) > 1e-6f) {
        fprintf(stderr, "[FAIL] setLoudnessTarget(nan) should give unity gain\n");
        return 1;
    }
    printf("[PASS] Loudness normalization can be disabled\n");

    proc.setLoudnessTarget(-18.0f);
    if (proc.hasLoudness() && proc.getOutputGain() == 1.0f) {
        fprintf(stderr, "[FAIL] setLoudnessTarget(-18) should give non-unity gain when model has loudness\n");
        return 1;
    }
    printf("[PASS] Loudness normalization can be re-enabled\n");

    printf("\nAll NAM tests passed.\n");
    return 0;
}
