#pragma once

#include "capture_index.h"
#include "convolution_engine.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

class IRProcessor {
public:
    static constexpr size_t kMaxIrSamples = 4096;
    static constexpr size_t kConvBlock = ConvolutionEngine::L;
    static constexpr size_t kMaxPartitions = kMaxIrSamples / kConvBlock;
    static constexpr size_t kDeferredPartitionsPerCallback = 12;

    void init(float* irFreqBuf, float* fdlBuf) {
        convolution_.Init(kMaxPartitions, irFreqBuf, fdlBuf);
    }

    bool loadModel(const ModelEntry& model) {
        if ((model.type != ModelType::IrOnly && model.type != ModelType::NamAndIr)
            || model.ir_item_count > kMaxIrSamples) {
            return false;
        }
        const float* ir = reinterpret_cast<const float*>(model.ir_qspi_address);
        convolution_.Prepare(ir, model.ir_item_count);
        resetState();
        loaded_ = true;
        return true;
    }

    // The convolution needs a power-of-two partition to FFT, so it runs at
    // ConvolutionEngine::L samples regardless of the audio block size. Buffer
    // into it the same way NAMProcessor buffers into its 48-sample inference,
    // reading back the previous partition's output. Costs L samples of latency
    // when the audio block is smaller than L; when they are equal this is a
    // straight pass-through with no added delay.
    void processBlock(const float* input, float* output, size_t n) {
        if (!loaded_) {
            if (input != output) std::copy(input, input + n, output);
            return;
        }
        if (n == kConvBlock) {
            convolution_.ProcessBlock(input, output, n);
            return;
        }

        size_t deferredBudget = kDeferredPartitionsPerCallback;
        deferredBudget -= convolution_.ProcessDeferred(deferredBudget);

        bool startedBlock = false;
        for (size_t i = 0; i < n; ++i) {
            output[i] = outBlock_[blockIndex_];
            inBlock_[blockIndex_] = input[i];
            if (++blockIndex_ >= kConvBlock) {
                if (!convolution_.ProcessHead(inBlock_, outBlock_)) deferredScheduleMissed_ = true;
                blockIndex_ = 0;
                startedBlock = true;
            }
        }

        if (startedBlock && deferredBudget > 0)
            convolution_.ProcessDeferred(deferredBudget);
    }

    bool isLoaded() const { return loaded_; }
    bool deferredScheduleMissed() const { return deferredScheduleMissed_; }

    // Rewind the overlap/FDL state but keep the loaded IR. This is what
    // re-enabling the model engine wants.
    void resetState() {
        convolution_.Reset();
        std::fill(inBlock_, inBlock_ + kConvBlock, 0.0f);
        std::fill(outBlock_, outBlock_ + kConvBlock, 0.0f);
        blockIndex_ = 0;
        deferredScheduleMissed_ = false;
    }

    // Unload entirely. Used before loading a different model, and as part of
    // recovering from a non-finite sample -- clear() must drop the IR spectrum
    // and the loaded_ flag together, or processBlock keeps convolving.
    void clear() {
        convolution_.Clear();
        resetState();
        loaded_ = false;
    }

private:
    ConvolutionEngine convolution_;
    float inBlock_[kConvBlock]{};
    float outBlock_[kConvBlock]{};
    size_t blockIndex_ = 0;
    bool loaded_ = false;
    bool deferredScheduleMissed_ = false;
};
