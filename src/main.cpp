// AmpSim - Guitar Amp Simulator based on Daisy Seed
// NAM A2 processing implementation

#include "guitar_pedal_125b.h"
#include "nam_processor.h"
#include "model_data.h"
#include <string.h>

using namespace multifs;

GuitarPedal125B hw;

NAMProcessor namProcessor;
int currentModelIndex = 0;
bool namEnabled = false;

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
        sprintf(line, "NAM: %s", namEnabled ? "ON" : "OFF");
        hw.display.WriteString(line, Font_6x8, true);
    } else {
        hw.display.WriteString("No model loaded", Font_6x8, true);
    }
    
    hw.display.SetCursor(0, 54);
    sprintf(line, "FS1: Toggle NAM");
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
        float output = input;
        
        if (namEnabled && namProcessor.isModelLoaded()) {
            namProcessor.process(&input, &output, 1);
        }
        
        out[0][i] = output;
        out[1][i] = output;
    }
}

int main(void) {
    hw.Init(48, true);
    
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    
    hw.display.Fill(false);
    hw.display.Update();
    hw.DelayMs(500);
    
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
            hw.SetLed(1, namEnabled ? 0.0f : 1.0f);
        }
        
        // Footswitch 2: Cycle through models
        if (hw.switches[1].RisingEdge()) {
            currentModelIndex = (currentModelIndex + 1) % NAM_MODEL_COUNT;
            namProcessor.loadModel(nam_models[currentModelIndex].model_json);
        }
        
        hw.UpdateLeds();
        UpdateDisplay();
        
        hw.DelayMs(50);
    }
}
