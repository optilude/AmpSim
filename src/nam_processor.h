#pragma once

#include <memory>
#include <cstddef>

namespace nam
{
class DSP;
}

// Thin wrapper around a NAM (Neural Amp Modeler) DSP model.
//
// A .nam capture is fed in as a raw JSON string. NAM allocates its
// weight tensors on the heap during loadModel(); previous state is
// released before parsing to keep peak heap usage bounded.
//
// The model exposes a "loudness" figure (LUFS) via the capture
// metadata. If enabled, an output post-gain is applied to normalize
// disparate models to a common perceived level (see setLoudnessTarget).
class NAMProcessor
{
public:
    NAMProcessor();
    ~NAMProcessor();

    // Load a model from a JSON string. Accepts C-string for zero-copy
    // (the pointer is only borrowed for the duration of parsing).
    // Returns false and leaves the previous model unloaded on failure.
    bool loadModel(const char* modelJson, size_t jsonLength);

    // Process a block of samples. NAM is designed for block processing;
    // callers should pass entire audio blocks (e.g. 48 samples), NOT
    // single samples in a loop.
    void process(float* input, float* output, size_t numSamples);

    void setSampleRate(double sampleRate);

    // Set the target LUFS for loudness normalization. When the loaded
    // model advertises a loudness value in its metadata, an output post-
    // gain of 10^((target - measured)/20) is applied. Pass a NaN to
    // disable normalization. Default: -18 dBFS RMS-ish (-18.0).
    void setLoudnessTarget(float targetDb);

    bool isModelLoaded() const { return modelLoaded; }
    bool hasLoudness() const { return hasLoudness_; }
    float getModelLoudness() const { return modelLoudness_; }
    float getOutputGain() const { return outputGain_; }

private:
    void recomputeOutputGain();

    std::unique_ptr<nam::DSP> model;
    bool modelLoaded = false;
    double sampleRate = 48000.0;

    bool hasLoudness_ = false;
    float modelLoudness_ = 0.0f;
    float loudnessTargetDb_ = -18.0f;
    float outputGain_ = 1.0f;
};
