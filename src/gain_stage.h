#pragma once
#include <cmath>

class GainStage {
public:
    void Init() {
        gain_ = 1.0f;
        targetGain_ = 1.0f;
    }
    
    // Set gain from knob value (-1 to +1 maps to ±20dB)
    void SetGain(float knobValue) {
        if (knobValue < 0.0f) {
            // Attenuation: 0dB to -20dB
            targetGain_ = std::pow(10.0f, knobValue * 20.0f / 20.0f);
        } else {
            // Boost: 0dB to +20dB
            targetGain_ = std::pow(10.0f, knobValue * 20.0f / 20.0f);
        }
    }
    
    float Process(float in) {
        // Simple smoothing to prevent zipper noise
        gain_ += (targetGain_ - gain_) * 0.01f;
        return in * gain_;
    }
    
    float GetGainDb() const {
        return 20.0f * std::log10(gain_);
    }
    
private:
    float gain_;
    float targetGain_;
};
