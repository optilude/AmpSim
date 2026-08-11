#include "nam_processor.h"
#include "NAM/get_dsp.h"
#include "NAM/dsp.h"

#include <cmath>
#include <sstream>

NAMProcessor::NAMProcessor() = default;
NAMProcessor::~NAMProcessor() = default;

bool NAMProcessor::loadModel(const char* modelJson, size_t jsonLength)
{
    try {
        nlohmann::json config = nlohmann::json::parse(modelJson, modelJson + jsonLength);

        // Free the previous model after JSON parsing so corrupt input does not
        // kill the current sound, but before tensor allocation so model changes
        // fit the Daisy SRAM heap.
        model.reset();
        modelLoaded = false;
        hasLoudness_ = false;
        modelLoudness_ = 0.0f;

        nam::dspData returnedConfig;
        std::unique_ptr<nam::DSP> newModel = nam::get_dsp(config, returnedConfig);

        if (!newModel) {
            recomputeOutputGain();
            return false;
        }

        // Match the max audio block size we'll ever pass. Anything larger
        // will force NAM to reallocate its input buffer (harmless but wastes
        // heap on the audio thread).
        newModel->Reset(sampleRate, 256);

        bool newHasLoudness = false;
        float newModelLoudness = 0.0f;

        if (newModel->HasLoudness()) {
            newHasLoudness = true;
            newModelLoudness = static_cast<float>(newModel->GetLoudness());
        }

        model = std::move(newModel);
        hasLoudness_ = newHasLoudness;
        modelLoudness_ = newModelLoudness;
        recomputeOutputGain();

        modelLoaded = true;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void NAMProcessor::process(float* input, float* output, size_t numSamples)
{
    if (!modelLoaded || !model) {
        if (input != output) {
            for (size_t i = 0; i < numSamples; ++i) output[i] = input[i];
        }
        return;
    }

    NAM_SAMPLE* inputArrays[1]  = { reinterpret_cast<NAM_SAMPLE*>(input) };
    NAM_SAMPLE* outputArrays[1] = { reinterpret_cast<NAM_SAMPLE*>(output) };

    model->process(inputArrays, outputArrays, static_cast<int>(numSamples));

    if (outputGain_ != 1.0f) {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] *= outputGain_;
        }
    }
}

void NAMProcessor::setSampleRate(double sr)
{
    sampleRate = sr;
    // NAM applies the sample rate on the next Reset() (called from loadModel).
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
