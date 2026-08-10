// AmpSim - Guitar Amp Simulator based on Daisy Seed
// NAM A2 processing with Dattorro plate reverb and full control scheme.
//
// Signal path (per audio block):
//   in[0] -> [inputGain] -> [NAM] -> [3-band EQ] -> [reverb wet/dry] -> [outputVolume] -> out[0,1]
//
// The dry+wet mix is applied inside ReverbProcessor::process. When both
// effects are off, the true-bypass relay handles the analog path and the
// callback echoes silence (the codec output is muted while the relay flips
// and the analog signal is routed around the DSP).

#include "guitar_pedal_125b.h"
#include "nam_processor.h"
#include "reverb_processor.h"
#include "reverb_arena.h"
#include "dattorro/dsp/delays/InterpDelay.hpp"
#include "model_data.h"
#include "settings.h"
#include "gain_stage.h"
#include "guitar_eq.h"
#include "constants.h"
#include "helpers.h"

#include <string.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

using namespace multifs;

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
GuitarPedal125B hw;

// ---------------------------------------------------------------------------
// DSP components
//
// The reverb is constructed lazily in main() after the SDRAM arena is
// installed, so it MUST come as a plain global that is only wired up via
// its init() method (see ReverbProcessor).
// ---------------------------------------------------------------------------
NAMProcessor namProcessor;
ReverbProcessor reverbProcessor;
GainStage inputGain;
GainStage outputVolume;
GuitarEQ eq;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
SettingsManager settings;
PersistentSettings* currentSettings = nullptr;  // Wired to storage in main().

// Model browsing (encoder-driven).
int previewModelIndex = 0;
bool isPreviewingModel = false;
uint32_t previewStartTime = 0;

// Display update throttling.
uint32_t lastDisplayUpdate = 0;

// Debounced settings save.
uint32_t lastSettingsChange = 0;
bool settingsDirty = false;

// True-bypass relay state tracking.
bool currentBypassState = false;

// Audio-callback-visible suppression flag. Set while the analog mute is
// engaged around a relay flip so the DSP outputs silence for the duration
// (prevents pops on the DAC output while the relay contacts settle).
volatile bool audioSuppressed = false;

// Scratch buffers for block-based DSP. Sized for the maximum audio block
// we ever configure (48 samples per SetAudioBlockSize in Init).
static constexpr size_t kMaxBlock = 128;
float scratchDry[kMaxBlock];
float scratchWet[kMaxBlock];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void SaveSettingsDebounced() {
    settingsDirty = true;
    lastSettingsChange = daisy::System::GetNow();
}

// Manage the true-bypass relay. Both effects OFF => relay engaged (analog
// pass-through). Otherwise DSP is active. Follows bkshepherd's pattern:
// mute the analog output, wait, flip the relay, wait, unmute.
void UpdateBypassRelay() {
    const bool shouldBypass = !currentSettings->namEnabled
                           && !currentSettings->reverbEnabled;

    if (shouldBypass == currentBypassState) return;

    // Suppress DSP output before muting to guarantee silence at the DAC
    // (the mute pin muffs the analog output but the DAC keeps producing).
    audioSuppressed = true;
    hw.SetAudioMute(true);
    hw.DelayMs(MUTE_DELAY_MS);

    hw.SetAudioBypass(shouldBypass);

    hw.DelayMs(MUTE_DELAY_MS);
    hw.SetAudioMute(false);
    audioSuppressed = false;

    currentBypassState = shouldBypass;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
// 128x64 SSD1306. Font_6x8 => max 21 chars per line; Font_7x10 => max 18.
// Text longer than that is truncated by the OLED driver, not our concern
// beyond aesthetics — we still snprintf-clip our own buffers.
void UpdateDisplay() {
    hw.display.Fill(false);

    char line[32];

    const int shownIndex = isPreviewingModel ? previewModelIndex : currentSettings->modelIndex;
    const bool haveModels = (NAM_MODEL_COUNT > 0) && shownIndex >= 0 && shownIndex < NAM_MODEL_COUNT;

    // Line 0: model name (with preview arrow if browsing).
    hw.display.SetCursor(0, 0);
    if (haveModels) {
        snprintf(line, sizeof line, "%s%s",
                 isPreviewingModel ? "> " : "",
                 nam_models[shownIndex].model_name);
    } else {
        snprintf(line, sizeof line, "No models");
    }
    hw.display.WriteString(line, Font_7x10, true);

    // Line 1: variant.
    hw.display.SetCursor(0, 12);
    if (haveModels) {
        snprintf(line, sizeof line, "%s", nam_models[shownIndex].variant_name);
    } else {
        snprintf(line, sizeof line, "(regenerate model_data.h)");
    }
    hw.display.WriteString(line, Font_6x8, true);

    // Line 2: effect states.
    hw.display.SetCursor(0, 22);
    snprintf(line, sizeof line, "NAM:%s REV:%s",
             currentSettings->namEnabled ? "ON " : "OFF",
             currentSettings->reverbEnabled ? "ON " : "OFF");
    hw.display.WriteString(line, Font_6x8, true);

    // Line 3: I/O gain (compact so it fits: max "In:-20 Out:+20" = 14 chars).
    hw.display.SetCursor(0, 32);
    const int inDb = (int)std::lround(inputGain.GetGainDb());
    const int outDb = (int)std::lround(outputVolume.GetGainDb());
    snprintf(line, sizeof line, "In:%+3d Out:%+3d dB", inDb, outDb);
    hw.display.WriteString(line, Font_6x8, true);

    // Line 4: reverb mix + EQ. Keep it under 21 chars for Font_6x8.
    hw.display.SetCursor(0, 42);
    const int revPct = (int)std::lround(currentSettings->reverbMix * 100.0f);
    const int bassDb = (int)std::lround(KnobToNormalized(currentSettings->bass) * EQ_RANGE_DB);
    const int midDb = (int)std::lround(KnobToNormalized(currentSettings->mid) * EQ_RANGE_DB);
    const int trebDb = (int)std::lround(KnobToNormalized(currentSettings->treble) * EQ_RANGE_DB);
    snprintf(line, sizeof line, "R%3d B%+3d M%+3d T%+3d", revPct, bassDb, midDb, trebDb);
    hw.display.WriteString(line, Font_6x8, true);

    // Line 5: context-sensitive help.
    hw.display.SetCursor(0, 52);
    if (isPreviewingModel) {
        hw.display.WriteString("Click to load", Font_6x8, true);
    } else {
        hw.display.WriteString("FS1:Rev FS2:NAM Enc:Mdl", Font_6x8, true);
    }

    hw.display.Update();
}

// Show a fullscreen message. Used for load progress and error surfaces.
static void ShowMessage(const char* line0, const char* line1 = nullptr) {
    hw.display.Fill(false);
    hw.display.SetCursor(0, 0);
    hw.display.WriteString(line0, Font_7x10, true);
    if (line1) {
        hw.display.SetCursor(0, 14);
        hw.display.WriteString(line1, Font_6x8, true);
    }
    hw.display.Update();
}

// ---------------------------------------------------------------------------
// Model management
// ---------------------------------------------------------------------------
bool IsValidModelIndex(int index) {
    return index >= 0 && index < NAM_MODEL_COUNT;
}

// Load a model by index. Suppresses audio for the duration so we don't
// glitch. Preserves the previously loaded model on failure by not touching
// currentSettings->modelIndex until success is confirmed.
void LoadModel(int index) {
    if (!IsValidModelIndex(index)) {
        ShowMessage("Invalid model");
        hw.DelayMs(ERROR_DISPLAY_TIME_MS);
        return;
    }

    ShowMessage("Loading...", nam_models[index].variant_name);

    // Silence the DSP while NAM tears down and rebuilds; the load itself
    // is not real-time-safe (heap allocations).
    audioSuppressed = true;
    const bool ok = namProcessor.loadModel(nam_models[index].model_json,
                                           nam_models[index].model_json_len);
    audioSuppressed = false;

    if (!ok) {
        ShowMessage("Load failed", nam_models[index].variant_name);
        hw.DelayMs(ERROR_DISPLAY_TIME_MS);
        // Keep the old model index; the previous model is now gone (NAMProcessor
        // clears state before parsing) but at least we don't persist a bad index.
        return;
    }

    currentSettings->modelIndex = index;
    SaveSettingsDebounced();
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t size) {
    // Clamp to our scratch capacity as a safety measure.
    if (size > kMaxBlock) size = kMaxBlock;

    // Fast-path silence when audio is suppressed (mute window / model load).
    if (audioSuppressed) {
        for (size_t i = 0; i < size; ++i) { out[0][i] = 0.0f; out[1][i] = 0.0f; }
        return;
    }

    // True-bypass path (both effects off). The relay routes the analog
    // signal around the DSP; we still output silence in case the DAC isn't
    // physically disconnected from the output jack.
    if (!currentSettings->namEnabled && !currentSettings->reverbEnabled) {
        for (size_t i = 0; i < size; ++i) {
            const float x = in[0][i];
            out[0][i] = x;
            out[1][i] = x;
        }
        return;
    }

    // -- DSP path --
    // 1) Input gain (per-sample tick keeps smoothing rate = sample rate).
    for (size_t i = 0; i < size; ++i) scratchDry[i] = in[0][i] * inputGain.Tick();

    // 2) NAM in one shot per block (necessary for a2_fast to amortise loop
    //    overhead; per-sample calls destroy throughput).
    if (currentSettings->namEnabled && namProcessor.isModelLoaded()) {
        namProcessor.process(scratchDry, scratchWet, size);
        // 3) Tone stack (only when NAM is on — pass-through otherwise).
        eq.ProcessBlock(scratchWet, scratchWet, size);
    } else {
        // Pass through the dry path (NAM off) as if it were the "wet".
        for (size_t i = 0; i < size; ++i) scratchWet[i] = scratchDry[i];
    }

    // 4) Reverb + output volume + stereo. When reverb is off we still write
    //    to both output channels for consistent monitoring.
    if (currentSettings->reverbEnabled) {
        for (size_t i = 0; i < size; ++i) {
            float l, r;
            reverbProcessor.process(scratchWet[i], &l, &r);
            const float g = outputVolume.Tick();
            out[0][i] = l * g;
            out[1][i] = r * g;
        }
    } else {
        for (size_t i = 0; i < size; ++i) {
            const float g = outputVolume.Tick();
            const float y = scratchWet[i] * g;
            out[0][i] = y;
            out[1][i] = y;
        }
    }
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------
void HandleEncoderMovement() {
    if (NAM_MODEL_COUNT == 0) return;

    const int32_t inc = hw.encoders[0].Increment();
    if (inc == 0) return;

    int newIndex = previewModelIndex + inc;
    if (newIndex < 0) newIndex = NAM_MODEL_COUNT - 1;
    if (newIndex >= NAM_MODEL_COUNT) newIndex = 0;

    previewModelIndex = newIndex;
    isPreviewingModel = true;
    previewStartTime = daisy::System::GetNow();
}

void HandleEncoderClick() {
    if (!hw.encoders[0].RisingEdge()) return;

    if (isPreviewingModel) {
        // Snapshot the target before we clear the flag so that if the
        // timeout races with this callback we still load what the user saw.
        const int target = previewModelIndex;
        isPreviewingModel = false;
        LoadModel(target);
    }
}

void CheckPreviewTimeout() {
    if (!isPreviewingModel) return;

    const uint32_t now = daisy::System::GetNow();
    if (now - previewStartTime > PREVIEW_TIMEOUT_MS) {
        previewModelIndex = IsValidModelIndex(currentSettings->modelIndex)
                                ? currentSettings->modelIndex
                                : 0;
        isPreviewingModel = false;
    }
}

void HandleFootswitches() {
    // FS1 (Left): Reverb on/off
    if (hw.switches[0].RisingEdge()) {
        currentSettings->reverbEnabled = !currentSettings->reverbEnabled;
        hw.SetLed(0, currentSettings->reverbEnabled ? 1.0f : 0.0f);
        hw.UpdateLeds();
        UpdateBypassRelay();
        SaveSettingsDebounced();
    }

    // FS2 (Right): NAM on/off
    if (hw.switches[1].RisingEdge()) {
        currentSettings->namEnabled = !currentSettings->namEnabled;
        hw.SetLed(1, currentSettings->namEnabled ? 1.0f : 0.0f);
        hw.UpdateLeds();
        UpdateBypassRelay();
        SaveSettingsDebounced();
    }
}

// Deadbanded knob update helper. Returns true if a change was applied.
template <typename Apply>
static bool UpdateKnob(float knobRaw, float& stored, Apply apply) {
    if (std::abs(knobRaw - stored) <= KNOB_DEADBAND) return false;
    stored = knobRaw;
    apply(knobRaw);
    return true;
}

void HandleKnobs() {
    bool changed = false;
    changed |= UpdateKnob(hw.knobs[0].Value(), currentSettings->inputGain,
                          [](float v) { inputGain.SetGain(KnobToNormalized(v)); });
    changed |= UpdateKnob(hw.knobs[1].Value(), currentSettings->outputVolume,
                          [](float v) { outputVolume.SetGain(KnobToNormalized(v)); });
    changed |= UpdateKnob(hw.knobs[2].Value(), currentSettings->reverbMix,
                          [](float v) { reverbProcessor.setMix(v); });
    changed |= UpdateKnob(hw.knobs[3].Value(), currentSettings->bass,
                          [](float v) { eq.SetBass(KnobToNormalized(v)); });
    changed |= UpdateKnob(hw.knobs[4].Value(), currentSettings->mid,
                          [](float v) { eq.SetMid(KnobToNormalized(v)); });
    changed |= UpdateKnob(hw.knobs[5].Value(), currentSettings->treble,
                          [](float v) { eq.SetTreble(KnobToNormalized(v)); });
    if (changed) SaveSettingsDebounced();
}

void CheckSettingsSave() {
    if (!settingsDirty) return;
    const uint32_t now = daisy::System::GetNow();
    if (now - lastSettingsChange > SAVE_DELAY_MS) {
        settings.Save();
        settingsDirty = false;
    }
}

// ---------------------------------------------------------------------------
// Init helpers
// ---------------------------------------------------------------------------
static void InitReverbOrHalt() {
    // Install the SDRAM arena BEFORE constructing Dattorro so all InterpDelay
    // instances land there. If this fails the pedal is unusable, so surface
    // it on the display and hang; better than a silent crash on hardware.
    InterpDelayArena::set(g_reverb_arena, kReverbArenaFloats);
    try {
        reverbProcessor.init(hw.AudioSampleRate());
    } catch (...) {
        ShowMessage("Reverb OOM", "SDRAM exhausted");
        // Blink both LEDs alternately to indicate init failure.
        while (true) {
            hw.SetLed(0, 1.0f); hw.SetLed(1, 0.0f); hw.UpdateLeds();
            hw.DelayMs(250);
            hw.SetLed(0, 0.0f); hw.SetLed(1, 1.0f); hw.UpdateLeds();
            hw.DelayMs(250);
        }
    }
    if (InterpDelayArena::exhausted()) {
        // The arena wasn't big enough: some delay lines fell back to heap
        // (or failed). Surface it — even if we didn't throw, the reverb may
        // be silently broken.
        ShowMessage("Reverb arena", "underprovisioned");
        hw.DelayMs(ERROR_DISPLAY_TIME_MS);
    }
    // After construction, further InterpDelay creation shouldn't happen; the
    // reverb doesn't dynamically add delays. Clear the arena pointer so any
    // stray InterpDelay would fall back to heap and be caught in review.
    InterpDelayArena::set(nullptr, 0);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(void) {
    hw.Init(48, true);
    hw.SetAudioBlockSize(48);

    // Settings — must be initialized before anything reads currentSettings.
    settings.Init(hw.seed.qspi);
    settings.ValidateSettings(NAM_MODEL_COUNT);
    currentSettings = &settings.GetSettings();

    ShowMessage("Initializing", "AmpSim");

    // DSP init (no allocations here).
    inputGain.Init(hw.AudioSampleRate());
    outputVolume.Init(hw.AudioSampleRate());
    eq.Init(hw.AudioSampleRate());

    // Reverb (allocates from SDRAM arena).
    InitReverbOrHalt();

    // Apply saved control values.
    inputGain.SetGain(KnobToNormalized(currentSettings->inputGain));
    outputVolume.SetGain(KnobToNormalized(currentSettings->outputVolume));
    reverbProcessor.setMix(currentSettings->reverbMix);
    eq.SetBass(KnobToNormalized(currentSettings->bass));
    eq.SetMid(KnobToNormalized(currentSettings->mid));
    eq.SetTreble(KnobToNormalized(currentSettings->treble));

    // Enable NAM loudness normalization to -18 dBFS-ish.
    namProcessor.setSampleRate(hw.AudioSampleRate());
    namProcessor.setLoudnessTarget(-18.0f);

    if (NAM_MODEL_COUNT == 0) {
        ShowMessage("No models!", "Use nam_to_header.py");
    } else {
        previewModelIndex = IsValidModelIndex(currentSettings->modelIndex)
                                ? currentSettings->modelIndex
                                : 0;
        LoadModel(previewModelIndex);
    }

    // Restore LEDs / bypass state from persisted settings.
    hw.SetLed(0, currentSettings->reverbEnabled ? 1.0f : 0.0f);
    hw.SetLed(1, currentSettings->namEnabled ? 1.0f : 0.0f);
    hw.UpdateLeds();
    currentBypassState = !currentSettings->namEnabled && !currentSettings->reverbEnabled;
    hw.SetAudioBypass(currentBypassState);

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while (true) {
        hw.ProcessAllControls();

        HandleEncoderMovement();
        HandleEncoderClick();
        CheckPreviewTimeout();

        HandleFootswitches();
        HandleKnobs();

        CheckSettingsSave();

        const uint32_t now = daisy::System::GetNow();
        if (now - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL_MS) {
            UpdateDisplay();
            lastDisplayUpdate = now;
        }
    }
}
