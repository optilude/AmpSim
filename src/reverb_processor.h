// Reverb wrapper for AmpSim
// Mono input, stereo output with Dattorro plate reverb

#pragma once
#include "dattorro/Dattorro.hpp"

class ReverbProcessor {
public:
    ReverbProcessor() : reverb(48000.0f, 16.0f, 4.0f) {}
    
    void init(float sampleRate) {
        reverb.setSampleRate(sampleRate);
        
        // MuleBox Flick default parameters
        reverb.setTimeScale(1.007500f);
        reverb.enableInputDiffusion(true);
        reverb.setPreDelay(0.0f);
        
        // Input filtering
        reverb.setInputFilterLowCutoffPitch(2.87f);
        reverb.setInputFilterHighCutoffPitch(7.25f);
        
        // Tank parameters
        reverb.setDecay(0.8f);
        reverb.setTankDiffusion(0.85f);
        reverb.setTankFilterHighCutFrequency(7.25f);
        reverb.setTankFilterLowCutFrequency(2.87f);
        
        // Modulation
        reverb.setTankModSpeed(0.8f);
        reverb.setTankModDepth(1.5f);
        reverb.setTankModShape(0.25f);
    }
    
    void process(float input, float* outL, float* outR) {
        reverb.process(input, input);
        *outL = reverb.getLeftOutput();
        *outR = reverb.getRightOutput();
    }
    
    void setMix(float mix) {
        wetMix = mix;
        dryMix = 1.0f - mix;
    }
    
    void setDecay(float decay) {
        reverb.setDecay(decay);
    }
    
    void setTone(float tone) {
        // Map 0-1 to pitch value for high cut
        float pitch = 2.87f + tone * 4.38f;  // Range from 2.87 to 7.25
        reverb.setTankFilterHighCutFrequency(pitch);
    }
    
    void setModSpeed(float speed) {
        reverb.setTankModSpeed(speed);
    }
    
    void setModDepth(float depth) {
        reverb.setTankModDepth(depth);
    }
    
    void clear() {
        reverb.clear();
    }
    
    float getDryMix() const { return dryMix; }
    float getWetMix() const { return wetMix; }
    
private:
    Dattorro reverb;
    float dryMix = 0.5f;
    float wetMix = 0.5f;
};
