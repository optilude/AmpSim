// AmpSim - Guitar Amp Simulator based on Daisy Seed
// NAM A2 processing with Dattorro plate reverb and full control scheme

#include "guitar_pedal_125b.h"
#include "nam_processor.h"
#include "reverb_processor.h"
#include "model_data.h"
#include "settings.h"
#include "gain_stage.h"
#include "guitar_eq.h"
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
const uint32_t PREVIEW_TIMEOUT_MS = 10000;

// Save debouncing
uint32_t lastSettingsChange = 0;
bool settingsDirty = false;
const uint32_t SAVE_DELAY_MS = 2000;

void SaveSettingsDebounced() {
    settingsDirty = true;
    lastSettingsChange = daisy::System::GetNow();
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

void LoadModel(int index) {
    hw.display.Fill(false);
    hw.display.SetCursor(0, 0);
    hw.display.WriteString("Loading...", Font_7x10, true);
    hw.display.Update();
    
    namProcessor.loadModel(nam_models[index].model_json);
    currentSettings.modelIndex = index;
    SaveSettingsDebounced();
}

void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t size) {
    hw.ProcessAnalogControls();
    
    for (size_t i = 0; i < size; i++) {
        float input = in[0][i];
        float output = input;
        
        if (currentSettings.namEnabled && namProcessor.isModelLoaded()) {
            // Input gain
            output = inputGain.Process(input);
            
            // NAM processing
            namProcessor.process(&output, &output, 1);
            
            // EQ
            output = eq.Process(output);
            
            // Reverb
            if (currentSettings.reverbEnabled) {
                float reverbL, reverbR;
                reverbProcessor.process(output, &reverbL, &reverbR);
                
                float dryMix = reverbProcessor.getDryMix();
                float wetMix = reverbProcessor.getWetMix();
                
                output = output * dryMix + reverbL * wetMix;
                out[0][i] = outputVolume.Process(output);
                out[1][i] = outputVolume.Process(output * dryMix + reverbR * wetMix);
            } else {
                // No reverb
                out[0][i] = outputVolume.Process(output);
                out[1][i] = out[0][i];
            }
        } else {
            // NAM bypassed - should be handled by true bypass relay
            out[0][i] = input;
            out[1][i] = input;
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
            // Revert to current model
            previewModelIndex = currentSettings.modelIndex;
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
        SaveSettingsDebounced();
    }
    
    // FS2 (Right): NAM on/off (controls true bypass)
    if (hw.switches[1].RisingEdge()) {
        currentSettings.namEnabled = !currentSettings.namEnabled;
        hw.SetLed(1, currentSettings.namEnabled ? 1.0f : 0.0f);
        hw.UpdateLeds();
        
        // Mute before switching relay
        hw.SetAudioMute(true);
        hw.DelayMs(10);
        
        // Switch relay
        hw.SetAudioBypass(!currentSettings.namEnabled);
        
        // Unmute
        hw.DelayMs(10);
        hw.SetAudioMute(false);
        
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
    previewModelIndex = currentSettings.modelIndex;
    if (NAM_MODEL_COUNT > 0) {
        LoadModel(currentSettings.modelIndex);
    }
    
    // Set initial LED states
    hw.SetLed(0, currentSettings.reverbEnabled ? 1.0f : 0.0f);
    hw.SetLed(1, currentSettings.namEnabled ? 1.0f : 0.0f);
    hw.UpdateLeds();
    
    // Set initial bypass state
    hw.SetAudioBypass(!currentSettings.namEnabled);
    
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
        UpdateDisplay();
        
        hw.DelayMs(10);
    }
}
