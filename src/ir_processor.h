#pragma once

#include "capture_index.h"
#include "convolution_engine.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

class IRProcessor {
public:
    static constexpr size_t kMaxIrSamples = 4096;
    static constexpr size_t kMaxPartitions = kMaxIrSamples / ConvolutionEngine::L;

    void init(float* irFreqBuf, float* fdlBuf) {
        convolution_.Init(kMaxPartitions, irFreqBuf, fdlBuf);
    }

    bool loadCapture(const CaptureEntry& capture) {
        if (capture.type != CaptureType::CabinetIr || capture.item_count > kMaxIrSamples) {
            return false;
        }
        const float* ir = reinterpret_cast<const float*>(capture.qspi_address);
        convolution_.Prepare(ir, capture.item_count);
        loaded_ = true;
        return true;
    }

    void processBlock(const float* input, float* output, size_t n) {
        if (!loaded_) {
            if (input != output) std::copy(input, input + n, output);
            return;
        }
        convolution_.ProcessBlock(input, output, n);
    }

    bool isLoaded() const { return loaded_; }
    void clear() {
        convolution_.Reset();
    }

private:
    ConvolutionEngine convolution_;
    bool loaded_ = false;
};
