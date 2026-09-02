// Chromatic tuner, ported from bkshepherd's DaisySeedProjects GuitarPedal
// "Tuner" module (Effect-Modules/tuner_module.h / .cpp and
// Util/frequency_detector_q.h / .cpp, MIT), which itself wraps the Cycfi Q
// pitch_detector / signal_conditioner chain.
//
// Cycfi Q needs C++20 (concepts, in q/support/unit.hpp and
// q/support/basic_concepts.hpp), but AmpSim otherwise builds at gnu++17. To
// avoid bumping the whole project, every Q include lives in
// tuner_processor.cpp only, which is compiled at gnu++20 via a per-object
// override in the Makefile. This header is plain C++17 so it can be included
// from main.cpp unchanged. See THIRD_PARTY.md for licensing.
#pragma once

class TunerProcessor {
public:
    TunerProcessor();
    ~TunerProcessor();

    // Allocates the Cycfi Q detector state (~40KB from the heap, in RAM_D2).
    // Call once at boot, after hw.Init() -- never from the audio callback.
    void init(float sampleRate);

    // Feed one input sample. Safe to call every audio sample once init()
    // has run.
    void process(float in);

    // Clears the cached frequency/note (e.g. when re-entering tuner mode).
    void reset();

    bool hasPitch() const { return frequency_ > 0.0f; }
    float frequency() const { return frequency_; }
    // MIDI-style note index: A4 (440 Hz) == 69. Name is kNoteNames[idx % 12].
    int noteIndex() const { return noteIndex_; }
    int octave() const { return octave_; }
    float cents() const { return cents_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;

    float frequency_ = 0.0f;
    int noteIndex_ = 0;
    int octave_ = 0;
    float cents_ = 0.0f;
};
