// Partitioned overlap-save convolution engine adapted from MuleBox.
// Uses CMSIS-DSP FFTs and external buffers for long cabinet IRs.

#pragma once

#include <arm_math.h>
#include <cstring>

static constexpr size_t CONV_PARTITION_SIZE = 128;
static constexpr size_t CONV_FFT_SIZE = 2 * CONV_PARTITION_SIZE;

class ConvolutionEngine {
public:
    static constexpr size_t L = CONV_PARTITION_SIZE;
    static constexpr size_t N = CONV_FFT_SIZE;

    void Init(size_t maxPartitions, float* irFreqBuf, float* fdlBuf) {
        maxPartitions_ = maxPartitions;
        irFreq_ = irFreqBuf;
        fdl_ = fdlBuf;
        arm_rfft_fast_init_f32(&fftInst_, N);
        Reset();
        numPartitions_ = 0;
        prepared_ = false;
    }

    void Prepare(const float* ir, size_t irLength) {
        prepared_ = false;
        numPartitions_ = (irLength + L - 1) / L;
        if (numPartitions_ > maxPartitions_) numPartitions_ = maxPartitions_;

        for (size_t p = 0; p < numPartitions_; ++p) {
            float padded[N];
            std::memset(padded, 0, sizeof(padded));
            const size_t offset = p * L;
            size_t count = irLength - offset;
            if (count > L) count = L;
            std::memcpy(padded, ir + offset, count * sizeof(float));
            arm_rfft_fast_f32(&fftInst_, padded, irFreqAt(p), 0);
        }

        Reset();
        prepared_ = true;
    }

    void ProcessBlock(const float* in, float* out, size_t blockSize) {
        if (!prepared_ || numPartitions_ == 0 || blockSize != L) {
            std::memcpy(out, in, blockSize * sizeof(float));
            return;
        }

        std::memcpy(inputBuf_, inputOverlap_, L * sizeof(float));
        std::memcpy(inputBuf_ + L, in, L * sizeof(float));
        std::memcpy(inputOverlap_, in, L * sizeof(float));

        arm_rfft_fast_f32(&fftInst_, inputBuf_, scratchA_, 0);
        std::memcpy(fdlAt(fdlIndex_), scratchA_, N * sizeof(float));

        std::memset(accumFreq_, 0, sizeof(accumFreq_));
        for (size_t p = 0; p < numPartitions_; ++p) {
            const size_t fdlIdx = (fdlIndex_ + numPartitions_ - p) % numPartitions_;
            arm_cmplx_mult_cmplx_f32(fdlAt(fdlIdx), irFreqAt(p), scratchB_, N / 2);
            arm_add_f32(accumFreq_, scratchB_, accumFreq_, N);
        }

        arm_rfft_fast_f32(&fftInst_, accumFreq_, scratchA_, 1);
        std::memcpy(out, scratchA_ + L, L * sizeof(float));

        fdlIndex_ = (fdlIndex_ + 1) % numPartitions_;
    }

    // Rewind the running state, keeping the prepared IR spectrum.
    void Reset() {
        std::memset(inputOverlap_, 0, sizeof(inputOverlap_));
        if (fdl_) std::memset(fdl_, 0, maxPartitions_ * N * sizeof(float));
        fdlIndex_ = 0;
    }

    // Drop the prepared IR as well. Reset() alone leaves irFreq_ intact, so a
    // spectrum that picked up a NaN would survive and re-poison every block.
    void Clear() {
        Reset();
        if (irFreq_) std::memset(irFreq_, 0, maxPartitions_ * N * sizeof(float));
        numPartitions_ = 0;
        prepared_ = false;
    }

private:
    float* irFreqAt(size_t partition) { return irFreq_ + partition * N; }
    float* fdlAt(size_t partition) { return fdl_ + partition * N; }

    arm_rfft_fast_instance_f32 fftInst_{};
    size_t maxPartitions_ = 0;
    size_t numPartitions_ = 0;
    size_t fdlIndex_ = 0;
    bool prepared_ = false;
    float inputOverlap_[L]{};

    // Scratch. Previously ~5 KB of stack arrays inside the audio ISR; as
    // members of the (global) IRProcessor they land in .bss, which this
    // linker script maps to DTCMRAM -- both faster and off the ISR stack.
    float inputBuf_[N]{};
    float accumFreq_[N]{};
    float scratchA_[N]{};
    float scratchB_[N]{};

    float* irFreq_ = nullptr;
    float* fdl_ = nullptr;
};
