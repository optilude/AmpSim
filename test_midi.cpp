#include "midi_control.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

using namespace midi_control;

static bool Near(float actual, float expected) {
    return std::abs(actual - expected) < 0.00001f;
}

int main() {
    assert(AcceptsChannel(0, 0));
    assert(!AcceptsChannel(0, 1));
    assert(AcceptsChannel(15, 15));
    assert(AcceptsChannel(kOmniChannel, 0));
    assert(AcceptsChannel(kOmniChannel, 15));

    Action action = Translate(MessageType::ProgramChange, 0, 0, 0, 0, 29);
    assert(action.type == ActionType::SelectModel && action.modelIndex == 0);
    action = Translate(MessageType::ProgramChange, 0, 28, 0, 0, 29);
    assert(action.type == ActionType::SelectModel && action.modelIndex == 28);
    assert(Translate(MessageType::ProgramChange, 0, 29, 0, 0, 29).type
           == ActionType::None);
    assert(Translate(MessageType::ProgramChange, 1, 0, 0, 0, 29).type
           == ActionType::None);
       action = Translate(MessageType::ProgramChange, 0, 127, 0, 0, 128);
       assert(action.type == ActionType::SelectModel && action.modelIndex == 127);

       const uint8_t controllers[] = {
              kInputGainCc, kOutputVolumeCc, kReverbMixCc, kBassCc, kMidCc, kTrebleCc,
       };
       for (size_t i = 0; i < kParameterCount; ++i) {
              action = Translate(MessageType::ControlChange, 0, controllers[i], 0, 0, 29);
              assert(action.type == ActionType::SetParameter);
              assert(action.parameter == static_cast<Parameter>(i));
              assert(Near(action.normalizedValue, 0.0f));
              action = Translate(MessageType::ControlChange, 0, controllers[i], 64, 0, 29);
              assert(Near(action.normalizedValue, 0.5f));
              action = Translate(MessageType::ControlChange, 0, controllers[i], 127, 0, 29);
              assert(Near(action.normalizedValue, 1.0f));
       }
    assert(Translate(MessageType::ControlChange, 0, 22, 127, 0, 29).type
           == ActionType::None);

    action = Translate(MessageType::ControlChange, 0, kReverbEnableCc, 63, 0, 29);
    assert(action.type == ActionType::SetReverbEnabled && !action.enabled);
    action = Translate(MessageType::ControlChange, 0, kReverbEnableCc, 64, 0, 29);
    assert(action.type == ActionType::SetReverbEnabled && action.enabled);

    PendingState pending;
    pending.SetParameter(Parameter::Bass, 0.25f);
    pending.SetParameter(Parameter::Bass, 0.75f);
    pending.SetReverbEnabled(true);
    PendingSnapshot snapshot = pending.Consume();
    assert(snapshot.parameterPending[static_cast<size_t>(Parameter::Bass)]);
    assert(Near(snapshot.parameterValues[static_cast<size_t>(Parameter::Bass)], 0.75f));
    assert(snapshot.reverbPending && snapshot.reverbEnabled);
    snapshot = pending.Consume();
    assert(!snapshot.parameterPending[static_cast<size_t>(Parameter::Bass)]);
    assert(!snapshot.reverbPending);

    KnobTakeover takeover;
    takeover.SetMidiControlled(0.4f);
    assert(!takeover.ShouldApplyPhysical(0.405f, 0.01f));
    assert(takeover.IsMidiControlled());
    assert(takeover.ShouldApplyPhysical(0.42f, 0.01f));
    assert(!takeover.IsMidiControlled());

    RawThruBuffer<16> thru;
    const uint8_t stream[] = {
              0xb0, 16, 127, 17, 64, 0xf8, 0xb0, 123, 0, 0xc1, 3,
    };
    assert(thru.Push(stream, sizeof stream));
    uint8_t output[16] = {};
    const size_t outputSize = thru.Pop(output, sizeof output);
    assert(outputSize == sizeof stream);
    for (size_t i = 0; i < outputSize; ++i) assert(output[i] == stream[i]);

       uint8_t longSysex[140];
       longSysex[0] = 0xf0;
       for (size_t i = 1; i < sizeof longSysex - 1; ++i)
              longSysex[i] = static_cast<uint8_t>(i & 0x7f);
       longSysex[sizeof longSysex - 1] = 0xf7;
       RawThruBuffer<160> sysexThru;
       assert(sysexThru.Push(longSysex, sizeof longSysex));
       uint8_t sysexOutput[140] = {};
       assert(sysexThru.Pop(sysexOutput, sizeof sysexOutput) == sizeof longSysex);
       for (size_t i = 0; i < sizeof longSysex; ++i)
              assert(sysexOutput[i] == longSysex[i]);

    const uint8_t overflow[] = {0, 1, 2, 3, 4};
    RawThruBuffer<4> smallThru;
    assert(!smallThru.Push(overflow, sizeof overflow));
    assert(smallThru.Size() == 4 && smallThru.DroppedBytes() == 1);
    assert(smallThru.Pop(output, sizeof output) == 4);
    for (size_t i = 0; i < 4; ++i) assert(output[i] == i);

    std::cout << "MIDI control tests passed\n";
    return 0;
}