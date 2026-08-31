// AmpSim - Guitar Amp Simulator based on Daisy Seed
// NAM A2 processing with Dattorro plate reverb and full control scheme.
//
// Signal path (per audio block):
//   in[0] -> [inputGain] -> [NAM] -> [IR] -> [3-band EQ] -> [reverb wet/dry] -> [outputVolume] -> out[0,1]
//
// The dry+wet mix is applied inside ReverbProcessor::process. When both
// effects are off, the true-bypass relay handles the analog path and the
// callback echoes silence (the codec output is muted while the relay flips
// and the analog signal is routed around the DSP).

#include "guitar_pedal_125b.h"
#include "nam_processor.h"
#include "reverb_processor.h"
#include "reverb_arena.h"
#include "dattorro/dsp/delays/InterpDelay.hpp"
#include "capture_index.h"
#include "capture_verify.h"
#include "ir_processor.h"
#include "settings.h"
#include "gain_stage.h"
#include "guitar_eq.h"
#include "constants.h"
#include "float_guard.h"
#include "helpers.h"
#include "util/CpuLoadMeter.h"

#include <string.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

using namespace multifs;

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
GuitarPedal125B hw;

// ---------------------------------------------------------------------------
// DSP components
//
// The reverb is constructed lazily in main() after the SDRAM arena is
// installed, so it MUST come as a plain global that is only wired up via
// its init() method (see ReverbProcessor).
// ---------------------------------------------------------------------------
NAMProcessor namProcessor;
ReverbProcessor reverbProcessor;
IRProcessor irProcessor;
GainStage inputGain;
GainStage outputVolume;
GuitarEQ eq;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
SettingsManager settings;
PersistentSettings* currentSettings = nullptr;  // Wired to storage in main().

// Model browsing (encoder-driven).
int previewModelIndex = 0;
bool isPreviewingModel = false;
uint32_t previewStartTime = 0;

// Display update throttling.
uint32_t lastDisplayUpdate = 0;

// Debounced settings save. Save() is currently a no-op under BOOT_QSPI, but
// keeping the dirty tracking localizes the future persistence hook.
uint32_t lastSettingsChange = 0;
bool settingsDirty = false;

// True-bypass relay / analog mute. Driven entirely from the audio callback as
// a sample counter: the main loop used to sleep 60 ms inside the toggle, which
// froze the UI and the display every time a footswitch was pressed.
bool currentBypassState = false;   // last effect-derived bypass decision
bool relayBypassOn = false;        // what the relay pin is actually set to
bool relayMuteOn = false;          // what the mute pin is actually set to
int32_t samplesTilBypassToggle = 0;
int32_t samplesTilMuteOff = 0;
int32_t bypassToggleTransitionSamples = 0;
int32_t muteOffTransitionSamples = 0;

// Set by the audio callback when the encoder is clicked. Loading a model reads
// QSPI and paints the display, so it has to happen on the main loop.
volatile int pendingLoadIndex = -1;

// Audio callback timing. The full chain (NAM + IR + EQ + reverb) is close to
// the per-block budget, so the load is measured and surfaced on the display.
daisy::CpuLoadMeter loadMeter;

// Safety valve: if the callback consistently cannot finish inside its block
// period the main loop is starved and the pedal appears frozen. Disable the
// model engine from the audio thread instead so control is never lost.
volatile bool cpuOverloadTripped = false;
int32_t overloadWarmupBlocks = 0;   // re-zero the meter when this hits 0
int32_t overloadGraceBlocks = 0;    // cannot trip while this is above 0
int32_t overloadStreakBlocks = 0;   // consecutive blocks over the threshold
// The readings at the moment of the trip, held for the display. Without these
// the screen only ever says "overloaded", which is not enough to tell a chain
// that is 5% too slow from one that is 100% too slow.
volatile float overloadAvgAtTrip = 0.0f;
volatile float overloadMaxAtTrip = 0.0f;

// Blocks left to keep running the reverb after it is switched off, so the
// tail rings out instead of being cut dead. Unbounded trails would mean
// paying for the reverb forever whenever the model engine is on.
volatile int32_t reverbTailBlocks = 0;
int32_t reverbTailBlocksFull = 0;  // REVERB_TAIL_SECONDS converted at init.

// Set by the audio thread when the model path emits a non-finite sample.
// The recovery (clearing the reverb tank) memsets ~1 MB of SDRAM, far too slow
// for the ISR, so the main loop picks this up and does the work.
volatile bool dspFaultPending = false;
volatile bool dspFaultLatched = false;

// Audio-callback-visible suppression flag. Set while the analog mute is
// engaged around a relay flip so the DSP outputs silence for the duration
// (prevents pops on the DAC output while the relay contacts settle).
volatile bool audioSuppressed = false;

// Live physical knob state. Knobs are intentionally not persisted; after boot,
// the physical pot position is the source of truth for these parameters.
float currentInputGainKnob = 0.5f;
float currentOutputVolumeKnob = 0.5f;
float currentReverbMixKnob = 0.3f;
float currentBassKnob = 0.5f;
float currentMidKnob = 0.5f;
float currentTrebleKnob = 0.5f;

// Audio block size. 48 matches the NAM A2 inference block exactly (one
// inference per callback, flat load instead of the 2-or-3 the old 128 gave)
// and puts the control/LED update rate at 1 kHz, which is what libDaisy's
// Switch and Encoder debouncing is tuned for. Same value bkshepherd uses.
static constexpr size_t kAudioBlock = 48;

// Scratch buffers for block-based DSP.
static constexpr size_t kMaxBlock = 128;
float scratchDry[kMaxBlock];
float scratchMid[kMaxBlock];
float scratchWet[kMaxBlock];

// IR spectrum and frequency-domain delay line. These are the hottest large
// buffers in the callback: every block streams both of them end to end, which
// in SDRAM made the convolution memory-bound. RAM_D2 has the headroom and is
// far closer to the core.
static float g_ir_freq_buf[IRProcessor::kMaxPartitions * ConvolutionEngine::N] __attribute__((section(".sram_d2_bss")));
static float g_ir_fdl_buf[IRProcessor::kMaxPartitions * ConvolutionEngine::N] __attribute__((section(".sram_d2_bss")));

void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t size);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void SaveSettingsDebounced() {
    settingsDirty = true;
    lastSettingsChange = daisy::System::GetNow();
}

// Both effects OFF => relay engaged (analog pass-through), otherwise the DSP
// is in circuit. Called once per audio block; when the decision changes it
// arms the mute/relay sequence that RunBypassTimingForSample steps through.
static inline void UpdateBypassRelay() {
    const bool shouldBypass = !currentSettings->namEnabled
                           && !currentSettings->reverbEnabled;

    if (shouldBypass == currentBypassState) return;
    currentBypassState = shouldBypass;

    // Mute immediately, flip the relay once the output is quiet, release the
    // mute after the contacts have settled. No sleeping: the audio callback
    // is now the only thing servicing the controls and the LEDs.
    relayMuteOn = true;
    samplesTilBypassToggle = bypassToggleTransitionSamples;
    samplesTilMuteOff = muteOffTransitionSamples;
}

// Advance the mute/relay sequence by one block's worth of samples. The GPIOs
// are written once per block, so counting per block rather than per sample
// costs no accuracy and keeps it out of the inner loops.
static inline void StepBypassTiming(int32_t samples) {
    if (!relayMuteOn) return;
    samplesTilBypassToggle -= samples;
    samplesTilMuteOff -= samples;
    if (samplesTilBypassToggle < 0) relayBypassOn = currentBypassState;
    if (samplesTilMuteOff < 0) relayMuteOn = false;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
// 128x64 SSD1306. Font_6x8 => max 21 chars per line; Font_7x10 => max 18.
// Text longer than that is truncated by the OLED driver, not our concern
// beyond aesthetics — we still snprintf-clip our own buffers.

// A CpuLoadMeter reading as a percent, clamped to three digits so it cannot
// push the rest of the line off the display. NaN is what the meter holds
// between Reset() and the first block end.
static int LoadPercent(float load) {
    if (fguard::IsNonFinite(load) || load <= 0.0f) return 0;
    const int pct = (int)std::lround(load * 100.0f);
    return pct > 999 ? 999 : pct;
}

void UpdateDisplay() {
    hw.display.Fill(false);

    char line[32];

    const int shownIndex = isPreviewingModel ? previewModelIndex : currentSettings->modelIndex;
    const bool haveModels = (MODEL_COUNT > 0) && shownIndex >= 0 && shownIndex < MODEL_COUNT;

    // Line 0: model name (with preview arrow if browsing).
    hw.display.SetCursor(0, 0);
    if (haveModels) {
        snprintf(line, sizeof line, "%s%s",
                 isPreviewingModel ? "> " : "",
                 model_entries[shownIndex].model_name);
    } else {
        snprintf(line, sizeof line, "No models");
    }
    hw.display.WriteString(line, Font_7x10, true);

    // Line 1: variant and type.
    hw.display.SetCursor(0, 12);
    if (haveModels) {
        const char* typeStr = "";
        switch (model_entries[shownIndex].type) {
            case ModelType::NamOnly: typeStr = "[NAM]"; break;
            case ModelType::IrOnly: typeStr = "[IR]"; break;
            case ModelType::NamAndIr: typeStr = "[N+I]"; break;
        }
        snprintf(line, sizeof line, "%.15s %s", model_entries[shownIndex].variant_name, typeStr);
    } else {
        snprintf(line, sizeof line, "(regenerate models)");
    }
    hw.display.WriteString(line, Font_6x8, true);

    // Line 2: effect states.
    hw.display.SetCursor(0, 22);
    snprintf(line, sizeof line, "MDL:%s REV:%s %s",
             currentSettings->namEnabled ? "ON " : "OFF",
             currentSettings->reverbEnabled ? "ON " : "OFF",
             reverbProcessor.isHalfRate() ? "24k" : "48k");
    hw.display.WriteString(line, Font_6x8, true);

    // Line 3: I/O gain (compact so it fits: max "In:-20 Out:+20" = 14 chars).
    // Read from the knobs, not the gain stages: those only advance on the
    // audio thread, which is skipped entirely in true bypass.
    hw.display.SetCursor(0, 32);
    const int inDb = (int)std::lround(KnobToNormalized(currentInputGainKnob) * GAIN_RANGE_DB);
    const int outDb = (int)std::lround(KnobToNormalized(currentOutputVolumeKnob) * GAIN_RANGE_DB);
    snprintf(line, sizeof line, "In:%+3d Out:%+3d dB", inDb, outDb);
    hw.display.WriteString(line, Font_6x8, true);

    // Line 4: reverb mix + EQ. Keep it under 21 chars for Font_6x8.
    hw.display.SetCursor(0, 42);
    const int revPct = (int)std::lround(currentReverbMixKnob * 100.0f);
    const int bassDb = (int)std::lround(KnobToNormalized(currentBassKnob) * EQ_RANGE_DB);
    const int midDb = (int)std::lround(KnobToNormalized(currentMidKnob) * EQ_RANGE_DB);
    const int trebDb = (int)std::lround(KnobToNormalized(currentTrebleKnob) * EQ_RANGE_DB);
    snprintf(line, sizeof line, "R%3d B%+3d M%+3d T%+3d", revPct, bassDb, midDb, trebDb);
    hw.display.WriteString(line, Font_6x8, true);

    // Line 5: context-sensitive help.
    hw.display.SetCursor(0, 52);
    if (isPreviewingModel) {
        hw.display.WriteString("Click to load", Font_6x8, true);
    } else if (dspFaultLatched) {
        hw.display.WriteString("Model NaN: MDL off", Font_6x8, true);
    } else if (cpuOverloadTripped) {
        // The numbers matter: "97/104" is a chain that needs trimming,
        // "180/240" is one that needs rethinking.
        snprintf(line, sizeof line, "OVL%3d/%3d%% FS1 retry",
                 LoadPercent(overloadAvgAtTrip), LoadPercent(overloadMaxAtTrip));
        hw.display.WriteString(line, Font_6x8, true);
    } else {
        snprintf(line, sizeof line, "CPU%3d/%3d%% F1:M F2:R",
                 LoadPercent(loadMeter.GetAvgCpuLoad()),
                 LoadPercent(loadMeter.GetMaxCpuLoad()));
        hw.display.WriteString(line, Font_6x8, true);
    }

    hw.display.Update();
}

// Show a fullscreen message. Used for load progress and error surfaces.
static void ShowMessage(const char* line0, const char* line1 = nullptr,
                        const char* line2 = nullptr, const char* line3 = nullptr) {
    hw.display.Fill(false);
    hw.display.SetCursor(0, 0);
    hw.display.WriteString(line0, Font_7x10, true);
    const char* rest[3] = {line1, line2, line3};
    for (int i = 0; i < 3; ++i) {
        if (!rest[i]) continue;
        hw.display.SetCursor(0, 14 + i * 10);
        hw.display.WriteString(rest[i], Font_6x8, true);
    }
    hw.display.Update();
}

// ---------------------------------------------------------------------------
// Model management
// ---------------------------------------------------------------------------
bool IsValidModelIndex(int index) {
    return index >= 0 && index < MODEL_COUNT;
}

// Load a model by index. Suppresses audio for the duration so we don't
// glitch. Preserves the previously loaded model on failure by not touching
// currentSettings->modelIndex until success is confirmed.
void LoadModel(int index) {
    if (!IsValidModelIndex(index)) {
        ShowMessage("Invalid model");
        hw.DelayMs(ERROR_DISPLAY_TIME_MS);
        return;
    }

    const ModelEntry& entry = model_entries[index];
    char typeLine[32];
    const char* typeStr = "";
    switch (entry.type) {
        case ModelType::NamOnly: typeStr = "[NAM]"; break;
        case ModelType::IrOnly: typeStr = "[IR]"; break;
        case ModelType::NamAndIr: typeStr = "[NAM+IR]"; break;
    }
    snprintf(typeLine, sizeof typeLine, "%s %s", entry.variant_name, typeStr);

    ShowMessage("Loading...", typeLine);

    // The capture blob is read straight out of memory-mapped QSPI at an address
    // baked in at build time. If that address is wrong the data still reads —
    // it is just the wrong data, or unwritten flash (0xFF == NaN). Catch it here
    // rather than letting NaN propagate into the EQ and reverb state.
    if (!capture::VerifyEntry(entry)) {
        // Show the numbers, not just the verdict. A mismatch means the blob in
        // QSPI is not the one this build was compiled against -- almost always
        // a stale flash -- and the address plus the two CRCs is enough to tell
        // that apart from genuinely corrupt data without attaching a probe.
        const uintptr_t addr = entry.nam_qspi_address ? entry.nam_qspi_address
                                                      : entry.ir_qspi_address;
        char l1[32], l2[32], l3[32];
        snprintf(l1, sizeof l1, "at %08lx", (unsigned long)addr);
        snprintf(l2, sizeof l2, "got %08lx", (unsigned long)capture::ComputeEntryCrc(entry));
        snprintf(l3, sizeof l3, "want %08lx", (unsigned long)entry.crc32);
        ShowMessage("Bad model data", l1, l2, l3);
        hw.DelayMs(ERROR_DISPLAY_TIME_MS);
        return;
    }

    // Silence the DSP while the model state is swapped, but keep the callback
    // running: it is what services the controls, the LEDs and the relay now,
    // and StopAudio() here would freeze all of them. audioSuppressed makes the
    // callback skip the DSP only.
    audioSuppressed = true;
    bool ok = true;
    
    // Clear out previous state regardless of what we're loading
    namProcessor.reset();
    irProcessor.clear();
    
    if (entry.type == ModelType::NamOnly || entry.type == ModelType::NamAndIr) {
        ok &= namProcessor.loadModel(entry);
    }
    
    if (entry.type == ModelType::IrOnly || entry.type == ModelType::NamAndIr) {
        ok &= irProcessor.loadModel(entry);
    }
    
    audioSuppressed = false;

    if (!ok) {
        ShowMessage("Load failed", entry.variant_name);
        hw.DelayMs(ERROR_DISPLAY_TIME_MS);
        // Keep the old model index and the previous loaded model.
        return;
    }

    if (currentSettings->modelIndex != index) {
        currentSettings->modelIndex = index;
        SaveSettingsDebounced();
    }
}

// ---------------------------------------------------------------------------
// Controls
//
// Everything here runs on the audio thread, once per block. That is the only
// fixed-rate context in the firmware: the main loop's period swings with the
// display refresh and with QSPI reads, which is what made the switches miss
// edges and the LEDs flicker. Same arrangement bkshepherd uses.
//
// The rules for this section: no display writes, no blocking, no QSPI. Work
// that needs any of those sets a flag for the main loop instead.
// ---------------------------------------------------------------------------
void HandleEncoderMovement() {
    if (MODEL_COUNT == 0) return;

    const int32_t inc = hw.encoders[0].Increment();
    if (inc == 0) return;

    int newIndex = previewModelIndex + inc;
    if (newIndex < 0) newIndex = MODEL_COUNT - 1;
    if (newIndex >= MODEL_COUNT) newIndex = 0;

    previewModelIndex = newIndex;
    isPreviewingModel = true;
    previewStartTime = daisy::System::GetNow();
}

void HandleEncoderClick() {
    if (!hw.encoders[0].RisingEdge()) return;

    if (!isPreviewingModel) {
        // A click outside model browsing had no meaning, so it is the A/B
        // switch for the reverb tank rate. Half rate costs 15% of the block
        // against 29% and is what makes MDL + REV fit; this is here to judge
        // by ear whether it costs anything worth 14 points.
        reverbProcessor.toggleRate();
        return;
    }

    {
        // Snapshot the target before we clear the flag so that if the
        // timeout races with this callback we still load what the user saw.
        // Loading reads QSPI and paints the display, so hand it to the main
        // loop rather than doing it here.
        pendingLoadIndex = previewModelIndex;
        isPreviewingModel = false;
    }
}

void HandleFootswitches() {
    // switches[0] (FS2 on the enclosure): Reverb on/off
    if (hw.switches[0].RisingEdge()) {
        currentSettings->reverbEnabled = !currentSettings->reverbEnabled;
        if (currentSettings->reverbEnabled) {
            reverbTailBlocks = 0;
        } else {
            // Let the tail ring out, then stop paying for the reverb.
            reverbTailBlocks = reverbTailBlocksFull;
        }
        SaveSettingsDebounced();
    }

    // switches[1] (FS1 on the enclosure): model engine on/off
    if (hw.switches[1].RisingEdge()) {
        const bool enabling = !currentSettings->namEnabled;
        if (enabling) {
            if (dspFaultLatched) {
                // Recovery dropped the IR spectrum, so a rewind is not enough:
                // ask the main loop to reload the model from QSPI (which
                // re-verifies its CRC).
                dspFaultLatched = false;
                pendingLoadIndex = currentSettings->modelIndex;
            }
            namProcessor.reset();
            irProcessor.resetState();
            eq.Reset();
            cpuOverloadTripped = false;
            loadMeter.Reset();
            overloadWarmupBlocks = CPU_LOAD_WARMUP_BLOCKS;
            overloadGraceBlocks = CPU_OVERLOAD_GRACE_BLOCKS;
            overloadStreakBlocks = 0;
            currentSettings->namEnabled = 1;
        } else {
            currentSettings->namEnabled = 0;
        }
        SaveSettingsDebounced();
    }
}

// Deadbanded knob read. Returns true when the pot has moved far enough that
// the parameter should be re-applied.
static bool UpdateKnob(float knobRaw, float& stored) {
    if (std::abs(knobRaw - stored) <= KNOB_DEADBAND) return false;
    stored = knobRaw;
    return true;
}

void HandleKnobs() {
    if (UpdateKnob(hw.knobs[0].Value(), currentInputGainKnob))
        inputGain.SetGain(KnobToNormalized(currentInputGainKnob));
    if (UpdateKnob(hw.knobs[1].Value(), currentOutputVolumeKnob))
        outputVolume.SetGain(KnobToNormalized(currentOutputVolumeKnob));
    if (UpdateKnob(hw.knobs[2].Value(), currentReverbMixKnob))
        reverbProcessor.setMix(currentReverbMixKnob);
    if (UpdateKnob(hw.knobs[3].Value(), currentBassKnob))
        eq.SetBass(KnobToNormalized(currentBassKnob));
    if (UpdateKnob(hw.knobs[4].Value(), currentMidKnob))
        eq.SetMid(KnobToNormalized(currentMidKnob));
    if (UpdateKnob(hw.knobs[5].Value(), currentTrebleKnob))
        eq.SetTreble(KnobToNormalized(currentTrebleKnob));
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
// The DSP chain proper. Split out of AudioCallback so the callback can always
// run the control, relay and LED housekeeping around it, whichever of the DSP
// early-outs is taken.
static void ProcessAudioDsp(daisy::AudioHandle::InputBuffer in,
                            daisy::AudioHandle::OutputBuffer out,
                            size_t size) {
    // Fast-path silence when audio is suppressed (mute window / model load).
    if (audioSuppressed) {
        for (size_t i = 0; i < size; ++i) { out[0][i] = 0.0f; out[1][i] = 0.0f; }
        return;
    }

    // True-bypass path (both effects off). The relay routes the analog
    // signal around the DSP; keep the codec path as dual mono until the
    // hardware confirms whether DAC silence is preferable here.
    if (!currentSettings->namEnabled && !currentSettings->reverbEnabled) {
        for (size_t i = 0; i < size; ++i) {
            const float x = in[0][i];
            out[0][i] = x;
            out[1][i] = x;
        }
        // A pending tail can never ring out from here, and leaving the counter
        // armed would make the next MDL-on run the reverb over silence for
        // three seconds. Drop it.
        reverbTailBlocks = 0;
        return;
    }

    // -- DSP path --
    // 1) Input gain (per-sample tick keeps smoothing rate = sample rate).
    for (size_t i = 0; i < size; ++i) scratchDry[i] = in[0][i] * inputGain.Tick();

    // 2) Selected model engine: NAM A2 Lite and/or cabinet IR.
    if (currentSettings->namEnabled && IsValidModelIndex(currentSettings->modelIndex)) {
        const ModelEntry& entry = model_entries[currentSettings->modelIndex];
        
        // Pass 1: NAM
        if ((entry.type == ModelType::NamOnly || entry.type == ModelType::NamAndIr) && namProcessor.isModelLoaded()) {
            namProcessor.process(scratchDry, scratchMid, size);
        } else {
            for (size_t i = 0; i < size; ++i) scratchMid[i] = scratchDry[i];
        }
        
        // Pass 2: IR
        if ((entry.type == ModelType::IrOnly || entry.type == ModelType::NamAndIr) && irProcessor.isLoaded()) {
            irProcessor.processBlock(scratchMid, scratchWet, size);
        } else {
            for (size_t i = 0; i < size; ++i) scratchWet[i] = scratchMid[i];
        }
        
        // Catch a non-finite sample before it reaches the EQ or the reverb.
        // Both recirculate their output into their own state, so one NaN here
        // silences the pedal until power-cycled. Silencing the block and
        // handing recovery to the main loop keeps the reverb usable.
        if (fguard::SilenceIfNonFinite(scratchWet, size)) {
            dspFaultPending = true;
        }

        // 3) Tone stack (only when model engine is on — pass-through otherwise).
        eq.ProcessBlock(scratchWet, scratchWet, size);
    } else {
        // Pass through the dry path (model engine off) as if it were the "wet".
        for (size_t i = 0; i < size; ++i) scratchWet[i] = scratchDry[i];
    }

    // 4) Reverb + output volume + stereo. When reverb is off we still write
    //    to both output channels for consistent monitoring.
    //    After the reverb is switched off it keeps running for a bounded
    //    number of blocks so the tail decays naturally. True bypass routes
    //    around the DSP entirely, so trails only apply while the model
    //    engine keeps the DSP path alive.
    // Both tanks only fit in a block when the model engine is off; that is
    // also the only configuration worth A/B-ing the reverb in, so it is where
    // seamless switching is offered.
    reverbProcessor.setCompareMode(!currentSettings->namEnabled);

    if (currentSettings->reverbEnabled || reverbTailBlocks > 0) {
        for (size_t i = 0; i < size; ++i) {
            float l, r;
            // If reverb is disabled, we feed silence (0.0f) to the reverb input to let it decay,
            // while mixing the dry signal as normal.
            const float reverbInput = currentSettings->reverbEnabled ? scratchWet[i] : 0.0f;
            reverbProcessor.process(reverbInput, scratchWet[i], &l, &r);
            const float g = outputVolume.Tick();
            out[0][i] = l * g;
            out[1][i] = r * g;
        }
        if (!currentSettings->reverbEnabled) --reverbTailBlocks;
    } else {
        for (size_t i = 0; i < size; ++i) {
            const float g = outputVolume.Tick();
            const float y = scratchWet[i] * g;
            out[0][i] = y;
            out[1][i] = y;
        }
    }
}

void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t size) {
    loadMeter.OnBlockStart();

    // Clamp to our scratch capacity as a safety measure.
    if (size > kMaxBlock) size = kMaxBlock;

    // -- Controls in, before the DSP so a footswitch takes effect this block --
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();
    HandleKnobs();
    HandleFootswitches();
    HandleEncoderMovement();
    HandleEncoderClick();

    // -- True-bypass relay and analog mute --
    // Re-evaluated every block rather than only on footswitch edges, so the
    // relay also tracks the overload and DSP-fault paths that turn the model
    // engine off from underneath the UI.
    UpdateBypassRelay();
    StepBypassTiming((int32_t)size);
    hw.SetAudioBypass(relayBypassOn);
    hw.SetAudioMute(relayMuteOn);

    ProcessAudioDsp(in, out, size);

    // -- LEDs out. Led::Update() advances the software-PWM sawtooth, so it has
    // to be called at a steady rate or the LEDs simply do not light. --
    hw.SetLed(0, currentSettings->reverbEnabled ? 1.0f : 0.0f);
    hw.SetLed(1, currentSettings->namEnabled ? 1.0f : 0.0f);
    hw.UpdateLeds();

    loadMeter.OnBlockEnd();

    // If the chain no longer fits in a block period the main loop stops
    // running and the pedal looks dead. Shed the most expensive stage from
    // here (the only context still scheduled) so the UI stays alive.
    //
    // Only once the reading is trustworthy, though: see the CPU_LOAD_* notes
    // in constants.h for why a raw first-block reading is not.
    if (!currentSettings->namEnabled) {
        overloadStreakBlocks = 0;
    } else if (overloadWarmupBlocks > 0 && --overloadWarmupBlocks == 0) {
        // Throw away the warm-up, cold caches and engine resets included, so
        // both the trip decision and the number on the display describe the
        // steady state.
        loadMeter.Reset();
    } else if (overloadGraceBlocks > 0) {
        --overloadGraceBlocks;
    } else if (!cpuOverloadTripped) {
        const float load = loadMeter.GetAvgCpuLoad();
        // -Ofast folds std::isnan away; the meter parks NaN in avg_ on Reset().
        if (fguard::IsNonFinite(load) || load <= CPU_OVERLOAD_THRESHOLD) {
            overloadStreakBlocks = 0;
        } else if (++overloadStreakBlocks >= CPU_OVERLOAD_TRIP_BLOCKS) {
            overloadAvgAtTrip = load;
            overloadMaxAtTrip = loadMeter.GetMaxCpuLoad();
            cpuOverloadTripped = true;
            currentSettings->namEnabled = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Main-loop work
//
// Anything the audio thread deferred because it blocks, paints the display or
// touches QSPI.
// ---------------------------------------------------------------------------
void CheckPreviewTimeout() {
    if (!isPreviewingModel) return;

    const uint32_t now = daisy::System::GetNow();
    if (now - previewStartTime > PREVIEW_TIMEOUT_MS) {
        previewModelIndex = IsValidModelIndex(currentSettings->modelIndex)
                                ? currentSettings->modelIndex
                                : 0;
        isPreviewingModel = false;
    }
}

// The encoder click asked for a model swap. Loading paints the display and
// reads QSPI, neither of which belongs on the audio thread.
void HandlePendingLoad() {
    const int target = pendingLoadIndex;
    if (target < 0) return;
    pendingLoadIndex = -1;
    LoadModel(target);
}

// Recover from a non-finite sample reported by the audio thread. Everything
// here is too slow for the ISR: Dattorro::clear() memsets the whole SDRAM
// tank. Shut the model engine down rather than re-enabling it blind — the data
// or the model is bad, and looping through the fault would just stutter.
void HandleDspFault() {
    if (!dspFaultPending) return;
    dspFaultPending = false;
    dspFaultLatched = true;

    audioSuppressed = true;
    currentSettings->namEnabled = 0;
    namProcessor.reset();
    irProcessor.clear();
    eq.Reset();
    reverbProcessor.clear();
    loadMeter.Reset();
    audioSuppressed = false;
}

void CheckSettingsSave() {
    if (!settingsDirty) return;
    const uint32_t now = daisy::System::GetNow();
    if (now - lastSettingsChange > SAVE_DELAY_MS) {
        settings.Save();
        settingsDirty = false;
    }
}

// ---------------------------------------------------------------------------
// Init helpers
// ---------------------------------------------------------------------------
static void InitReverbOrHalt() {
    // Install the SDRAM arena BEFORE constructing Dattorro so all InterpDelay
    // instances land there. If this fails the pedal is unusable, so surface
    // it on the display and hang; better than a silent crash on hardware.
    InterpDelayArena::set(g_reverb_arena, kReverbArenaFloats);
    reverbProcessor.init(hw.AudioSampleRate());
    if (InterpDelayArena::exhausted()) {
        // The arena wasn't big enough: some delay lines fell back to heap
        // (or failed). Treat this as fatal so the NAM heap is not consumed
        // unpredictably by reverb buffers.
        ShowMessage("Reverb arena", "underprovisioned");
        while (true) {
            hw.SetLed(0, 1.0f); hw.SetLed(1, 1.0f); hw.UpdateLeds();
            hw.DelayMs(100);
            hw.SetLed(0, 0.0f); hw.SetLed(1, 0.0f); hw.UpdateLeds();
            hw.DelayMs(100);
        }
    }
    // After construction, further InterpDelay creation shouldn't happen; the
    // reverb doesn't dynamically add delays. Clear the arena pointer so any
    // stray InterpDelay would fall back to heap and be caught in review.
    InterpDelayArena::set(nullptr, 0);
}

// libDaisy's startup code only zeroes .bss. The tiered-memory sections we
// place NAM state into are NOLOAD and never cleared, so anything relying on
// zero-initialization there (notably SharedWeights::loaded) starts as whatever
// the RAM powered up with. Clear them before anything reads them.
extern "C" {
extern uint32_t __dtcmram_bss_start__, __dtcmram_bss_end__;
extern uint32_t __sram_d2_bss_start__, __sram_d2_bss_end__;
}

static void ZeroUninitSections() {
    for (uint32_t* p = &__dtcmram_bss_start__; p < &__dtcmram_bss_end__; ++p) *p = 0;
    for (uint32_t* p = &__sram_d2_bss_start__; p < &__sram_d2_bss_end__; ++p) *p = 0;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(void) {
    ZeroUninitSections();

    hw.Init(kAudioBlock, true);
    hw.SetAudioBlockSize(kAudioBlock);
    static_assert(kAudioBlock <= kMaxBlock, "Audio block exceeds scratch capacity");
    static_assert(kAudioBlock == nam_a2::kBlockSize,
                  "Audio block should match the NAM inference block for a flat load");

    // Settings — must be initialized before anything reads currentSettings.
    settings.Init(hw.seed.qspi, SETTINGS_QSPI_OFFSET);
    settings.ValidateSettings(MODEL_COUNT);
    currentSettings = &settings.GetSettings();

    ShowMessage("Initializing", "AmpSim");

    // DSP init (no allocations here).
    inputGain.Init(hw.AudioSampleRate());
    outputVolume.Init(hw.AudioSampleRate());
    eq.Init(hw.AudioSampleRate());
    irProcessor.init(g_ir_freq_buf, g_ir_fdl_buf);
    loadMeter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    reverbTailBlocksFull = (int32_t)(REVERB_TAIL_SECONDS * hw.AudioSampleRate()
                                     / (float)hw.AudioBlockSize());
    bypassToggleTransitionSamples =
        (int32_t)(BYPASS_TOGGLE_TRANSITION_S * hw.AudioSampleRate());
    muteOffTransitionSamples =
        (int32_t)(MUTE_OFF_TRANSITION_S * hw.AudioSampleRate());

    // Reverb (allocates from SDRAM arena).
    InitReverbOrHalt();

    // Physical knobs are the source of truth; defaults are only used until
    // ADC readings are available below.
    inputGain.SetGain(KnobToNormalized(currentInputGainKnob));
    outputVolume.SetGain(KnobToNormalized(currentOutputVolumeKnob));
    reverbProcessor.setMix(currentReverbMixKnob);
    eq.SetBass(KnobToNormalized(currentBassKnob));
    eq.SetMid(KnobToNormalized(currentMidKnob));
    eq.SetTreble(KnobToNormalized(currentTrebleKnob));

    // Enable NAM loudness normalization to -18 dBFS-ish.
    namProcessor.setSampleRate(hw.AudioSampleRate());
    namProcessor.setLoudnessTarget(-18.0f);

    if (MODEL_COUNT == 0) {
        ShowMessage("No models!", "Use build_capture_blob.py");
    } else {
        previewModelIndex = IsValidModelIndex(currentSettings->modelIndex)
                                ? currentSettings->modelIndex
                                : 0;
        LoadModel(previewModelIndex);
    }

    // Restore the bypass state from persisted settings without running the
    // mute sequence: at boot there is nothing to pop, and priming
    // currentBypassState here is what stops the callback from firing a
    // spurious transition on its first block.
    currentBypassState = !currentSettings->namEnabled && !currentSettings->reverbEnabled;
    relayBypassOn = currentBypassState;
    relayMuteOn = false;
    hw.SetAudioBypass(relayBypassOn);
    hw.SetAudioMute(relayMuteOn);

    // Let the ADC settle so the first HandleKnobs() call sees real pot
    // positions rather than half-charged sample-and-holds; otherwise the
    // deadband latches a bogus value and the knobs feel dead until moved.
    hw.StartAdc();
    for (int i = 0; i < 20; ++i) {
        hw.ProcessAllControls();
        hw.DelayMs(1);
    }
    HandleKnobs();

    // From here the audio callback owns the controls, the LEDs and the relay.
    // The main loop must not touch them.
    hw.StartAudio(AudioCallback);

    while (true) {
        HandlePendingLoad();
        CheckPreviewTimeout();
        HandleDspFault();
        CheckSettingsSave();

        const uint32_t now = daisy::System::GetNow();
        if (now - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL_MS) {
            UpdateDisplay();
            lastDisplayUpdate = now;
        }
    }
}
