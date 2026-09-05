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
#include <vector>

#include "nam_processor.h"
#include "capture_index.h"

static int fail(const char* msg) { fprintf(stderr, "[FAIL] %s\n", msg); return 1; }

static bool loadJson(const char* path, std::string& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream b;
    b << f.rdbuf();
    out = b.str();
    return true;
}

static bool loadBinary(const char* path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()), size);
    return true;
}

static bool loadWavMono(const char* path, std::vector<float>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    char riff[4];
    f.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) return false;
    f.ignore(4);
    char wave[4];
    f.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) return false;
    uint16_t channels = 0, bits = 0, format = 0;
    uint32_t rate = 0;
    std::vector<char> data;
    while (f && (!channels || data.empty())) {
        char id[4];
        uint32_t size = 0;
        f.read(id, 4);
        f.read(reinterpret_cast<char*>(&size), 4);
        if (!f) break;
        if (std::strncmp(id, "fmt ", 4) == 0) {
            f.read(reinterpret_cast<char*>(&format), 2);
            f.read(reinterpret_cast<char*>(&channels), 2);
            f.read(reinterpret_cast<char*>(&rate), 4);
            f.ignore(6);
            f.read(reinterpret_cast<char*>(&bits), 2);
            if (size > 16) f.ignore(size - 16);
        } else if (std::strncmp(id, "data", 4) == 0) {
            data.resize(size);
            f.read(data.data(), size);
        } else {
            f.ignore(size);
        }
    }
    if (format != 1 || rate != 48000 || channels == 0 || (bits != 16 && bits != 24) || data.empty()) return false;
    const int bytesPerSample = bits / 8;
    const int frames = data.size() / (channels * bytesPerSample);
    out.resize(frames);
    if (bits == 16) {
        const int16_t* samples = reinterpret_cast<const int16_t*>(data.data());
        for (int i = 0; i < frames; ++i) out[i] = samples[i * channels] / 32768.0f;
    } else {
        for (int i = 0; i < frames; ++i) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data()) + i * channels * 3;
            int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
            if (v & 0x800000) v |= 0xff000000;
            out[i] = float(v) / 8388608.0f;
        }
    }
    return true;
}

int main() {
    printf("NAM A2 desktop test\n");
    printf("===================\n\n");

    if (MODEL_COUNT <= 0) {
        fprintf(stderr, "ERROR: capture_index.h contains no models.\n");
        return 1;
    }

    std::vector<uint8_t> captureBlob;
    if (!loadBinary("build/capture_data.bin", captureBlob)) {
        fprintf(stderr, "ERROR: build/capture_data.bin missing. Run build_capture_blob.py first.\n");
        return 1;
    }
    std::vector<ModelEntry> desktopEntries(model_entries, model_entries + MODEL_COUNT);
    for (ModelEntry& entry : desktopEntries) {
        if (entry.nam_byte_count > 0) {
            const uintptr_t offset = entry.nam_qspi_address - CAPTURE_DATA_QSPI_BASE;
            if (offset + entry.nam_byte_count > captureBlob.size()) {
                fprintf(stderr, "ERROR: capture entry outside capture_data.bin\n");
                return 1;
            }
            entry.nam_qspi_address = reinterpret_cast<uintptr_t>(captureBlob.data() + offset);
        }
    }

    NAMProcessor proc;
    proc.setSampleRate(48000.0);
    
    // Find the first model with a NAM payload to test.
    const ModelEntry* firstNam = nullptr;
    for (const ModelEntry& entry : desktopEntries) {
        if (entry.nam_byte_count > 0) {
            firstNam = &entry;
            break;
        }
    }
    
    if (!firstNam) {
        fprintf(stderr, "ERROR: no NAM models in capture index\n");
        return 1;
    }

    if (!proc.loadModel(*firstNam)) {
        fprintf(stderr, "ERROR: loadModel returned false for generated model 0\n");
        return 1;
    }
    printf("[PASS] Generated model loaded: %s / %s\n",
           firstNam->model_name, firstNam->variant_name);
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

    // All generated NAM captures, including paired NAM+IR entries, should
    // load and produce finite output.
    bool sawCombined = false;
    for (int modelIndex = 0; modelIndex < MODEL_COUNT; ++modelIndex) {
        if (desktopEntries[modelIndex].type == ModelType::IrOnly) continue;
        if (desktopEntries[modelIndex].type == ModelType::NamAndIr) sawCombined = true;
        if (!proc.loadModel(desktopEntries[modelIndex])) {
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
    if (!sawCombined) {
        fprintf(stderr, "[FAIL] no combined NAM+IR model in capture index\n");
        return 1;
    }
    printf("[PASS] Combined NAM+IR metadata loads in the NAM processor\n");

    bool sawIr = false;
    for (int i = 0; i < MODEL_COUNT; ++i) {
        if (desktopEntries[i].type == ModelType::IrOnly) {
            sawIr = true;
            // The IR path might point to 0 in QSPI flash since we mocked the address rewriting
            // for test purposes. But let's just make sure it's valid if there is an IR byte count.
            if (desktopEntries[i].ir_byte_count > 0) {
                 // Skip test checks here, handled in test_ir
            }
        }
    }
    if (sawIr) printf("[PASS] IR capture metadata exists\n");

    std::vector<float> di;
    if (loadWavMono("testing/di-stratocaster.wav", di)) {
        proc.loadModel(*firstNam);
        float blockIn[48], blockOut[48];
        float peak = 0.0f;
        for (size_t pos = 0; pos < di.size(); pos += 48) {
            for (int i = 0; i < 48; ++i) blockIn[i] = (pos + i < di.size()) ? di[pos + i] : 0.0f;
            proc.process(blockIn, blockOut, 48);
            for (float sample : blockOut) {
                if (!std::isfinite(sample)) return fail("DI render produced non-finite output");
                peak = std::max(peak, std::fabs(sample));
            }
        }
        if (peak < 1e-5f || peak > 5.0f) return fail("DI render peak outside expected bounds");
        printf("[PASS] DI guitar render is finite and bounded (peak %.3f)\n", peak);
    } else {
        printf("[SKIP] testing/di-stratocaster.wav not found or not 48kHz/16-bit PCM\n");
    }

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
