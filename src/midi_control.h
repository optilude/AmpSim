#pragma once

#include <cstddef>
#include <cstdint>

namespace midi_control {

static constexpr uint8_t kOmniChannel = 16;
static constexpr uint8_t kInputGainCc = 16;
static constexpr uint8_t kOutputVolumeCc = 17;
static constexpr uint8_t kReverbMixCc = 18;
static constexpr uint8_t kBassCc = 19;
static constexpr uint8_t kMidCc = 20;
static constexpr uint8_t kTrebleCc = 21;
static constexpr uint8_t kReverbEnableCc = 23;
static constexpr size_t kParameterCount = 6;

enum class MessageType : uint8_t { Other, ControlChange, ProgramChange };
enum class Parameter : uint8_t {
    InputGain,
    OutputVolume,
    ReverbMix,
    Bass,
    Mid,
    Treble,
};
enum class ActionType : uint8_t { None, SelectModel, SetParameter, SetReverbEnabled };

struct Action {
    ActionType type = ActionType::None;
    int modelIndex = -1;
    Parameter parameter = Parameter::InputGain;
    float normalizedValue = 0.0f;
    bool enabled = false;
};

bool AcceptsChannel(uint8_t configuredChannel, int messageChannel);
float ValueToNormalized(uint8_t value);
Action Translate(MessageType type,
                 int channel,
                 uint8_t data0,
                 uint8_t data1,
                 uint8_t configuredChannel,
                 int modelCount);

struct PendingSnapshot {
    bool parameterPending[kParameterCount] = {};
    float parameterValues[kParameterCount] = {};
    bool namPending = false;
    bool namEnabled = false;
    bool reverbPending = false;
    bool reverbEnabled = false;
};

class PendingState {
  public:
    void SetParameter(Parameter parameter, float normalizedValue);
    void SetNamEnabled(bool enabled);
    void SetReverbEnabled(bool enabled);
    PendingSnapshot Consume();

  private:
    PendingSnapshot state_;
};

class KnobTakeover {
  public:
    void SetMidiControlled(float physicalPosition) {
        midiControlled_ = true;
        physicalAnchor_ = physicalPosition;
    }

    bool ShouldApplyPhysical(float physicalPosition, float deadband);
    bool IsMidiControlled() const { return midiControlled_; }

  private:
    bool midiControlled_ = false;
    float physicalAnchor_ = 0.0f;
};

template <size_t Capacity>
class RawThruBuffer {
  public:
    bool Push(const uint8_t* bytes, size_t size) {
        bool acceptedAll = true;
        for (size_t i = 0; i < size; ++i) {
            if (count_ == Capacity) {
                ++droppedBytes_;
                acceptedAll = false;
                continue;
            }
            data_[writeIndex_] = bytes[i];
            writeIndex_ = (writeIndex_ + 1) % Capacity;
            ++count_;
        }
        return acceptedAll;
    }

    size_t Pop(uint8_t* destination, size_t maxSize) {
        const size_t resultSize = count_ < maxSize ? count_ : maxSize;
        for (size_t i = 0; i < resultSize; ++i) {
            destination[i] = data_[readIndex_];
            readIndex_ = (readIndex_ + 1) % Capacity;
        }
        count_ -= resultSize;
        return resultSize;
    }

    size_t Size() const { return count_; }
    uint32_t DroppedBytes() const { return droppedBytes_; }

  private:
    uint8_t data_[Capacity] = {};
    size_t readIndex_ = 0;
    size_t writeIndex_ = 0;
    size_t count_ = 0;
    uint32_t droppedBytes_ = 0;
};

} // namespace midi_control