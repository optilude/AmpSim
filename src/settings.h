#pragma once
#include "daisy.h"

struct PersistentSettings {
    int32_t modelIndex;
    bool namEnabled;
    bool reverbEnabled;
    float inputGain;
    float outputVolume;
    float reverbMix;
    float bass;
    float mid;
    float treble;
    
    PersistentSettings() 
        : modelIndex(0)
        , namEnabled(true)
        , reverbEnabled(true)
        , inputGain(0.5f)
        , outputVolume(0.5f)
        , reverbMix(0.3f)
        , bass(0.5f)
        , mid(0.5f)
        , treble(0.5f)
    {}
    
    bool operator==(const PersistentSettings& other) const {
        return modelIndex == other.modelIndex
            && namEnabled == other.namEnabled
            && reverbEnabled == other.reverbEnabled
            && std::abs(inputGain - other.inputGain) < 0.01f
            && std::abs(outputVolume - other.outputVolume) < 0.01f
            && std::abs(reverbMix - other.reverbMix) < 0.01f
            && std::abs(bass - other.bass) < 0.01f
            && std::abs(mid - other.mid) < 0.01f
            && std::abs(treble - other.treble) < 0.01f;
    }
    
    bool operator!=(const PersistentSettings& other) const {
        return !(*this == other);
    }
};

class SettingsManager {
public:
    SettingsManager() : storage_(nullptr), initialized_(false) {}
    
    void Init(daisy::QSPIHandle& qspi) {
        storage_ = new daisy::PersistentStorage<PersistentSettings>(qspi);
        
        PersistentSettings defaults;
        storage_->Init(defaults);
        
        settings_ = storage_->GetSettings();
        initialized_ = true;
    }
    
    ~SettingsManager() {
        if (storage_) {
            delete storage_;
        }
    }
    
    PersistentSettings& GetSettings() {
        return settings_;
    }
    
    void Save() {
        if (initialized_ && storage_) {
            storage_->Save();
        }
    }
    
    void RestoreDefaults() {
        if (initialized_ && storage_) {
            storage_->RestoreDefaults();
            settings_ = storage_->GetSettings();
        }
    }
    
private:
    daisy::PersistentStorage<PersistentSettings>* storage_;
    PersistentSettings settings_;
    bool initialized_;
};
