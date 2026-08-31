#include "guitar_pedal_125b.h"

#include <cstring>

using namespace daisy;
using namespace multifs;

static const int s_switchParamCount = 2;
static const PreferredSwitchMetaData s_switchMetaData[s_switchParamCount] = {
    {sfType : SpecialFunctionType::Bypass, switchMapping : 0},
    {sfType : SpecialFunctionType::Alternate, switchMapping : 1}
};

GuitarPedal125B::GuitarPedal125B()
    : m_supportsStereo(false), m_supportsMidi(false), m_supportsDisplay(false),
      m_supportsEncoder(false), m_supportsTrueBypass(false), m_audioBypass(false),
      m_audioMute(false), m_switchMetaDataParamCount(0) {
    m_switchMetaData = nullptr;
}

GuitarPedal125B::~GuitarPedal125B() {}

void GuitarPedal125B::Init(size_t blockSize, bool boost) {
    seed.Configure();
    seed.Init(boost);

    m_supportsStereo = true;

    Pin knobPins[] = {seed::D15, seed::D16, seed::D17, seed::D18, seed::D19, seed::D20};
    InitKnobs(6, knobPins);

    Pin switchPins[] = {seed::D6, seed::D5};
    InitSwitches(2, switchPins);

    Pin encoderPins[][3] = {{seed::D3, seed::D2, seed::D4}};
    InitEncoders(1, encoderPins);

    Pin ledPins[] = {seed::D22, seed::D23};
    InitLeds(2, ledPins);

    InitMidi(seed::D30, seed::D29);
    InitDisplay(seed::D9, seed::D11);
    InitTrueBypass(seed::D1, seed::D12);

    m_switchMetaDataParamCount = s_switchParamCount;
    m_switchMetaData = s_switchMetaData;

    SetAudioBlockSize(blockSize);
}

void GuitarPedal125B::DelayMs(size_t del) { seed.DelayMs(del); }

void GuitarPedal125B::SetHidUpdateRates() {
    if (!knobs.empty()) {
        for (int i = 0; i < GetKnobCount(); i++) {
            knobs[i].SetSampleRate(AudioCallbackRate());
        }
    }

    if (!leds.empty()) {
        for (int i = 0; i < GetLedCount(); i++) {
            leds[i].SetSampleRate(AudioCallbackRate());
        }
    }
}

void GuitarPedal125B::StartAudio(AudioHandle::InterleavingAudioCallback cb) { seed.StartAudio(cb); }

void GuitarPedal125B::StartAudio(AudioHandle::AudioCallback cb) { seed.StartAudio(cb); }

void GuitarPedal125B::StopAudio() { seed.StopAudio(); }

void GuitarPedal125B::SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate) {
    seed.SetAudioSampleRate(samplerate);
    SetHidUpdateRates();
}

float GuitarPedal125B::AudioSampleRate() { return seed.AudioSampleRate(); }

void GuitarPedal125B::SetAudioBlockSize(size_t size) {
    seed.SetAudioBlockSize(size);
    SetHidUpdateRates();
}

size_t GuitarPedal125B::AudioBlockSize() { return seed.AudioBlockSize(); }

float GuitarPedal125B::AudioCallbackRate() { return seed.AudioCallbackRate(); }

void GuitarPedal125B::StartAdc() { seed.adc.Start(); }

void GuitarPedal125B::StopAdc() { seed.adc.Stop(); }

void GuitarPedal125B::ProcessAnalogControls() {
    if (!knobs.empty()) {
        for (int i = 0; i < GetKnobCount(); i++) {
            knobs[i].Process();
        }
    }
}

void GuitarPedal125B::ProcessDigitalControls() {
    if (!switches.empty()) {
        for (int i = 0; i < GetSwitchCount(); i++) {
            switches[i].Debounce();
        }
    }

    if (!encoders.empty()) {
        for (int i = 0; i < GetEncoderCount(); i++) {
            encoders[i].Debounce();
        }
    }
}

int GuitarPedal125B::GetKnobCount() { return knobs.size(); }

float GuitarPedal125B::GetKnobValue(int knobID) {
    if (!knobs.empty() && knobID >= 0 && knobID < GetKnobCount()) {
        return knobs[knobID].Value();
    }
    return 0.0f;
}

int GuitarPedal125B::GetSwitchCount() { return switches.size(); }

int GuitarPedal125B::GetEncoderCount() { return encoders.size(); }

int GuitarPedal125B::GetLedCount() { return leds.size(); }

int GuitarPedal125B::GetPreferredSwitchIDForSpecialFunctionType(SpecialFunctionType sfType) {
    if (GetSwitchCount() == 0 || m_switchMetaDataParamCount == 0 || m_switchMetaData == nullptr) {
        return -1;
    }

    for (int i = 0; i < m_switchMetaDataParamCount; i++) {
        if (m_switchMetaData[i].sfType == sfType) {
            if (m_switchMetaData[i].switchMapping < GetSwitchCount()) {
                return m_switchMetaData[i].switchMapping;
            }
        }
    }

    return -1;
}

void GuitarPedal125B::SetLed(int ledID, float bright) {
    if (!leds.empty() && ledID >= 0 && ledID < GetLedCount()) {
        leds[ledID].Set(bright);
    }
}

void GuitarPedal125B::UpdateLeds() {
    if (!leds.empty()) {
        for (int i = 0; i < GetLedCount(); i++) {
            leds[i].Update();
        }
    }
}

void GuitarPedal125B::SetAudioBypass(bool enabled) {
    m_audioBypass = enabled;
    audioBypassTrigger.Write(!m_audioBypass);
}

void GuitarPedal125B::SetAudioMute(bool enabled) {
    m_audioMute = enabled;
    audioMuteTrigger.Write(m_audioMute);
}

bool GuitarPedal125B::SupportsStereo() { return m_supportsStereo; }

bool GuitarPedal125B::SupportsMidi() { return m_supportsMidi; }

bool GuitarPedal125B::SupportsDisplay() { return m_supportsDisplay; }

bool GuitarPedal125B::SupportsEncoder() { return m_supportsEncoder; }

bool GuitarPedal125B::SupportsTrueBypass() { return m_supportsTrueBypass; }

void GuitarPedal125B::InitKnobs(int count, Pin pins[]) {
    AdcChannelConfig cfg[count];

    for (int i = 0; i < count; i++) {
        cfg[i].InitSingle(pins[i]);
    }

    seed.adc.Init(cfg, count);

    for (int i = 0; i < count; i++) {
        AnalogControl myKnob;
        myKnob.Init(seed.adc.GetPtr(i), AudioCallbackRate());
        knobs.push_back(myKnob);
    }
}

void GuitarPedal125B::InitSwitches(int count, Pin pins[]) {
    for (int i = 0; i < count; i++) {
        Switch mySwitch;
        mySwitch.Init(pins[i]);
        switches.push_back(mySwitch);
    }
}

void GuitarPedal125B::InitEncoders(int count, Pin pins[][3]) {
    for (int i = 0; i < count; i++) {
        Encoder myEncoder;
        myEncoder.Init(pins[i][0], pins[i][1], pins[i][2]);
        encoders.push_back(myEncoder);
    }

    if (count > 0) {
        m_supportsEncoder = true;
    }
}

void GuitarPedal125B::InitLeds(int count, Pin pins[]) {
    for (int i = 0; i < count; i++) {
        // daisy::Led::Init() sets bright_ and pwm_cnt_ but leaves pwm_ -- the
        // software-PWM sawtooth phase -- untouched. Update() then evaluates
        // `bright_ > pwm_`, so a garbage phase outside [0,1) means the LED
        // never lights and the `if(pwm_ > 1) pwm_ -= 1` wrap never recovers it.
        // Zero the object first; Init() writes every other member.
        Led newLed;
        std::memset(&newLed, 0, sizeof newLed);
        newLed.Init(pins[i], false, AudioCallbackRate());
        leds.push_back(newLed);
    }
}

void GuitarPedal125B::InitMidi(Pin rxPin, Pin txPin) {
    MidiUartHandler::Config midi_config;
    midi_config.transport_config.rx = rxPin;
    midi_config.transport_config.tx = txPin;
    midi.Init(midi_config);

    m_supportsMidi = true;
}

void GuitarPedal125B::InitDisplay(Pin dcPin, Pin resetPin) {
    MyOledDisplay::Config disp_cfg;
    disp_cfg.driver_config.transport_config.pin_config.dc = dcPin;
    disp_cfg.driver_config.transport_config.pin_config.reset = resetPin;
    display.Init(disp_cfg);

    m_supportsDisplay = true;
}

void GuitarPedal125B::InitTrueBypass(Pin relayPin, Pin mutePin) {
    audioBypassTrigger.Init(relayPin, GPIO::Mode::OUTPUT);
    SetAudioBypass(true);

    audioMuteTrigger.Init(mutePin, GPIO::Mode::OUTPUT);
    SetAudioMute(false);

    m_supportsTrueBypass = true;
}
