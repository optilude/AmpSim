// See tuner_processor.h for the port lineage and why this translation unit
// is compiled at C++20 (see the per-object override in the Makefile) while
// the rest of AmpSim stays on C++17.
#include "tuner_processor.h"

#include <q/fx/signal_conditioner.hpp>
#include <q/pitch/pitch_detector.hpp>
#include <1efilter.hpp>

#include "daisy_seed.h"

#include <cmath>

using namespace cycfi::q;

namespace {
// C1 / C5, the detector's frequency floor/ceiling in the upstream port.
// Upstream gets these from q/support/pitch_names.hpp (a large symbolic
// octave/pitch-name table); rather than vendor that whole header for two
// constants, they're computed directly here (12-TET, A4 = 440 Hz):
//   C1 = 440 * 2^((-9-36)/12), C5 = 440 * 2^((-9+12)/12)
constexpr double kC1Hz = 32.703195662574829;
constexpr double kC5Hz = 523.2511306011972;

// A4 (440 Hz) = MIDI note 69; upstream spells this as the ASCII value of
// 'E' (also 69), which is a coincidence, not a reference to the note E.
constexpr int kA4NoteIndex = 69;
} // namespace

struct TunerProcessor::Impl {
    // Qualified as cycfi::q::frequency, not just frequency(): unqualified
    // lookup here finds the enclosing TunerProcessor::frequency() accessor
    // first (nested classes see their enclosing class's member names), which
    // takes no arguments and shadows the free type's constructor call.
    Impl(float sampleRate)
        : pitchDetector(cycfi::q::frequency(kC1Hz), cycfi::q::frequency(kC5Hz), sampleRate, lin_to_db(0)),
          preProcessor(signal_conditioner::config{}, cycfi::q::frequency(kC1Hz), cycfi::q::frequency(kC5Hz), sampleRate) {}

    pitch_detector pitchDetector;
    signal_conditioner preProcessor;
    // Same smoothing constants as upstream's FrequencyDetectorQ: cutoff 0.5,
    // beta 0.05, derivative cutoff 1.0. First argument (freq) is a seed --
    // Process() below drives the filter by real timestamps instead.
    one_euro_filter<float, float> smoothingFilter{48000.0, 0.5f, 0.05f, 1.0f};
};

TunerProcessor::TunerProcessor() = default;

TunerProcessor::~TunerProcessor() = default;

void TunerProcessor::init(float sampleRate) {
    if (impl_ != nullptr) return;
    impl_ = new Impl(sampleRate);
}

void TunerProcessor::process(float in) {
    if (impl_ == nullptr) return;

    const float conditioned = impl_->preProcessor(in);
    const bool ready = impl_->pitchDetector(conditioned);

    if (ready) {
        const float rawFrequency = impl_->pitchDetector.get_frequency();
        const float nowSeconds = static_cast<float>(daisy::System::GetNow()) / 1000.f;
        frequency_ = impl_->smoothingFilter(rawFrequency, nowSeconds);

        if (frequency_ > 0.0f) {
            const float semitones = 12.0f * (std::log(frequency_ / 440.0f) / std::log(2.0f));
            noteIndex_ = static_cast<int>(std::round(semitones)) + kA4NoteIndex;
            octave_ = noteIndex_ / 12 - 1;

            const float noteFrequency = 440.0f * std::pow(2.0f, (noteIndex_ - kA4NoteIndex) / 12.0f);
            cents_ = 1200.0f * std::log(frequency_ / noteFrequency) / std::log(2.0f);
        }
    }
}

void TunerProcessor::reset() {
    frequency_ = 0.0f;
    noteIndex_ = 0;
    octave_ = 0;
    cents_ = 0.0f;
    if (impl_ != nullptr) impl_->pitchDetector.reset();
}
