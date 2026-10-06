#include "midi_control.h"

#include <cmath>

namespace midi_control {

bool AcceptsChannel(uint8_t configuredChannel, int messageChannel) {
    return configuredChannel == kOmniChannel
        || (configuredChannel < kOmniChannel && messageChannel == configuredChannel);
}

float ValueToNormalized(uint8_t value) {
    if (value <= 64) return static_cast<float>(value) / 128.0f;
    return 0.5f + static_cast<float>(value - 64) / 126.0f;
}

static bool ParameterForController(uint8_t controller, Parameter& parameter) {
    switch (controller) {
        case kInputGainCc: parameter = Parameter::InputGain; return true;
        case kOutputVolumeCc: parameter = Parameter::OutputVolume; return true;
        case kReverbMixCc: parameter = Parameter::ReverbMix; return true;
        case kBassCc: parameter = Parameter::Bass; return true;
        case kMidCc: parameter = Parameter::Mid; return true;
        case kTrebleCc: parameter = Parameter::Treble; return true;
        default: return false;
    }
}

Action Translate(MessageType type,
                 int channel,
                 uint8_t data0,
                 uint8_t data1,
                 uint8_t configuredChannel,
                 int modelCount) {
    Action action;
    if (!AcceptsChannel(configuredChannel, channel)) return action;

    if (type == MessageType::ProgramChange) {
        if (data0 < modelCount) {
            action.type = ActionType::SelectModel;
            action.modelIndex = data0;
            action.enabled = true;
        }
        return action;
    }

    if (type != MessageType::ControlChange) return action;

    Parameter parameter;
    if (ParameterForController(data0, parameter)) {
        action.type = ActionType::SetParameter;
        action.parameter = parameter;
        action.normalizedValue = ValueToNormalized(data1);
    } else if (data0 == kReverbEnableCc) {
        action.type = ActionType::SetReverbEnabled;
        action.enabled = data1 >= 64;
    }
    return action;
}

void PendingState::SetParameter(Parameter parameter, float normalizedValue) {
    const size_t index = static_cast<size_t>(parameter);
    state_.parameterPending[index] = true;
    state_.parameterValues[index] = normalizedValue;
}

void PendingState::SetNamEnabled(bool enabled) {
    state_.namPending = true;
    state_.namEnabled = enabled;
}

void PendingState::SetReverbEnabled(bool enabled) {
    state_.reverbPending = true;
    state_.reverbEnabled = enabled;
}

PendingSnapshot PendingState::Consume() {
    const PendingSnapshot result = state_;
    state_ = PendingSnapshot{};
    return result;
}

bool KnobTakeover::ShouldApplyPhysical(float physicalPosition, float deadband) {
    if (!midiControlled_) return true;
    if (std::abs(physicalPosition - physicalAnchor_) <= deadband) return false;
    midiControlled_ = false;
    return true;
}

} // namespace midi_control