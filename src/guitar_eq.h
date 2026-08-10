#pragma once
#include <cmath>
#include "daisysp.h"

using namespace daisysp;

class GuitarEQ {
public:
    void Init(float sampleRate) {
        sampleRate_ = sampleRate;
        
        // Bass: Low shelf at 100Hz
        bassFilter_.Init(sampleRate);
        bassFilter_.SetFreq(100.0f);
        bassFilter_.SetRes(0.707f);
        bassFilter_.SetDrive(0.0f);
        
        // Mid: Peak at 1kHz
        midFilter_.Init(sampleRate);
        midFilter_.SetFreq(1000.0f);
        midFilter_.SetRes(1.0f);  // Higher Q for peak
        midFilter_.SetDrive(0.0f);
        
        // Treble: High shelf at 4kHz
        trebleFilter_.Init(sampleRate);
        trebleFilter_.SetFreq(4000.0f);
        trebleFilter_.SetRes(0.707f);
        trebleFilter_.SetDrive(0.0f);
        
        bassGain_ = 0.0f;
        midGain_ = 0.0f;
        trebleGain_ = 0.0f;
    }
    
    void SetBass(float gain) {
        // Map -1 to +1 to ±12dB
        bassGain_ = gain * 12.0f;
    }
    
    void SetMid(float gain) {
        midGain_ = gain * 12.0f;
    }
    
    void SetTreble(float gain) {
        trebleGain_ = gain * 12.0f;
    }
    
    float Process(float in) {
        float out = in;
        
        // Bass (low shelf boost/cut)
        if (bassGain_ > 0.0f) {
            bassFilter_.SetFreq(100.0f);
            float bass = bassFilter_.Low();
            out = out + (bass * dbToLinear(bassGain_) - in) * 0.5f;
        } else if (bassGain_ < 0.0f) {
            // Cut: Use high pass
            bassFilter_.SetFreq(100.0f * dbToLinear(-bassGain_));
            out = bassFilter_.High();
        }
        bassFilter_.Process(out);
        
        // Mid (peaking)
        if (std::abs(midGain_) > 0.1f) {
            float mid = midFilter_.Peak();
            float gain = dbToLinear(midGain_);
            out = out + (mid * gain - out) * 0.3f;
        }
        midFilter_.Process(out);
        
        // Treble (high shelf boost/cut)
        if (trebleGain_ > 0.0f) {
            trebleFilter_.SetFreq(4000.0f);
            float treble = trebleFilter_.High();
            out = out + (treble * dbToLinear(trebleGain_) - in) * 0.5f;
        } else if (trebleGain_ < 0.0f) {
            // Cut: Use low pass
            trebleFilter_.SetFreq(4000.0f / dbToLinear(-trebleGain_));
            out = trebleFilter_.Low();
        }
        trebleFilter_.Process(out);
        
        return out;
    }
    
private:
    float sampleRate_;
    Svf bassFilter_;
    Svf midFilter_;
    Svf trebleFilter_;
    float bassGain_;
    float midGain_;
    float trebleGain_;
    
    inline float dbToLinear(float db) {
        return std::pow(10.0f, db / 20.0f);
    }
};
