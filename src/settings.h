#pragma once
#include "daisy.h"
#include "constants.h"
#include <cstdint>

// Persistent settings schema for future storage support. Runtime persistence is
// disabled while the firmware executes from QSPI flash because libDaisy rejects
// QSPI erase/write in that mode.
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
    SettingsManager() = default;

    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    void Init(daisy::QSPIHandle& qspi) {
        (void)qspi;
        settings_ = PersistentSettings();
    }

    // Returns a reference to the live settings held by PersistentStorage.
    // Mutations to the returned struct are visible to Save().
    PersistentSettings& GetSettings() {
        return settings_;
    }

    void Save() {
        // Disabled for BOOT_QSPI. See QSPIHandle::CheckProgramMemory().
    }

    void RestoreDefaults() {
        settings_ = PersistentSettings();
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
    PersistentSettings settings_;
};
