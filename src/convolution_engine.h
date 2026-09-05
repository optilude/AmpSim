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

        ProcessDeferred(numPartitions_);
        ProcessHead(in, out);
        ProcessDeferred(numPartitions_);
    }

    // Produce the block that is due now using H[0]. Contributions from older
    // input blocks were accumulated into its slot by ProcessDeferred().
    bool ProcessHead(const float* in, float* out) {
        if (!prepared_ || numPartitions_ == 0) {
            std::memcpy(out, in, L * sizeof(float));
            return true;
        }

        // A caller must finish one block's deferred work before submitting the
        // next. Complete it here as a correctness fallback; IRProcessor's
        // callback scheduler normally keeps this path cold.
        const bool deferredReady = !HasDeferredWork();
        ProcessDeferred(numPartitions_);

        std::memcpy(inputBuf_, inputOverlap_, L * sizeof(float));
        std::memcpy(inputBuf_ + L, in, L * sizeof(float));
        std::memcpy(inputOverlap_, in, L * sizeof(float));

        arm_rfft_fast_f32(&fftInst_, inputBuf_, inputFreq_, 0);

        deferredBaseIndex_ = outputIndex_;
        MultiplyAccumulate(inputFreq_, irFreqAt(0), accumAt(outputIndex_));

        arm_rfft_fast_f32(&fftInst_, accumAt(outputIndex_), scratchA_, 1);
        std::memcpy(out, scratchA_ + L, L * sizeof(float));
        std::memset(accumAt(outputIndex_), 0, N * sizeof(float));

        outputIndex_ = (outputIndex_ + 1) % numPartitions_;
        deferredPartition_ = 1;
        return deferredReady;
    }

    // Push this input block's tail partitions into the frequency-domain
    // accumulators for future output blocks. Calling this in slices spreads
    // the O(IR length) work across audio callbacks.
    size_t ProcessDeferred(size_t maxPartitions) {
        if (!prepared_ || deferredPartition_ >= numPartitions_) return 0;

        const size_t begin = deferredPartition_;
        const size_t end = std::min(deferredPartition_ + maxPartitions, numPartitions_);
        for (; deferredPartition_ < end; ++deferredPartition_) {
            const size_t outputSlot = (deferredBaseIndex_ + deferredPartition_) % numPartitions_;
            MultiplyAccumulate(inputFreq_, irFreqAt(deferredPartition_), accumAt(outputSlot));
        }
        return deferredPartition_ - begin;
    }

    bool HasDeferredWork() const { return deferredPartition_ < numPartitions_; }

    // Rewind the running state, keeping the prepared IR spectrum.
    void Reset() {
        std::memset(inputOverlap_, 0, sizeof(inputOverlap_));
        if (fdl_) std::memset(fdl_, 0, maxPartitions_ * N * sizeof(float));
        outputIndex_ = 0;
        deferredBaseIndex_ = 0;
        deferredPartition_ = numPartitions_;
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
    float* accumAt(size_t partition) { return fdl_ + partition * N; }

    void MultiplyAccumulate(const float* inputFreq, const float* irFreq, float* accumulator) {
        arm_cmplx_mult_cmplx_f32(inputFreq, irFreq, scratchB_, N / 2);
        arm_add_f32(accumulator, scratchB_, accumulator, N);
    }

    arm_rfft_fast_instance_f32 fftInst_{};
    size_t maxPartitions_ = 0;
    size_t numPartitions_ = 0;
    size_t outputIndex_ = 0;
    size_t deferredBaseIndex_ = 0;
    size_t deferredPartition_ = 0;
    bool prepared_ = false;
    float inputOverlap_[L]{};

    // Scratch. Previously ~5 KB of stack arrays inside the audio ISR; as
    // members of the (global) IRProcessor they land in .bss, which this
    // linker script maps to DTCMRAM -- both faster and off the ISR stack.
    float inputBuf_[N]{};
    float inputFreq_[N]{};
    float scratchA_[N]{};
    float scratchB_[N]{};

    float* irFreq_ = nullptr;
    float* fdl_ = nullptr;
};
