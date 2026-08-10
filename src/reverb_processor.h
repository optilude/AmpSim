// Reverb wrapper for AmpSim
// Mono input, stereo output with Dattorro plate reverb.
//
// The Dattorro delay lines are large (~850 KB at 48 kHz, maxTimeScale=4)
// and won't fit in the Daisy Seed SRAM heap. The reverb is therefore
// constructed lazily in init() *after* an SDRAM arena has been installed
// via InterpDelayArena::set(...) — see reverb_arena.h and main.cpp.
//
// Dry/wet mix is applied at the output; process() takes the source signal
// (dry) and returns the mixed L/R stereo output.

#pragma once
#include <memory>
#include "dattorro/Dattorro.hpp"

class ReverbProcessor {
public:
    // Construct the Dattorro engine and apply Flick/MuleBox default params.
    // Call this AFTER InterpDelayArena::set(...) so buffers land in SDRAM.
    void init(float sampleRate) {
        reverb_ = std::make_unique<Dattorro>(sampleRate, 16.0f, 4.0f);
        reverb_->setSampleRate(sampleRate);

        // MuleBox / Flick "plate" preset
        reverb_->setTimeScale(1.007500f);
        reverb_->enableInputDiffusion(true);
        reverb_->setPreDelay(0.0f);

        // Input filtering (pitch units: freq = 440 * 2^(pitch-5))
        reverb_->setInputFilterLowCutoffPitch(2.87f);
        reverb_->setInputFilterHighCutoffPitch(7.25f);

        // Tank parameters
        reverb_->setDecay(0.8f);
        reverb_->setTankDiffusion(0.85f);
        reverb_->setTankFilterHighCutFrequency(7.25f);
        reverb_->setTankFilterLowCutFrequency(2.87f);

        // Modulation
        reverb_->setTankModSpeed(0.8f);
        reverb_->setTankModDepth(1.5f);
        reverb_->setTankModShape(0.25f);
    }

    bool isReady() const { return reverb_ != nullptr; }

    // Process a single sample. `input` is the dry source. `outL`/`outR`
    // receive the dry/wet mixed output using the current mix setting.
    // If mix=0 the output equals the dry input (both channels); if mix=1
    // the output is pure wet reverb.
    void process(float input, float* outL, float* outR) {
        if (!reverb_) {
            *outL = input;
            *outR = input;
            return;
        }
        reverb_->process(input, input);
        const float wetL = reverb_->getLeftOutput();
        const float wetR = reverb_->getRightOutput();
        *outL = input * dryMix_ + wetL * wetMix_;
        *outR = input * dryMix_ + wetR * wetMix_;
    }

    // Mix: 0.0 = fully dry, 1.0 = fully wet. Linear crossfade.
    void setMix(float mix) {
        if (mix < 0.0f) mix = 0.0f;
        if (mix > 1.0f) mix = 1.0f;
        wetMix_ = mix;
        dryMix_ = 1.0f - mix;
    }

    void setDecay(float decay) {
        if (reverb_) reverb_->setDecay(decay);
    }

    void setTone(float tone) {
        // Map 0-1 to a pitch value for the tank high cut (in "pitch" units).
        const float pitch = 2.87f + tone * 4.38f;  // 2.87 .. 7.25
        if (reverb_) reverb_->setTankFilterHighCutFrequency(pitch);
    }

    void setModSpeed(float speed) {
        if (reverb_) reverb_->setTankModSpeed(speed);
    }

    void setModDepth(float depth) {
        if (reverb_) reverb_->setTankModDepth(depth);
    }

    void clear() {
        if (reverb_) reverb_->clear();
    }

    float getDryMix() const { return dryMix_; }
    float getWetMix() const { return wetMix_; }

private:
    std::unique_ptr<Dattorro> reverb_;
    float dryMix_ = 0.7f;
    float wetMix_ = 0.3f;
};
