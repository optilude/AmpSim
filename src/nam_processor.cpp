#include "nam_processor.h"

#include <cmath>
#include <algorithm>

// The single definitions of Player's static members. Keeping them here rather
// than in the header is what makes the section placement actually stick; see
// the declarations in nam_a2_runtime.h.
NAM_A2_HOT_DATA nam_a2::SharedWeights nam_a2::Player::weights_;
NAM_A2_HOT_STATE_DATA nam_a2::HotState nam_a2::Player::hot_;

NAM_A2_STATE_DATA static nam_a2::Player s_a2Player;

NAMProcessor::NAMProcessor() = default;
NAMProcessor::~NAMProcessor() = default;

bool NAMProcessor::loadModel(const ModelEntry& model)
{
    if (model.type != ModelType::NamOnly && model.type != ModelType::NamAndIr) {
        return false;
    }
    
    if (model.nam_item_count != nam_a2::kA2WeightCount
        || model.nam_byte_count != nam_a2::kA2WeightCount * sizeof(float)) {
        return false;
    }

    const float* weights = reinterpret_cast<const float*>(model.nam_qspi_address);
    if (!s_a2Player.load_weights(weights, model.nam_item_count)) {
        return false;
    }

    modelLoaded = true;
    blockIndex_ = 0;
    std::fill(inputBlock_, inputBlock_ + nam_a2::kBlockSize, 0.0f);
    std::fill(outputBlock_, outputBlock_ + nam_a2::kBlockSize, 0.0f);
    hasLoudness_ = model.nam_has_loudness != 0;
    modelLoudness_ = model.nam_loudness_db;
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

    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = outputBlock_[blockIndex_];
        inputBlock_[blockIndex_] = input[i];
        ++blockIndex_;
        if (blockIndex_ >= nam_a2::kBlockSize) {
            s_a2Player.process_block_48(inputBlock_, outputBlock_);
            blockIndex_ = 0;
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

void NAMProcessor::reset()
{
    if (modelLoaded && s_a2Player.is_loaded()) {
        s_a2Player.reset();
    }
    std::fill(inputBlock_, inputBlock_ + nam_a2::kBlockSize, 0.0f);
    std::fill(outputBlock_, outputBlock_ + nam_a2::kBlockSize, 0.0f);
    blockIndex_ = 0;
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
