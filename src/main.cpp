// AmpSim - Guitar Amp Simulator based on Daisy Seed
// NAM A2 processing with Dattorro plate reverb and full control scheme

#include "guitar_pedal_125b.h"
#include "nam_processor.h"
#include "reverb_processor.h"
#include "model_data.h"
#include "settings.h"
#include "gain_stage.h"
#include "guitar_eq.h"
#include "constants.h"
#include <string.h>
#include <cmath>

using namespace multifs;

// Hardware
GuitarPedal125B hw;

// DSP Components
NAMProcessor namProcessor;
ReverbProcessor reverbProcessor;
GainStage inputGain;
GainStage outputVolume;
GuitarEQ eq;

// State Management
SettingsManager settings;
PersistentSettings currentSettings;

// Model browsing state
int previewModelIndex = 0;
bool isPreviewingModel = false;
uint32_t previewStartTime = 0;

// Display update throttling
uint32_t lastDisplayUpdate = 0;

// Save debouncing
uint32_t lastSettingsChange = 0;
bool settingsDirty = false;

// Bypass relay state tracking
bool currentBypassState = false;

void SaveSettingsDebounced() {
    settingsDirty = true;
    lastSettingsChange = daisy::System::GetNow();
}

void UpdateBypassRelay() {
    // Only use true bypass when BOTH effects are off
    bool shouldBypass = !currentSettings.namEnabled && !currentSettings.reverbEnabled;
    
    if (shouldBypass != currentBypassState) {
        // Mute before switching relay
        hw.SetAudioMute(true);
        hw.DelayMs(MUTE_DELAY_MS);
        
        // Switch relay
        hw.SetAudioBypass(shouldBypass);
        
        // Unmute
        hw.DelayMs(MUTE_DELAY_MS);
        hw.SetAudioMute(false);
        
        currentBypassState = shouldBypass;
    }
}

void UpdateDisplay() {
    hw.display.Fill(false);
    
    hw.display.SetCursor(0, 0);
    char line[32];
    
    // Line 0: Model name
    if (isPreviewingModel) {
        sprintf(line, "-> %s", nam_models[previewModelIndex].model_name);
    } else {
        sprintf(line, "%s", nam_models[currentSettings.modelIndex].model_name);
    }
    hw.display.WriteString(line, Font_7x10, true);
    
    // Line 1: Variant name
    hw.display.SetCursor(0, 12);
    if (isPreviewingModel) {
        sprintf(line, "   %s", nam_models[previewModelIndex].variant_name);
    } else {
        sprintf(line, "%s", nam_models[currentSettings.modelIndex].variant_name);
    }
    hw.display.WriteString(line, Font_6x8, true);
    
    // Line 2: NAM/Reverb status
    hw.display.SetCursor(0, 22);
    sprintf(line, "[NAM:%s] [REV:%s]",
            currentSettings.namEnabled ? "ON " : "OFF",
            currentSettings.reverbEnabled ? "ON " : "OFF");
    hw.display.WriteString(line, Font_6x8, true);
    
    // Line 3: Input/Output levels
    hw.display.SetCursor(0, 32);
    float inDb = inputGain.GetGainDb();
    float outDb = outputVolume.GetGainDb();
    sprintf(line, "In:%+5.1fdB Out:%+5.1fdB", inDb, outDb);
    hw.display.WriteString(line, Font_6x8, true);
    
    // Line 4: Reverb mix, EQ settings
    hw.display.SetCursor(0, 42);
    int revPct = (int)(currentSettings.reverbMix * 100.0f);
    int bassDb = (int)(currentSettings.bass * 12.0f);
    int midDb = (int)(currentSettings.mid * 12.0f);
    int trebDb = (int)(currentSettings.treble * 12.0f);
    sprintf(line, "Rev:%3d%% EQ:B%+2d M%+2d T%+2d", revPct, bassDb, midDb, trebDb);
    hw.display.WriteString(line, Font_6x8, true);
    
    // Line 5: Encoder instruction
    hw.display.SetCursor(0, 52);
    if (isPreviewingModel) {
        hw.display.WriteString("Click to load, or wait", Font_6x8, true);
    } else {
        hw.display.WriteString("FS1:Rev FS2:NAM Enc:Model", Font_6x8, true);
    }
    
    hw.display.Update();
}

bool IsValidModelIndex(int index) {
    return index >= 0 && index < NAM_MODEL_COUNT;
}

void LoadModel(int index) {
    if (!IsValidModelIndex(index)) {
        hw.display.Fill(false);
        hw.display.SetCursor(0, 0);
        hw.display.WriteString("Invalid Model!", Font_7x10, true);
        hw.display.Update();
        hw.DelayMs(ERROR_DISPLAY_TIME_MS);
        return;
    }
    
    hw.display.Fill(false);
    hw.display.SetCursor(0, 0);
    hw.display.WriteString("Loading...", Font_7x10, true);
    hw.display.Update();
    
    if (!namProcessor.loadModel(nam_models[index].model_json)) {
        // Show error if model load failed
        hw.display.Fill(false);
        hw.display.SetCursor(0, 0);
        hw.display.WriteString("Model Load", Font_7x10, true);
        hw.display.SetCursor(0, 12);
        hw.display.WriteString("Failed!", Font_7x10, true);
        hw.display.Update();
        hw.DelayMs(ERROR_DISPLAY_TIME_MS);
        return;
    }
    
    currentSettings.modelIndex = index;
    SaveSettingsDebounced();
}

void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t size) {
    for (size_t i = 0; i < size; i++) {
        float input = in[0][i];
        
        // Check if both effects are off (relay handles bypass)
        if (!currentSettings.namEnabled && !currentSettings.reverbEnabled) {
            // True bypass relay is engaged, pass audio through
            // (relay routes analog signal around DSP, but we still output here for safety)
            out[0][i] = input;
            out[1][i] = input;
            continue;
        }
        
        // At least one effect is ON
        float output = input;
        
        // Input gain (always active when effects are on)
        output = inputGain.Process(input);
        
        // NAM (if enabled)
        if (currentSettings.namEnabled && namProcessor.isModelLoaded()) {
            namProcessor.process(&output, &output, 1);
            output = eq.Process(output);
        }
        
        // Reverb (if enabled)
        if (currentSettings.reverbEnabled) {
            float reverbL, reverbR;
            reverbProcessor.process(output, &reverbL, &reverbR);
            // Reverb processor already applied dry/wet mixing
            out[0][i] = outputVolume.Process(reverbL);
            out[1][i] = outputVolume.Process(reverbR);
        } else {
            output = outputVolume.Process(output);
            out[0][i] = output;
            out[1][i] = output;
        }
    }
}

void HandleEncoderMovement() {
    int32_t inc = hw.encoders[0].Increment();
    
    if (inc != 0) {
        int newIndex = previewModelIndex + inc;
        
        // Wrap around
        if (newIndex < 0) newIndex = NAM_MODEL_COUNT - 1;
        if (newIndex >= NAM_MODEL_COUNT) newIndex = 0;
        
        previewModelIndex = newIndex;
        isPreviewingModel = true;
        previewStartTime = daisy::System::GetNow();
    }
}

void HandleEncoderClick() {
    if (hw.encoders[0].RisingEdge()) {
        if (isPreviewingModel) {
            // Load the previewed model
            LoadModel(previewModelIndex);
            isPreviewingModel = false;
        }
    }
}

void CheckPreviewTimeout() {
    if (isPreviewingModel) {
        uint32_t now = daisy::System::GetNow();
        if (now - previewStartTime > PREVIEW_TIMEOUT_MS) {
            // Revert to current model (validated)
            if (IsValidModelIndex(currentSettings.modelIndex)) {
                previewModelIndex = currentSettings.modelIndex;
            } else {
                previewModelIndex = 0;
            }
            isPreviewingModel = false;
        }
    }
}

void HandleFootswitches() {
    // FS1 (Left): Reverb on/off
    if (hw.switches[0].RisingEdge()) {
        currentSettings.reverbEnabled = !currentSettings.reverbEnabled;
        hw.SetLed(0, currentSettings.reverbEnabled ? 1.0f : 0.0f);
        hw.UpdateLeds();
        UpdateBypassRelay();  // Check if bypass state should change
        SaveSettingsDebounced();
    }
    
    // FS2 (Right): NAM on/off
    if (hw.switches[1].RisingEdge()) {
        currentSettings.namEnabled = !currentSettings.namEnabled;
        hw.SetLed(1, currentSettings.namEnabled ? 1.0f : 0.0f);
        hw.UpdateLeds();
        UpdateBypassRelay();  // Check if bypass state should change
        SaveSettingsDebounced();
    }
}

void HandleKnobs() {
    bool changed = false;
    
    // Knob 0: Input gain
    float knob0 = hw.knobs[0].Value();
    if (std::abs(knob0 - currentSettings.inputGain) > 0.01f) {
        currentSettings.inputGain = knob0;
        inputGain.SetGain(knob0 - 0.5f);
        changed = true;
    }
    
    // Knob 1: Output volume
    float knob1 = hw.knobs[1].Value();
    if (std::abs(knob1 - currentSettings.outputVolume) > 0.01f) {
        currentSettings.outputVolume = knob1;
        outputVolume.SetGain(knob1 - 0.5f);
        changed = true;
    }
    
    // Knob 2: Reverb mix
    float knob2 = hw.knobs[2].Value();
    if (std::abs(knob2 - currentSettings.reverbMix) > 0.01f) {
        currentSettings.reverbMix = knob2;
        reverbProcessor.setMix(knob2);
        changed = true;
    }
    
    // Knob 3: Bass
    float knob3 = hw.knobs[3].Value();
    if (std::abs(knob3 - currentSettings.bass) > 0.01f) {
        currentSettings.bass = knob3;
        eq.SetBass((knob3 - 0.5f) * 2.0f);
        changed = true;
    }
    
    // Knob 4: Mid
    float knob4 = hw.knobs[4].Value();
    if (std::abs(knob4 - currentSettings.mid) > 0.01f) {
        currentSettings.mid = knob4;
        eq.SetMid((knob4 - 0.5f) * 2.0f);
        changed = true;
    }
    
    // Knob 5: Treble
    float knob5 = hw.knobs[5].Value();
    if (std::abs(knob5 - currentSettings.treble) > 0.01f) {
        currentSettings.treble = knob5;
        eq.SetTreble((knob5 - 0.5f) * 2.0f);
        changed = true;
    }
    
    if (changed) {
        SaveSettingsDebounced();
    }
}

void CheckSettingsSave() {
    if (settingsDirty) {
        uint32_t now = daisy::System::GetNow();
        if (now - lastSettingsChange > SAVE_DELAY_MS) {
            settings.Save();
            settingsDirty = false;
        }
    }
}

int main(void) {
    hw.Init(48, true);
    
    // Initialize settings
    settings.Init(hw.seed.qspi);
    settings.ValidateSettings(NAM_MODEL_COUNT);  // Validate loaded settings
    currentSettings = settings.GetSettings();
    
    // Initialize DSP
    hw.display.Fill(false);
    hw.display.SetCursor(0, 0);
    hw.display.WriteString("Initializing...", Font_7x10, true);
    hw.display.Update();
    
    inputGain.Init();
    outputVolume.Init();
    eq.Init(hw.AudioSampleRate());
    reverbProcessor.init(hw.AudioSampleRate());
    
    // Apply saved settings
    inputGain.SetGain(currentSettings.inputGain - 0.5f);
    outputVolume.SetGain(currentSettings.outputVolume - 0.5f);
    reverbProcessor.setMix(currentSettings.reverbMix);
    eq.SetBass((currentSettings.bass - 0.5f) * 2.0f);
    eq.SetMid((currentSettings.mid - 0.5f) * 2.0f);
    eq.SetTreble((currentSettings.treble - 0.5f) * 2.0f);
    
    // Load last used model
    if (NAM_MODEL_COUNT == 0) {
        hw.display.Fill(false);
        hw.display.SetCursor(0, 0);
        hw.display.WriteString("No Models!", Font_7x10, true);
        hw.display.SetCursor(0, 12);
        hw.display.WriteString("Use nam_to_header.py", Font_6x8, true);
        hw.display.Update();
        // Continue without model (audio will pass through)
    }
    
    previewModelIndex = IsValidModelIndex(currentSettings.modelIndex) 
        ? currentSettings.modelIndex 
        : 0;
    
    if (NAM_MODEL_COUNT > 0) {
        LoadModel(previewModelIndex);
    }
    
    // Set initial LED states
    hw.SetLed(0, currentSettings.reverbEnabled ? 1.0f : 0.0f);
    hw.SetLed(1, currentSettings.namEnabled ? 1.0f : 0.0f);
    hw.UpdateLeds();
    
    // Set initial bypass state based on loaded settings
    currentBypassState = !currentSettings.namEnabled && !currentSettings.reverbEnabled;
    hw.SetAudioBypass(currentBypassState);
    
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    
    while(1) {
        hw.ProcessAllControls();
        
        HandleEncoderMovement();
        HandleEncoderClick();
        CheckPreviewTimeout();
        
        HandleFootswitches();
        HandleKnobs();
        
        CheckSettingsSave();
        
        // Throttle display updates
        uint32_t now = daisy::System::GetNow();
        if (now - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL_MS) {
            UpdateDisplay();
            lastDisplayUpdate = now;
        }
    }
}
