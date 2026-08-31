// Reverb wrapper for AmpSim
// Mono input, stereo output with Dattorro plate reverb.
//
// The Dattorro delay lines are large and won't fit in the Daisy Seed SRAM
// heap. The reverb is therefore constructed lazily in init() *after* an
// SDRAM arena has been installed via InterpDelayArena::set(...).
//
// Dry/wet mix is applied at the output; process() takes the source signal
// (dry) and returns the mixed L/R stereo output.

#pragma once
#include <memory>
#include "dattorro/Dattorro.hpp"

class ReverbProcessor {
public:
    // Construct the Dattorro engine and apply Flick/MuleBox default params.
    // Call this AFTER InterpDelayArena::set(...) so buffers land in SDRAM.
    void init(float sampleRate) {
        // The tank runs at half the audio rate.
        //
        // Its cost is dominated not by arithmetic but by memory: every sample
        // it touches eight delay lines plus sixteen scattered output taps in
        // the 1 MB SDRAM arena, which is ~32 concurrent streams into a 16 KB
        // D-cache. They evict each other before a fetched line is used up, so
        // most accesses pay SDRAM latency. Halving the tick rate halves that
        // traffic along with the arithmetic.
        //
        // It costs nothing audible, because there is nothing up there to
        // lose. In pitch units freq = 440 * 2^(pitch-5), so the 7.25 set for
        // both the input high cut and the tank damping below is ~2.1 kHz --
        // and setTone's range tops out at that same 7.25. The reverb path is
        // band-limited to 2 kHz before the tank ever sees it; a 12 kHz
        // Nyquist is nowhere near it.
        //
        // Dattorro derives every delay length, LFO rate and filter
        // coefficient from the rate it is handed, so this is the only place
        // that has to know. Delay allocations halve too, which is why the
        // SDRAM arena has room to spare.
        reverb_ = std::make_unique<Dattorro>(sampleRate * 0.5f, 16.0f, 4.0f);

        // MuleBox / Flick "plate" preset
        reverb_->setTimeScale(1.007500f);
        reverb_->enableInputDiffusion(true);
        reverb_->setPreDelay(0.0f);

        // Input filtering (pitch units: freq = 440 * 2^(pitch-5))
        reverb_->setInputFilterLowCutoffPitch(2.87f);
        reverb_->setInputFilterHighCutoffPitch(7.25f);

        // Tank parameters
        reverb_->setDecay(0.8f);
        reverb_->setTankDiffusion(0.85f);
        reverb_->setTankFilterHighCutFrequency(7.25f);
        reverb_->setTankFilterLowCutFrequency(2.87f);

        // Modulation
        reverb_->setTankModSpeed(0.8f);
        reverb_->setTankModDepth(1.5f);
        reverb_->setTankModShape(0.25f);
    }

    bool isReady() const { return reverb_ != nullptr; }

    // Process a single sample. `reverbIn` is the signal fed into the reverb tank.
    // `dryIn` is the dry source signal to be mixed with the wet reverb output.
    // `outL`/`outR` receive the dry/wet mixed output.
    // The tank ticks on even samples only; odd samples read back the midpoint
    // of the last two tick outputs. Input samples are averaged in pairs on the
    // way down, which is a crude decimator but an ample one when the tank's
    // own input filter already stops at 2 kHz -- anything that could alias
    // into the passband would have to come from 22 kHz and up.
    //
    // Output lags one tick (two audio samples, ~42 us) so that the
    // interpolation always runs between two ticks that have both happened.
    // Emitting the newest tick immediately and interpolating afterwards would
    // step forward and then back in time, which is audible as distortion.
    void process(float reverbIn, float dryIn, float* outL, float* outR) {
        if (!reverb_) {
            *outL = dryIn;
            *outR = dryIn;
            return;
        }

        float wetL, wetR;
        if (!tickPhase_) {
            const float decimated = inAcc_ * 0.5f;
            reverb_->process(decimated, decimated);
            prevL_ = curL_;
            prevR_ = curR_;
            curL_ = reverb_->getLeftOutput();
            curR_ = reverb_->getRightOutput();
            wetL = prevL_;
            wetR = prevR_;
            inAcc_ = reverbIn;
        } else {
            inAcc_ += reverbIn;
            wetL = (prevL_ + curL_) * 0.5f;
            wetR = (prevR_ + curR_) * 0.5f;
        }
        tickPhase_ = !tickPhase_;

        *outL = dryIn * dryMix_ + wetL * wetMix_;
        *outR = dryIn * dryMix_ + wetR * wetMix_;
    }

    // Standard Guitar Pedal Mix: Dry stays at unity (1.0), wet increases from 0 to 1.
    void setMix(float mix) {
        if (mix < 0.0f) mix = 0.0f;
        if (mix > 1.0f) mix = 1.0f;
        wetMix_ = mix;
        dryMix_ = 1.0f;
    }

    void setDecay(float decay) {
        if (reverb_) reverb_->setDecay(decay);
    }

    void setTone(float tone) {
        // Map 0-1 to a pitch value for the tank high cut (in "pitch" units).
        const float pitch = 2.87f + tone * 4.38f;  // 2.87 .. 7.25
        if (reverb_) reverb_->setTankFilterHighCutFrequency(pitch);
    }

    void setModSpeed(float speed) {
        if (reverb_) reverb_->setTankModSpeed(speed);
    }

    void setModDepth(float depth) {
        if (reverb_) reverb_->setTankModDepth(depth);
    }

    void clear() {
        if (reverb_) reverb_->clear();
        // The half-rate interpolator holds two ticks of output. Leaving them
        // behind would let the old tail bleed through the first samples after
        // a model change or a fault recovery.
        inAcc_ = 0.0f;
        prevL_ = prevR_ = curL_ = curR_ = 0.0f;
        tickPhase_ = false;
    }

    float getDryMix() const { return dryMix_; }
    float getWetMix() const { return wetMix_; }

private:
    std::unique_ptr<Dattorro> reverb_;
    float dryMix_ = 0.7f;
    float wetMix_ = 0.3f;

    // Half-rate tick state: the pair accumulator on the way in, and the two
    // most recent tank outputs to interpolate between on the way out.
    float inAcc_ = 0.0f;
    float prevL_ = 0.0f, prevR_ = 0.0f;
    float curL_ = 0.0f, curR_ = 0.0f;
    bool tickPhase_ = false;
};
