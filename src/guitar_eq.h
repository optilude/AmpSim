#pragma once
#include <cmath>
#include "daisysp.h"
#include "constants.h"

using namespace daisysp;

class GuitarEQ {
public:
    void Init(float sampleRate) {
        sampleRate_ = sampleRate;
        
        // Bass: Low shelf at 100Hz
        bassFilter_.Init(sampleRate);
        bassFilter_.SetFreq(BASS_FREQ);
        bassFilter_.SetRes(0.707f);
        bassFilter_.SetDrive(0.0f);
        
        // Mid: Peak at 1kHz
        midFilter_.Init(sampleRate);
        midFilter_.SetFreq(MID_FREQ);
        midFilter_.SetRes(1.0f);  // Higher Q for peak
        midFilter_.SetDrive(0.0f);
        
        // Treble: High shelf at 4kHz
        trebleFilter_.Init(sampleRate);
        trebleFilter_.SetFreq(TREBLE_FREQ);
        trebleFilter_.SetRes(0.707f);
        trebleFilter_.SetDrive(0.0f);
        
        bassGain_ = 0.0f;
        midGain_ = 0.0f;
        trebleGain_ = 0.0f;
    }
    
    void SetBass(float gain) {
        // Map -1 to +1 to ±12dB
        bassGain_ = gain * EQ_RANGE_DB;
    }
    
    void SetMid(float gain) {
        midGain_ = gain * EQ_RANGE_DB;
    }
    
    void SetTreble(float gain) {
        trebleGain_ = gain * EQ_RANGE_DB;
    }
    
    float Process(float in) {
        float out = in;
        
        // Bass (low shelf boost/cut)
        // EQ boost vs cut uses different filter modes:
        // - Boost: Use shelf filter (Low/High outputs) for smooth frequency shaping
        // - Cut: Use opposite filter (High/Low outputs) for natural attenuation
        if (std::abs(bassGain_) > EQ_PROCESS_THRESHOLD) {
            bassFilter_.Process(out);
            if (bassGain_ > 0.0f) {
                float freq = BASS_FREQ;
                if (freq != lastBassFreq_) {
                    bassFilter_.SetFreq(freq);
                    lastBassFreq_ = freq;
                }
                float bass = bassFilter_.Low();
                out = out + (bass * dbToLinear(bassGain_) - in) * 0.5f;
            } else {
                float freq = BASS_FREQ * dbToLinear(-bassGain_);
                if (std::abs(freq - lastBassFreq_) > 0.1f) {
                    bassFilter_.SetFreq(freq);
                    lastBassFreq_ = freq;
                }
                out = bassFilter_.High();
            }
        }
        
        // Mid (peaking)
        if (std::abs(midGain_) > EQ_PROCESS_THRESHOLD) {
            midFilter_.Process(out);
            float mid = midFilter_.Peak();
            float gain = dbToLinear(midGain_);
            out = out + (mid * gain - out) * 0.3f;
        }
        
        // Treble (high shelf boost/cut)
        if (std::abs(trebleGain_) > EQ_PROCESS_THRESHOLD) {
            trebleFilter_.Process(out);
            if (trebleGain_ > 0.0f) {
                float freq = TREBLE_FREQ;
                if (freq != lastTrebleFreq_) {
                    trebleFilter_.SetFreq(freq);
                    lastTrebleFreq_ = freq;
                }
                float treble = trebleFilter_.High();
                out = out + (treble * dbToLinear(trebleGain_) - in) * 0.5f;
            } else {
                float freq = TREBLE_FREQ / dbToLinear(-trebleGain_);
                if (std::abs(freq - lastTrebleFreq_) > 0.1f) {
                    trebleFilter_.SetFreq(freq);
                    lastTrebleFreq_ = freq;
                }
                out = trebleFilter_.Low();
            }
        }
        
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
    
    // Cache for avoiding redundant recalculations
    float lastBassFreq_ = 100.0f;
    float lastTrebleFreq_ = 4000.0f;
    
    inline float dbToLinear(float db) {
        return std::pow(10.0f, db / 20.0f);
    }
};
