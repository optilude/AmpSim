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
#include "model_data.h"

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

    if (NAM_MODEL_COUNT <= 0) {
        fprintf(stderr, "ERROR: model_data.h contains no NAM models.\n");
        return 1;
    }

    NAMProcessor proc;
    proc.setSampleRate(48000.0);

    if (!proc.loadModel(nam_models[0].model_json, nam_models[0].model_json_len)) {
        fprintf(stderr, "ERROR: loadModel returned false for generated model 0\n");
        return 1;
    }
    printf("[PASS] Generated model loaded: %s / %s\n",
           nam_models[0].model_name, nam_models[0].variant_name);
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
        if (!std::isfinite(out[i])) {
            fprintf(stderr, "[FAIL] model output was not finite\n");
            return 1;
        }
        if (out[i] < mn) mn = out[i];
        if (out[i] > mx) mx = out[i];
    }
    if (std::max(std::fabs(mn), std::fabs(mx)) > 5.0f) {
        fprintf(stderr, "[FAIL] generated model output is unexpectedly hot [%.3f, %.3f]\n", mn, mx);
        return 1;
    }
    printf("[PASS] Block processed, output range [%.3f, %.3f]\n", mn, mx);

    // All generated models should load and produce finite output.
    for (int modelIndex = 1; modelIndex < NAM_MODEL_COUNT; ++modelIndex) {
        if (!proc.loadModel(nam_models[modelIndex].model_json,
                            nam_models[modelIndex].model_json_len)) {
            fprintf(stderr, "[FAIL] generated model %d failed to load\n", modelIndex);
            return 1;
        }
        proc.process(in, out, kBlock);
        for (int i = 0; i < kBlock; ++i) {
            if (!std::isfinite(out[i]) || std::fabs(out[i]) > 5.0f) {
                fprintf(stderr, "[FAIL] generated model %d produced bad output %.3f\n",
                        modelIndex, out[i]);
                return 1;
            }
        }
    }
    printf("[PASS] All generated models load and produce finite bounded output\n");

    // A corrupt JSON load must fail before unloading the previous good model.
    if (proc.loadModel("{ definitely not nam json", 25)) {
        fprintf(stderr, "[FAIL] corrupt model unexpectedly loaded\n");
        return 1;
    }
    if (!proc.isModelLoaded()) {
        fprintf(stderr, "[FAIL] corrupt load cleared previous model\n");
        return 1;
    }
    printf("[PASS] Failed load preserves previous model\n");

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
