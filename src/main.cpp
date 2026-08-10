// AmpSim - Guitar Amp Simulator based on Daisy Seed
// Minimal firmware to test LCD display and input handling

#include "guitar_pedal_125b.h"
#include <string.h>

using namespace multifs;

GuitarPedal125B hw;

int encoderPosition = 0;
int knobValues[6] = {0, 0, 0, 0, 0, 0};
bool footswitchStates[2] = {false, false};

void UpdateDisplay() {
    hw.display.Fill(false);
    
    hw.display.SetCursor(0, 0);
    hw.display.WriteString("AmpSim Test", Font_7x10, true);
    
    hw.display.SetCursor(0, 12);
    char line[32];
    sprintf(line, "Encoder: %d", encoderPosition);
    hw.display.WriteString(line, Font_6x8, true);
    
    hw.display.SetCursor(0, 22);
    hw.display.WriteString("Knobs:", Font_6x8, true);
    
    for (int i = 0; i < 6; i++) {
        int y = 32 + (i / 3) * 10;
        int x = (i % 3) * 43;
        hw.display.SetCursor(x, y);
        sprintf(line, "K%d:%3d", i + 1, knobValues[i]);
        hw.display.WriteString(line, Font_6x8, true);
    }
    
    hw.display.SetCursor(0, 54);
    sprintf(line, "FS1:%c FS2:%c", 
            footswitchStates[0] ? 'X' : '_',
            footswitchStates[1] ? 'X' : '_');
    hw.display.WriteString(line, Font_7x10, true);
    
    hw.display.Update();
}

void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t size) {
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();
    
    for (size_t i = 0; i < size; i++) {
        out[0][i] = in[0][i];
        out[1][i] = in[0][i];
    }
}

int main(void) {
    hw.Init(48, true);
    
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    
    hw.display.Fill(false);
    hw.display.Update();
    hw.DelayMs(500);
    
    hw.SetLed(0, 1.0f);
    hw.SetLed(1, 0.0f);
    hw.UpdateLeds();
    hw.DelayMs(500);
    
    hw.SetLed(0, 0.0f);
    hw.SetLed(1, 1.0f);
    hw.UpdateLeds();
    hw.DelayMs(500);
    
    while(1) {
        hw.ProcessAllControls();
        
        int32_t encoderInc = hw.encoders[0].Increment();
        if (encoderInc != 0) {
            encoderPosition += encoderInc;
            
            hw.SetLed(0, encoderInc > 0 ? 1.0f : 0.0f);
            hw.SetLed(1, encoderInc < 0 ? 1.0f : 0.0f);
        }
        
        if (hw.encoders[0].RisingEdge()) {
            hw.SetLed(0, 1.0f);
            hw.SetLed(1, 1.0f);
            encoderPosition = 0;
        }
        
        for (int i = 0; i < 6; i++) {
            float value = hw.GetKnobValue(i);
            knobValues[i] = (int)(value * 100.0f);
        }
        
        for (int i = 0; i < 2; i++) {
            if (hw.switches[i].RisingEdge()) {
                footswitchStates[i] = !footswitchStates[i];
                hw.SetLed(i, footswitchStates[i] ? 1.0f : 0.0f);
            }
        }
        
        hw.UpdateLeds();
        UpdateDisplay();
        
        hw.DelayMs(50);
    }
}
