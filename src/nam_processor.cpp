#include "nam_processor.h"
#include "nam_a2_runtime.h"

#include <cmath>
#include <algorithm>

NAM_A2_STATE_DATA static nam_a2::Player s_a2Player;

NAMProcessor::NAMProcessor() = default;
NAMProcessor::~NAMProcessor() = default;

bool NAMProcessor::loadCapture(const CaptureEntry& capture)
{
    if (capture.type != CaptureType::NamA2Lite
        || capture.item_count != nam_a2::kA2WeightCount
        || capture.byte_count != nam_a2::kA2WeightCount * sizeof(float)) {
        return false;
    }

    const float* weights = reinterpret_cast<const float*>(capture.qspi_address);
    if (!s_a2Player.load_weights(weights, capture.item_count)) {
        return false;
    }

    modelLoaded = true;
    hasLoudness_ = capture.has_loudness != 0;
    modelLoudness_ = capture.loudness_db;
    recomputeOutputGain();
    return true;
}

void NAMProcessor::process(float* input, float* output, size_t numSamples)
{
    if (!modelLoaded || !s_a2Player.is_loaded()) {
        if (input != output) {
            for (size_t i = 0; i < numSamples; ++i) output[i] = input[i];
        }
        return;
    }

    // The static A2 runtime is specialized for the Daisy audio block size.
    if (numSamples == nam_a2::kBlockSize) {
        s_a2Player.process_block_48(input, output);
    } else {
        // Should not happen in firmware; keep desktop/manual callers safe.
        for (size_t offset = 0; offset < numSamples; offset += nam_a2::kBlockSize) {
            float blockIn[nam_a2::kBlockSize]{};
            float blockOut[nam_a2::kBlockSize]{};
            const size_t n = std::min<size_t>(nam_a2::kBlockSize, numSamples - offset);
            std::copy(input + offset, input + offset + n, blockIn);
            s_a2Player.process_block_48(blockIn, blockOut);
            std::copy(blockOut, blockOut + n, output + offset);
        }
    }

    if (outputGain_ != 1.0f) {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] *= outputGain_;
        }
    }
}

void NAMProcessor::setSampleRate(double sr)
{
    sampleRate = sr;
    (void)sampleRate;
}

void NAMProcessor::setLoudnessTarget(float targetDb)
{
    loudnessTargetDb_ = targetDb;
    recomputeOutputGain();
}

void NAMProcessor::recomputeOutputGain()
{
    if (!hasLoudness_ || std::isnan(loudnessTargetDb_)) {
        outputGain_ = 1.0f;
        return;
    }
    // A negative model loudness (e.g., -16 LUFS) needs boosting to reach a
    // less negative target only if target > measured. To normalize a hot
    // model down we compute delta = target - measured; positive delta boosts,
    // negative delta cuts.
    const float deltaDb = loudnessTargetDb_ - modelLoudness_;
    outputGain_ = std::pow(10.0f, deltaDb / 20.0f);
    // Clamp to a reasonable range to guard against pathological metadata.
    if (outputGain_ < 0.01f) outputGain_ = 0.01f;    // -40 dB floor
    if (outputGain_ > 10.0f) outputGain_ = 10.0f;    // +20 dB ceiling
}
