// Simple reverb wrapper for AmpSim.
//
// Ported from bkshepherd's DaisySeedProjects GuitarPedal "Reverb" module
// (Effect-Modules/reverb_module.h / .cpp, MIT), which wraps DaisySP's
// ReverbSc. Offered as a lighter-weight alternative to the Dattorro plate
// tank in reverb_processor.h -- selectable from the Settings menu.
//
// AmpSim has a single reverb knob (Mix), already wired to whichever engine
// is active, so this wrapper exposes the same
// process(reverbIn, dryIn, outL, outR) / setMix / clear / isReady surface as
// ReverbProcessor. Time and Damp are not exposed as controls; both are
// pinned to reverb_module.cpp's own defaults (see kDefaultTime/kDefaultDamp
// below).
//
// ReverbSc keeps its ~386 KiB of delay-line buffers (DSY_REVERBSC_MAX_SIZE
// floats) embedded directly in the object, unlike Dattorro's InterpDelay
// (which carves its buffers from a separately-installed SDRAM arena). That
// means the whole SimpleReverbProcessor object -- not just some member --
// has to be placed in SDRAM at its point of definition; see
// `DSY_SDRAM_BSS simpleReverbProcessor` in main.cpp. SDRAM is NOLOAD and not
// zeroed at boot (see reverb_arena.cpp), but ReverbSc::Init() explicitly
// zeroes every delay line and filter state, and nothing here reads engine
// state before init() has run.

#pragma once
#include "Effects/reverbsc.h"

class SimpleReverbProcessor {
public:
    // bkshepherd's ParameterMetaData defaults: TIME = 0.45, DAMP = 0.3.
    static constexpr float kDefaultTime = 0.45f;
    static constexpr float kDefaultDamp = 0.3f;

    // Construct the engine and apply the ported module's default Time/Damp.
    void init(float sampleRate) {
        sampleRate_ = sampleRate;
        engine_.Init(sampleRate_);
        ApplyDefaults();
        wetMix_ = 0.3f;
        ready_ = true;
    }

    bool isReady() const { return ready_; }

    // Same call shape as ReverbProcessor::process: reverbIn feeds the tank,
    // dryIn is the source signal mixed back in at the output.
    void process(float reverbIn, float dryIn, float* outL, float* outR) {
        if (!ready_) {
            *outL = dryIn;
            *outR = dryIn;
            return;
        }

        float wetL, wetR;
        engine_.Process(reverbIn, reverbIn, &wetL, &wetR);
        *outL = wetL * wetMix_ + dryIn * (1.0f - wetMix_);
        *outR = wetR * wetMix_ + dryIn * (1.0f - wetMix_);
    }

    // Linear crossfade, matching bkshepherd's reverb_module.cpp -- unlike
    // ReverbProcessor::setMix, there is no squared taper or dry floor here.
    void setMix(float mix) {
        if (mix < 0.0f) mix = 0.0f;
        if (mix > 1.0f) mix = 1.0f;
        wetMix_ = mix;
    }

    // Re-running Init() re-zeroes every delay line and filter state (see
    // ReverbSc::InitDelayLine), the same effect Dattorro::clear() has for the
    // other engine.
    void clear() {
        if (!ready_) return;
        engine_.Init(sampleRate_);
        ApplyDefaults();
    }

private:
    void ApplyDefaults() {
        engine_.SetFeedback(kTimeMin + kDefaultTime * (kTimeMax - kTimeMin));
        engine_.SetLpFreq(DampToLpFreq(kDefaultDamp));
    }

    // Inverts Damp (knob left = less dampening, knob right = more) and
    // squares it for an exponential taper, exactly as reverb_module.cpp
    // does.
    static float DampToLpFreq(float damp) {
        float inverted = 1.0f - damp;
        inverted *= inverted;
        return kLpFreqMin + inverted * (kLpFreqMax - kLpFreqMin);
    }

    static constexpr float kTimeMin = 0.6f;
    static constexpr float kTimeMax = 1.0f;
    static constexpr float kLpFreqMin = 600.0f;
    static constexpr float kLpFreqMax = 16000.0f;

    // No in-class initializers here: this object lives in SDRAM
    // (DSY_SDRAM_BSS in main.cpp), and giving these fields default values
    // makes the compiler emit a global constructor that runs before main()
    // -- before hw.Init() has configured the FMC/SDRAM controller -- causing
    // a bus fault on boot. init() (called after hw.Init()) sets all of
    // these before anything reads them.
    daisysp::ReverbSc engine_;
    float sampleRate_;
    float wetMix_;
    bool ready_;
};
