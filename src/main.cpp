// AmpSim - Guitar Amp Simulator based on Daisy Seed
// NAM A2 processing with Dattorro plate reverb

#include "guitar_pedal_125b.h"
#include "nam_processor.h"
#include "reverb_processor.h"
#include "model_data.h"
#include <string.h>

using namespace multifs;

GuitarPedal125B hw;

NAMProcessor namProcessor;
ReverbProcessor reverbProcessor;

int currentModelIndex = 0;
bool namEnabled = false;
bool reverbEnabled = true;

// Reverb parameters (can be controlled by knobs)
float reverbMix = 0.3f;
float reverbDecay = 0.8f;

void UpdateDisplay() {
    hw.display.Fill(false);
    
    hw.display.SetCursor(0, 0);
    hw.display.WriteString("AmpSim NAM A2", Font_7x10, true);
    
    hw.display.SetCursor(0, 12);
    char line[32];
    
    if (namProcessor.isModelLoaded()) {
        sprintf(line, "Model: %s", nam_models[currentModelIndex].model_name);
        hw.display.WriteString(line, Font_6x8, true);
        
        hw.display.SetCursor(0, 22);
        sprintf(line, "Variant: %s", nam_models[currentModelIndex].variant_name);
        hw.display.WriteString(line, Font_6x8, true);
        
        hw.display.SetCursor(0, 32);
        sprintf(line, "NAM: %s  REV: %s", 
                namEnabled ? "ON" : "OFF",
                reverbEnabled ? "ON" : "OFF");
        hw.display.WriteString(line, Font_6x8, true);
    } else {
        hw.display.WriteString("No model loaded", Font_6x8, true);
    }
    
    hw.display.SetCursor(0, 44);
    sprintf(line, "Mix: %.1f  Decay: %.1f", reverbMix, reverbDecay);
    hw.display.WriteString(line, Font_6x8, true);
    
    hw.display.SetCursor(0, 54);
    sprintf(line, "FS1:NAM FS2:Model FS3:Rev");
    hw.display.WriteString(line, Font_6x8, true);
    
    hw.display.Update();
}

void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t size) {
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();
    
    for (size_t i = 0; i < size; i++) {
        float input = in[0][i];
        float namOutput = input;
        
        // Process through NAM model
        if (namEnabled && namProcessor.isModelLoaded()) {
            namProcessor.process(&input, &namOutput, 1);
        }
        
        // Process through reverb (mono in, stereo out)
        float reverbL = 0.0f;
        float reverbR = 0.0f;
        
        if (reverbEnabled) {
            reverbProcessor.process(namOutput, &reverbL, &reverbR);
            
            // Mix dry and wet signals
            float dryMix = reverbProcessor.getDryMix();
            float wetMix = reverbProcessor.getWetMix();
            
            out[0][i] = namOutput * dryMix + reverbL * wetMix;
            out[1][i] = namOutput * dryMix + reverbR * wetMix;
        } else {
            // No reverb - just output NAM signal (mono to stereo)
            out[0][i] = namOutput;
            out[1][i] = namOutput;
        }
    }
}

int main(void) {
    hw.Init(48, true);
    
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    
    hw.display.Fill(false);
    hw.display.Update();
    hw.DelayMs(500);
    
    // Initialize reverb processor
    hw.display.SetCursor(0, 0);
    hw.display.WriteString("Initializing...", Font_7x10, true);
    hw.display.Update();
    
    reverbProcessor.init(hw.AudioSampleRate());
    reverbProcessor.setMix(reverbMix);
    reverbProcessor.setDecay(reverbDecay);
    
    // Load first NAM model
    if (NAM_MODEL_COUNT > 0) {
        hw.display.SetCursor(0, 0);
        hw.display.WriteString("Loading NAM...", Font_7x10, true);
        hw.display.Update();
        
        bool loaded = namProcessor.loadModel(nam_models[0].model_json);
        
        if (loaded) {
            hw.SetLed(0, 1.0f);
            hw.SetLed(1, 0.0f);
            namEnabled = true;
        } else {
            hw.SetLed(0, 0.0f);
            hw.SetLed(1, 1.0f);
        }
        hw.UpdateLeds();
        hw.DelayMs(500);
    }
    
    while(1) {
        hw.ProcessAllControls();
        
        // Footswitch 1: Toggle NAM on/off
        if (hw.switches[0].RisingEdge()) {
            namEnabled = !namEnabled;
            hw.SetLed(0, namEnabled ? 1.0f : 0.0f);
            hw.UpdateLeds();
        }
        
        // Footswitch 2: Cycle through NAM models
        if (hw.switches[1].RisingEdge()) {
            currentModelIndex = (currentModelIndex + 1) % NAM_MODEL_COUNT;
            
            hw.display.Fill(false);
            hw.display.SetCursor(0, 0);
            hw.display.WriteString("Loading...", Font_7x10, true);
            hw.display.Update();
            
            namProcessor.loadModel(nam_models[currentModelIndex].model_json);
            hw.DelayMs(100);
        }
        
        // Footswitch 3: Toggle reverb on/off
        if (hw.switches[2].RisingEdge()) {
            reverbEnabled = !reverbEnabled;
            hw.SetLed(1, reverbEnabled ? 1.0f : 0.0f);
            hw.UpdateLeds();
        }
        
        // Knob 1: Reverb mix
        float knob1 = hw.knobs[0].Value();
        reverbMix = knob1;
        reverbProcessor.setMix(reverbMix);
        
        // Knob 2: Reverb decay
        float knob2 = hw.knobs[1].Value();
        reverbDecay = 0.5f + knob2 * 0.5f;  // Range: 0.5 to 1.0
        reverbProcessor.setDecay(reverbDecay);
        
        // Knob 3: Reverb tone (high cut filter)
        float knob3 = hw.knobs[2].Value();
        reverbProcessor.setTone(knob3);
        
        // Knob 4: Output level (master volume)
        // (This would be applied in the audio callback if needed)
        
        UpdateDisplay();
        hw.DelayMs(10);
    }
}
