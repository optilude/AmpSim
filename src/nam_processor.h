#pragma once

#include <memory>
#include <string>

namespace nam
{
class DSP;
}

class NAMProcessor
{
public:
    NAMProcessor();
    ~NAMProcessor();
    
    bool loadModel(const std::string& modelJson);
    void process(float* input, float* output, size_t numSamples);
    void setSampleRate(double sampleRate);
    
    bool isModelLoaded() const { return modelLoaded; }
    
private:
    std::unique_ptr<nam::DSP> model;
    bool modelLoaded;
    double sampleRate;
};
