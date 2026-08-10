#include "nam_processor.h"
#include "NAM/get_dsp.h"
#include "NAM/dsp.h"
#include <sstream>

NAMProcessor::NAMProcessor() 
    : model(nullptr), modelLoaded(false), sampleRate(48000.0)
{
}

NAMProcessor::~NAMProcessor() = default;

bool NAMProcessor::loadModel(const std::string& modelJson)
{
    try {
        model.reset();
        modelLoaded = false;
        
        // Parse JSON string
        nlohmann::json config = nlohmann::json::parse(modelJson);
        
        // Get DSP model
        nam::dspData returnedConfig;
        model = nam::get_dsp(config, returnedConfig);
        
        if (model) {
            // Initialize model with sample rate and max buffer size
            // Max buffer size of 256 is typical for real-time audio
            model->Reset(sampleRate, 256);
            modelLoaded = true;
            return true;
        }
    } catch (const std::exception& e) {
        model = nullptr;
        modelLoaded = false;
    }
    return false;
}

void NAMProcessor::process(float* input, float* output, size_t numSamples)
{
    if (!modelLoaded || !model) {
        for (size_t i = 0; i < numSamples; i++) {
            output[i] = input[i];
        }
        return;
    }
    
    // NAM expects arrays of pointers
    NAM_SAMPLE* inPtr = reinterpret_cast<NAM_SAMPLE*>(input);
    NAM_SAMPLE* outPtr = reinterpret_cast<NAM_SAMPLE*>(output);
    NAM_SAMPLE* inputArrays[1] = { inPtr };
    NAM_SAMPLE* outputArrays[1] = { outPtr };
    
    model->process(inputArrays, outputArrays, static_cast<int>(numSamples));
}

void NAMProcessor::setSampleRate(double sr)
{
    sampleRate = sr;
    // NAM doesn't need explicit sample rate setting after initialization
}
