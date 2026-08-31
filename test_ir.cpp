// Desktop test for QSPI-backed cabinet IR processing.
// Uses the same capture blob/index and IRProcessor path as firmware.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "capture_index.h"
#include "ir_processor.h"

static int fail(const char* msg) { fprintf(stderr, "[FAIL] %s\n", msg); return 1; }

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

static bool writeWavMono16(const char* path, const std::vector<float>& samples) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint32_t rate = 48000;
    const uint16_t blockAlign = channels * bits / 8;
    const uint32_t byteRate = rate * blockAlign;
    const uint32_t dataBytes = samples.size() * blockAlign;
    const uint32_t riffBytes = 36 + dataBytes;

    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&riffBytes), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    const uint32_t fmtBytes = 16;
    const uint16_t format = 1;
    f.write(reinterpret_cast<const char*>(&fmtBytes), 4);
    f.write(reinterpret_cast<const char*>(&format), 2);
    f.write(reinterpret_cast<const char*>(&channels), 2);
    f.write(reinterpret_cast<const char*>(&rate), 4);
    f.write(reinterpret_cast<const char*>(&byteRate), 4);
    f.write(reinterpret_cast<const char*>(&blockAlign), 2);
    f.write(reinterpret_cast<const char*>(&bits), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&dataBytes), 4);

    for (float sample : samples) {
        const float clipped = std::max(-1.0f, std::min(1.0f, sample));
        const int16_t q = static_cast<int16_t>(std::lrintf(clipped * 32767.0f));
        f.write(reinterpret_cast<const char*>(&q), 2);
    }
    return true;
}

int main() {
    printf("IR desktop test\n");
    printf("===============\n\n");

    std::vector<uint8_t> captureBlob;
    if (!loadBinary("build/capture_data.bin", captureBlob)) {
        return fail("build/capture_data.bin missing; run capture conversion first");
    }

    std::vector<float> di;
    if (!loadWavMono("testing/di-stratocaster.wav", di)) {
        return fail("testing/di-stratocaster.wav missing or unsupported (need 48kHz PCM)");
    }

    std::vector<ModelEntry> entries(model_entries, model_entries + MODEL_COUNT);
    for (ModelEntry& entry : entries) {
        if (entry.ir_byte_count > 0) {
            const uintptr_t offset = entry.ir_qspi_address - CAPTURE_DATA_QSPI_BASE;
            if (offset + entry.ir_byte_count > captureBlob.size()) return fail("capture entry outside blob");
            entry.ir_qspi_address = reinterpret_cast<uintptr_t>(captureBlob.data() + offset);
        }
    }

    const ModelEntry* irEntry = nullptr;
    for (const ModelEntry& entry : entries) {
        if (entry.type == ModelType::IrOnly) {
            irEntry = &entry;
            break;
        }
    }
    if (!irEntry) return fail("no IrOnly entry in capture index");

    std::vector<float> irFreq(IRProcessor::kMaxPartitions * ConvolutionEngine::N);
    std::vector<float> fdl(IRProcessor::kMaxPartitions * ConvolutionEngine::N);
    IRProcessor ir;
    ir.init(irFreq.data(), fdl.data());
    if (!ir.loadModel(*irEntry)) return fail("IR loadModel failed");

    float in[ConvolutionEngine::L]{};
    float out[ConvolutionEngine::L]{};
    std::vector<float> rendered;
    rendered.reserve(di.size() + ConvolutionEngine::L);
    float peak = 0.0f;
    double energy = 0.0;
    for (size_t pos = 0; pos < di.size(); pos += ConvolutionEngine::L) {
        for (size_t i = 0; i < ConvolutionEngine::L; ++i) {
            in[i] = (pos + i < di.size()) ? di[pos + i] : 0.0f;
        }
        ir.processBlock(in, out, ConvolutionEngine::L);
        for (float sample : out) {
            if (!std::isfinite(sample)) return fail("IR output was not finite");
            peak = std::max(peak, std::fabs(sample));
            energy += double(sample) * sample;
            rendered.push_back(sample);
        }
    }

    if (peak < 1e-5f) return fail("IR output was silent");
    if (peak > 10.0f) return fail("IR output peak unexpectedly high");
    writeWavMono16("build/ir-render.wav", rendered);
    printf("[PASS] IR render is finite and bounded (peak %.3f, rms %.4f)\n", peak, std::sqrt(energy / di.size()));
    printf("[INFO] Wrote build/ir-render.wav for listening\n");
    printf("\nAll IR tests passed.\n");
    return 0;
}
