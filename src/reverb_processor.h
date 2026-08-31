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
    // Construct both engines and apply the Flick/MuleBox preset to each.
    // Call this AFTER InterpDelayArena::set(...) so buffers land in SDRAM.
    //
    // Two engines exist so the half-rate tank can be A/B'd against the full-
    // rate one by ear, on the same signal, without a reflash. The half-rate
    // one is the default: it costs 15% of the audio block against 29%, which
    // is what makes NAM + reverb fit at all.
    //
    // Why halving the tick rate is nearly free: the tank's cost is memory,
    // not arithmetic. Every sample it touches eight delay lines plus sixteen
    // scattered output taps in the SDRAM arena -- ~32 concurrent streams into
    // a 16 KB D-cache, which cannot hold them, so lines are evicted before
    // they are used up and most accesses pay SDRAM latency.
    //
    // And why it should cost nothing audible: in pitch units
    // freq = 440 * 2^(pitch-5), so the 7.25 used below for both the input
    // high cut and the tank damping is ~2.1 kHz, and setTone's range tops out
    // at the same value. The reverb path is band-limited to 2 kHz before the
    // tank sees it, nowhere near a 12 kHz Nyquist.
    void init(float sampleRate) {
        half_ = std::make_unique<Dattorro>(sampleRate * 0.5f, 16.0f, 4.0f);
        full_ = std::make_unique<Dattorro>(sampleRate, 16.0f, 4.0f);
        ApplyPreset(*half_);
        ApplyPreset(*full_);
    }

    bool isReady() const { return half_ != nullptr && full_ != nullptr; }

    // Process a single sample. `reverbIn` is the signal fed into the reverb tank.
    // `dryIn` is the dry source signal to be mixed with the wet reverb output.
    // `outL`/`outR` receive the dry/wet mixed output.
    // The half-rate tank ticks on even samples only; odd samples read back the
    // midpoint of the last two tick outputs. Input samples are averaged in
    // pairs on the way down, which is a crude decimator but an ample one when
    // the tank's own input filter already stops at 2 kHz -- anything that
    // could alias into the passband would have to come from 22 kHz and up.
    //
    // Output lags one tick (two audio samples, ~42 us) so that the
    // interpolation always runs between two ticks that have both happened.
    // Emitting the newest tick immediately and interpolating afterwards would
    // step forward and then back in time, which is audible as distortion.
    //
    // In compare mode both engines run so switching is seamless mid-tail --
    // otherwise the newly selected one starts from a decayed state and there
    // is nothing to compare. That costs both their budgets at once, so the
    // caller only enables it when the model engine is off.
    void process(float reverbIn, float dryIn, float* outL, float* outR) {
        if (!half_ || !full_) {
            *outL = dryIn;
            *outR = dryIn;
            return;
        }

        float halfL = 0.0f, halfR = 0.0f;
        if (halfRate_ || compare_) {
            if (!tickPhase_) {
                const float decimated = inAcc_ * 0.5f;
                half_->process(decimated, decimated);
                prevL_ = curL_;
                prevR_ = curR_;
                curL_ = half_->getLeftOutput();
                curR_ = half_->getRightOutput();
                halfL = prevL_;
                halfR = prevR_;
                inAcc_ = reverbIn;
            } else {
                inAcc_ += reverbIn;
                halfL = (prevL_ + curL_) * 0.5f;
                halfR = (prevR_ + curR_) * 0.5f;
            }
            tickPhase_ = !tickPhase_;
        }

        float fullL = 0.0f, fullR = 0.0f;
        if (!halfRate_ || compare_) {
            full_->process(reverbIn, reverbIn);
            fullL = full_->getLeftOutput();
            fullR = full_->getRightOutput();
        }

        const float wetL = halfRate_ ? halfL : fullL;
        const float wetR = halfRate_ ? halfR : fullR;
        *outL = dryIn * dryMix_ + wetL * wetMix_;
        *outR = dryIn * dryMix_ + wetR * wetMix_;
    }

    // Run both engines so the A/B switch is seamless. Only affordable with
    // the model engine off: 15% + 29% of the block instead of one or other.
    void setCompareMode(bool on) { compare_ = on; }

    bool isHalfRate() const { return halfRate_; }
    void toggleRate() { halfRate_ = !halfRate_; }

    // Standard Guitar Pedal Mix: Dry stays at unity (1.0), wet increases from 0 to 1.
    void setMix(float mix) {
        if (mix < 0.0f) mix = 0.0f;
        if (mix > 1.0f) mix = 1.0f;
        wetMix_ = mix;
        dryMix_ = 1.0f;
    }

    void setDecay(float decay) {
        if (half_) half_->setDecay(decay);
        if (full_) full_->setDecay(decay);
    }

    void setTone(float tone) {
        // Map 0-1 to a pitch value for the tank high cut (in "pitch" units).
        const float pitch = 2.87f + tone * 4.38f;  // 2.87 .. 7.25
        if (half_) half_->setTankFilterHighCutFrequency(pitch);
        if (full_) full_->setTankFilterHighCutFrequency(pitch);
    }

    void setModSpeed(float speed) {
        if (half_) half_->setTankModSpeed(speed);
        if (full_) full_->setTankModSpeed(speed);
    }

    void setModDepth(float depth) {
        if (half_) half_->setTankModDepth(depth);
        if (full_) full_->setTankModDepth(depth);
    }

    void clear() {
        if (half_) half_->clear();
        if (full_) full_->clear();
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
    static void ApplyPreset(Dattorro& d) {
        // MuleBox / Flick "plate" preset.
        d.setTimeScale(1.007500f);
        d.enableInputDiffusion(true);
        d.setPreDelay(0.0f);

        // Input filtering (pitch units: freq = 440 * 2^(pitch-5))
        d.setInputFilterLowCutoffPitch(2.87f);
        d.setInputFilterHighCutoffPitch(7.25f);

        // Tank parameters
        d.setDecay(0.8f);
        d.setTankDiffusion(0.85f);
        d.setTankFilterHighCutFrequency(7.25f);
        d.setTankFilterLowCutFrequency(2.87f);

        // Modulation
        d.setTankModSpeed(0.8f);
        d.setTankModDepth(1.5f);
        d.setTankModShape(0.25f);
    }

    std::unique_ptr<Dattorro> half_;
    std::unique_ptr<Dattorro> full_;
    bool halfRate_ = true;
    bool compare_ = false;
    float dryMix_ = 0.7f;
    float wetMix_ = 0.3f;

    // Half-rate tick state: the pair accumulator on the way in, and the two
    // most recent tank outputs to interpolate between on the way out.
    float inAcc_ = 0.0f;
    float prevL_ = 0.0f, prevR_ = 0.0f;
    float curL_ = 0.0f, curR_ = 0.0f;
    bool tickPhase_ = false;
};
