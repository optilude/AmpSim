#pragma once
#ifndef GUITAR_PEDAL_125B_H
#define GUITAR_PEDAL_125B_H

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include <vector>

using namespace daisy;

/** Typedef the OledDisplay to make syntax cleaner below
 *  This is a 4Wire SPI Transport controlling an 128x64 sized SSD1306
 */
using MyOledDisplay = OledDisplay<SSD130x4WireSpi128x64Driver>;

namespace multifs {

/** Special Function Types */
enum SpecialFunctionType {
    Bypass,
    Alternate,
    SpecialFunctionType_LAST,
};

// Meta data for mapping preferred switch ids to special function types
struct PreferredSwitchMetaData {
    SpecialFunctionType sfType;
    int switchMapping;
};

class GuitarPedal125B {
  public:
    GuitarPedal125B();
    ~GuitarPedal125B();

    void Init(size_t blockSize = 48, bool boost = false);

    void DelayMs(size_t del);
    void StartAudio(AudioHandle::InterleavingAudioCallback cb);
    void StartAudio(AudioHandle::AudioCallback cb);
    void StopAudio();
    void SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate);
    float AudioSampleRate();
    void SetAudioBlockSize(size_t size);
    size_t AudioBlockSize();
    float AudioCallbackRate();

    void StartAdc();
    void StopAdc();
    void ProcessAnalogControls();
    void ProcessDigitalControls();
    inline void ProcessAllControls() {
        ProcessAnalogControls();
        ProcessDigitalControls();
    }

    int GetKnobCount();
    float GetKnobValue(int knobID);
    int GetSwitchCount();
    int GetEncoderCount();
    int GetLedCount();

    int GetPreferredSwitchIDForSpecialFunctionType(SpecialFunctionType sfType);

    void SetLed(int ledID, float bright);
    void UpdateLeds();

    void SetAudioBypass(bool enabled);
    void SetAudioMute(bool enabled);

    bool SupportsStereo();
    bool SupportsMidi();
    bool SupportsDisplay();
    bool SupportsEncoder();
    bool SupportsTrueBypass();

    DaisySeed seed;

    std::vector<AnalogControl> knobs;
    std::vector<Switch> switches;
    std::vector<Encoder> encoders;
    std::vector<Led> leds;

    MidiUartHandler midi;
    MyOledDisplay display;
    GPIO audioBypassTrigger;
    GPIO audioMuteTrigger;

  protected:
    bool m_supportsStereo;
    bool m_supportsMidi;
    bool m_supportsDisplay;
    bool m_supportsEncoder;
    bool m_supportsTrueBypass;
    bool m_audioBypass;
    bool m_audioMute;
    int m_switchMetaDataParamCount;
    const PreferredSwitchMetaData *m_switchMetaData;

    void SetHidUpdateRates();

    void InitKnobs(int count, Pin pins[]);
    void InitSwitches(int count, Pin pins[]);
    void InitEncoders(int count, Pin pins[][3]);
    void InitLeds(int count, Pin pins[]);
    void InitMidi(Pin rxPin, Pin txPin);
    void InitDisplay(Pin dcPin, Pin resetPin);
    void InitTrueBypass(Pin relayPin, Pin mutePin);

    inline uint16_t *adc_ptr(const uint8_t chn) { return seed.adc.GetPtr(chn); }
};

} // namespace multifs
#endif
