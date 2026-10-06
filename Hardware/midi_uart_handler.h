#pragma once

#include "hid/midi.h"

namespace multifs {

class MidiUartThruHandler {
  public:
    using RawRxCallback = void (*)(const uint8_t* data, size_t size, void* context);

    struct Config {
        daisy::MidiUartTransport::Config transport_config;
    };

    MidiUartThruHandler() : rawRxCallback_(nullptr), rawRxContext_(nullptr) {}

    void Init(Config config) {
        transport_.Init(config.transport_config);
        parser_.Init();
    }

    void StartReceive() {
        transport_.StartRx(ParseCallback, this);
    }

    void Listen() {
        if (!transport_.RxActive()) {
            parser_.Reset();
            transport_.FlushRx();
            StartReceive();
        }
    }

    bool HasEvents() const { return eventQueue_.GetNumElements() > 0; }
    daisy::MidiEvent PopEvent() { return eventQueue_.PopFront(); }

    void SendMessage(uint8_t* bytes, size_t size) {
        transport_.Tx(bytes, size);
    }

    void SetRawRxCallback(RawRxCallback callback, void* context) {
        rawRxCallback_ = callback;
        rawRxContext_ = context;
    }

  private:
    daisy::MidiUartTransport transport_;
    daisy::MidiParser parser_;
    daisy::FIFO<daisy::MidiEvent, 256> eventQueue_;
    RawRxCallback rawRxCallback_;
    void* rawRxContext_;

    static void ParseCallback(uint8_t* data, size_t size, void* context) {
        MidiUartThruHandler* handler = static_cast<MidiUartThruHandler*>(context);
        if (handler->rawRxCallback_)
            handler->rawRxCallback_(data, size, handler->rawRxContext_);

        for (size_t i = 0; i < size; ++i) {
            daisy::MidiEvent event;
            if (handler->parser_.Parse(data[i], &event))
                handler->eventQueue_.PushBack(event);
        }
    }
};

} // namespace multifs