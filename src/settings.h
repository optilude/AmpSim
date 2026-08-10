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
    
    void ValidateSettings(int maxModelIndex) {
        // Clamp model index to valid range
        if (settings_.modelIndex < 0 || settings_.modelIndex >= maxModelIndex) {
            settings_.modelIndex = 0;
        }
        
        // Clamp float values to valid ranges
        settings_.inputGain = std::max(0.0f, std::min(1.0f, settings_.inputGain));
        settings_.outputVolume = std::max(0.0f, std::min(1.0f, settings_.outputVolume));
        settings_.reverbMix = std::max(0.0f, std::min(1.0f, settings_.reverbMix));
        settings_.bass = std::max(0.0f, std::min(1.0f, settings_.bass));
        settings_.mid = std::max(0.0f, std::min(1.0f, settings_.mid));
        settings_.treble = std::max(0.0f, std::min(1.0f, settings_.treble));
    }
    
private:
    daisy::PersistentStorage<PersistentSettings>* storage_;
    PersistentSettings settings_;
    bool initialized_;
};
