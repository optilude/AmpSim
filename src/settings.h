#pragma once
#include "daisy.h"
#include "constants.h"
#include <cstdint>

// Persistent settings serialized to QSPI via daisy::PersistentStorage. This is
// only safe because the firmware runs with BOOT_SRAM in this branch.
// IMPORTANT: This struct is written raw. Do not add non-trivially-copyable
// members. If you change layout, bump the schema below so on-disk data is
// treated as factory defaults instead of being reinterpreted.
struct PersistentSettings {
    // Bump when the struct layout changes to invalidate stale flash.
    static constexpr uint32_t kSchemaVersion = 2;

    uint32_t schemaVersion;
    int32_t modelIndex;
    uint8_t namEnabled;
    uint8_t reverbEnabled;

    PersistentSettings()
        : schemaVersion(kSchemaVersion)
        , modelIndex(0)
        , namEnabled(true)
        , reverbEnabled(true)
    {}

    bool operator==(const PersistentSettings& other) const {
        return schemaVersion == other.schemaVersion
            && modelIndex == other.modelIndex
            && namEnabled == other.namEnabled
            && reverbEnabled == other.reverbEnabled;
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

    ~SettingsManager() { delete storage_; }

    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    void Init(daisy::QSPIHandle& qspi, uint32_t addressOffset) {
        storage_ = new daisy::PersistentStorage<PersistentSettings>(qspi);
        PersistentSettings defaults;
        storage_->Init(defaults, addressOffset);
        if (storage_->GetSettings().schemaVersion != PersistentSettings::kSchemaVersion) {
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
        if (initialized_ && storage_) storage_->Save();
    }

    void RestoreDefaults() {
        if (initialized_ && storage_) storage_->RestoreDefaults();
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

        s.namEnabled = s.namEnabled ? 1 : 0;
        s.reverbEnabled = s.reverbEnabled ? 1 : 0;
    }

private:
    daisy::PersistentStorage<PersistentSettings>* storage_;
    bool initialized_;
};
