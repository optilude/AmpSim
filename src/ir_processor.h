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

    bool loadModel(const ModelEntry& model) {
        if ((model.type != ModelType::IrOnly && model.type != ModelType::NamAndIr) || model.ir_item_count > kMaxIrSamples) {
            return false;
        }
        const float* ir = reinterpret_cast<const float*>(model.ir_qspi_address);
        convolution_.Prepare(ir, model.ir_item_count);
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

    // Rewind the overlap/FDL state but keep the loaded IR. This is what
    // re-enabling the model engine wants.
    void resetState() {
        convolution_.Reset();
    }

    // Unload entirely. Used before loading a different model, and as part of
    // recovering from a non-finite sample -- clear() must drop the IR spectrum
    // and the loaded_ flag together, or processBlock keeps convolving.
    void clear() {
        convolution_.Clear();
        loaded_ = false;
    }

private:
    ConvolutionEngine convolution_;
    bool loaded_ = false;
};
