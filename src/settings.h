#pragma once
#include "daisy.h"
#include "constants.h"

// Persistent settings serialized to QSPI via daisy::PersistentStorage.
// IMPORTANT: This struct is written raw. Do not add non-trivially-copyable
// members. If you change layout, bump the schema below so on-disk data is
// treated as factory defaults instead of being reinterpreted.
struct PersistentSettings {
    // Bump when the struct layout changes to invalidate stale flash.
    static constexpr uint32_t kSchemaVersion = 1;

    uint32_t schemaVersion;
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
        : schemaVersion(kSchemaVersion)
        , modelIndex(0)
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
        return schemaVersion == other.schemaVersion
            && modelIndex == other.modelIndex
            && namEnabled == other.namEnabled
            && reverbEnabled == other.reverbEnabled
            && std::abs(inputGain - other.inputGain) < KNOB_DEADBAND
            && std::abs(outputVolume - other.outputVolume) < KNOB_DEADBAND
            && std::abs(reverbMix - other.reverbMix) < KNOB_DEADBAND
            && std::abs(bass - other.bass) < KNOB_DEADBAND
            && std::abs(mid - other.mid) < KNOB_DEADBAND
            && std::abs(treble - other.treble) < KNOB_DEADBAND;
    }

    bool operator!=(const PersistentSettings& other) const {
        return !(*this == other);
    }
};

// Thin wrapper around daisy::PersistentStorage.
//
// The wrapper deliberately does NOT keep its own copy of the settings:
// the previous implementation shadowed the storage's data and Save() then
// wrote stale (factory-default) values back to flash, silently losing all
// user changes. GetSettings() now returns a reference directly into the
// underlying PersistentStorage so mutations propagate to Save().
class SettingsManager {
public:
    SettingsManager() : storage_(nullptr), initialized_(false) {}

    ~SettingsManager() {
        delete storage_;
    }

    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    void Init(daisy::QSPIHandle& qspi) {
        storage_ = new daisy::PersistentStorage<PersistentSettings>(qspi);

        PersistentSettings defaults;
        storage_->Init(defaults);

        // If the loaded settings have the wrong schema version, treat as
        // uninitialised and restore defaults. Guards against reading garbage
        // after a struct layout change or a fresh flash.
        if (storage_->GetSettings().schemaVersion
            != PersistentSettings::kSchemaVersion) {
            storage_->RestoreDefaults();
        }

        initialized_ = true;
    }

    // Returns a reference to the live settings held by PersistentStorage.
    // Mutations to the returned struct are visible to Save().
    PersistentSettings& GetSettings() {
        return storage_->GetSettings();
    }

    void Save() {
        if (initialized_ && storage_) {
            storage_->Save();
        }
    }

    void RestoreDefaults() {
        if (initialized_ && storage_) {
            storage_->RestoreDefaults();
        }
    }

    // Clamp all fields into their valid ranges. Called after Init to reject
    // corrupted flash contents that survived the schema check.
    void ValidateSettings(int maxModelIndex) {
        PersistentSettings& s = GetSettings();

        if (maxModelIndex <= 0) {
            s.modelIndex = 0;
        } else if (s.modelIndex < 0 || s.modelIndex >= maxModelIndex) {
            s.modelIndex = 0;
        }

        auto clamp01 = [](float v) {
            if (v < 0.0f) return 0.0f;
            if (v > 1.0f) return 1.0f;
            return v;
        };
        s.inputGain = clamp01(s.inputGain);
        s.outputVolume = clamp01(s.outputVolume);
        s.reverbMix = clamp01(s.reverbMix);
        s.bass = clamp01(s.bass);
        s.mid = clamp01(s.mid);
        s.treble = clamp01(s.treble);
    }

private:
    daisy::PersistentStorage<PersistentSettings>* storage_;
    bool initialized_;
};
