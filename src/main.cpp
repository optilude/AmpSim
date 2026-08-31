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
bool audioStarted = false;

// True-bypass relay state tracking.
bool currentBypassState = false;

// Audio callback timing. The full chain (NAM + IR + EQ + reverb) is close to
// the per-block budget, so the load is measured and surfaced on the display.
daisy::CpuLoadMeter loadMeter;

// Safety valve: if the callback consistently cannot finish inside its block
// period the main loop is starved and the pedal appears frozen. Disable the
// model engine from the audio thread instead so control is never lost.
volatile bool cpuOverloadTripped = false;

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

static void ApplyControlTargetsOnAudioThread() {
    static float appliedInputGain = -1.0f;
    static float appliedOutputVolume = -1.0f;
    static float appliedReverbMix = -1.0f;
    static float appliedBass = -1.0f;
    static float appliedMid = -1.0f;
    static float appliedTreble = -1.0f;

    if (currentInputGainKnob != appliedInputGain) {
        appliedInputGain = currentInputGainKnob;
        inputGain.SetGain(KnobToNormalized(appliedInputGain));
    }
    if (currentOutputVolumeKnob != appliedOutputVolume) {
        appliedOutputVolume = currentOutputVolumeKnob;
        outputVolume.SetGain(KnobToNormalized(appliedOutputVolume));
    }
    if (currentReverbMixKnob != appliedReverbMix) {
        appliedReverbMix = currentReverbMixKnob;
        reverbProcessor.setMix(appliedReverbMix);
    }
    if (currentBassKnob != appliedBass) {
        appliedBass = currentBassKnob;
        eq.SetBass(KnobToNormalized(appliedBass));
    }
    if (currentMidKnob != appliedMid) {
        appliedMid = currentMidKnob;
        eq.SetMid(KnobToNormalized(appliedMid));
    }
    if (currentTrebleKnob != appliedTreble) {
        appliedTreble = currentTrebleKnob;
        eq.SetTreble(KnobToNormalized(appliedTreble));
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void SaveSettingsDebounced() {
    settingsDirty = true;
    lastSettingsChange = daisy::System::GetNow();
}

// Manage the true-bypass relay. Both effects OFF => relay engaged (analog
// pass-through). Otherwise DSP is active. Follows bkshepherd's pattern:
// mute the analog output, wait, flip the relay, wait, unmute.
void UpdateBypassRelay() {
    const bool shouldBypass = !currentSettings->namEnabled
                           && !currentSettings->reverbEnabled;

    if (shouldBypass == currentBypassState) return;

    // Suppress DSP output before muting to guarantee silence at the DAC
    // (the mute pin muffs the analog output but the DAC keeps producing).
    audioSuppressed = true;
    hw.SetAudioMute(true);
    hw.DelayMs(MUTE_DELAY_MS);

    hw.SetAudioBypass(shouldBypass);

    hw.DelayMs(MUTE_DELAY_MS);
    hw.SetAudioMute(false);
    audioSuppressed = false;

    currentBypassState = shouldBypass;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
// 128x64 SSD1306. Font_6x8 => max 21 chars per line; Font_7x10 => max 18.
// Text longer than that is truncated by the OLED driver, not our concern
// beyond aesthetics — we still snprintf-clip our own buffers.
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
    snprintf(line, sizeof line, "MDL:%s REV:%s",
             currentSettings->namEnabled ? "ON " : "OFF",
             currentSettings->reverbEnabled ? "ON " : "OFF");
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
        hw.display.WriteString("CPU OVERLOAD: MDL off", Font_6x8, true);
    } else {
        const float load = loadMeter.GetAvgCpuLoad();
        const int cpuPct = fguard::IsNonFinite(load) ? 0 : (int)std::lround(load * 100.0f);
        snprintf(line, sizeof line, "CPU%3d%% FS1:M FS2:R", cpuPct);
        hw.display.WriteString(line, Font_6x8, true);
    }

    hw.display.Update();
}

// Show a fullscreen message. Used for load progress and error surfaces.
static void ShowMessage(const char* line0, const char* line1 = nullptr) {
    hw.display.Fill(false);
    hw.display.SetCursor(0, 0);
    hw.display.WriteString(line0, Font_7x10, true);
    if (line1) {
        hw.display.SetCursor(0, 14);
        hw.display.WriteString(line1, Font_6x8, true);
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
        ShowMessage("Bad model data", "reflash: make program");
        hw.DelayMs(ERROR_DISPLAY_TIME_MS);
        return;
    }

    // Silence and stop the callback while NAM allocates and swaps model state.
    // This mirrors the intentional brief mute during model changes.
    audioSuppressed = true;
    if (audioStarted) hw.StopAudio();
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
    
    if (audioStarted) hw.StartAudio(AudioCallback);
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
// Audio
// ---------------------------------------------------------------------------
void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t size) {
    loadMeter.OnBlockStart();

    // Clamp to our scratch capacity as a safety measure.
    if (size > kMaxBlock) size = kMaxBlock;

    // Fast-path silence when audio is suppressed (mute window / model load).
    if (audioSuppressed) {
        for (size_t i = 0; i < size; ++i) { out[0][i] = 0.0f; out[1][i] = 0.0f; }
        loadMeter.OnBlockEnd();
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
        loadMeter.OnBlockEnd();
        return;
    }

    ApplyControlTargetsOnAudioThread();

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

    loadMeter.OnBlockEnd();

    // If the chain no longer fits in a block period the main loop stops
    // running and the pedal looks dead. Shed the most expensive stage from
    // here (the only context still scheduled) so the UI stays alive.
    if (!cpuOverloadTripped && currentSettings->namEnabled
        && loadMeter.GetAvgCpuLoad() > CPU_OVERLOAD_THRESHOLD) {
        cpuOverloadTripped = true;
        currentSettings->namEnabled = 0;
    }
}

// ---------------------------------------------------------------------------
// Controls
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

    if (isPreviewingModel) {
        // Snapshot the target before we clear the flag so that if the
        // timeout races with this callback we still load what the user saw.
        const int target = previewModelIndex;
        isPreviewingModel = false;
        LoadModel(target);
    }
}

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
        hw.SetLed(0, currentSettings->reverbEnabled ? 1.0f : 0.0f);
        hw.UpdateLeds();
        UpdateBypassRelay();
        SaveSettingsDebounced();
    }

    // switches[1] (FS1 on the enclosure): model engine on/off
    if (hw.switches[1].RisingEdge()) {
        const bool enabling = !currentSettings->namEnabled;
        if (enabling) {
            if (dspFaultLatched) {
                // Recovery dropped the IR spectrum, so a rewind is not enough:
                // reload the model from QSPI (which re-verifies its CRC).
                dspFaultLatched = false;
                LoadModel(currentSettings->modelIndex);
            }
            // reset()/resetState() rewind state the audio callback is actively
            // reading, so silence the DSP for the duration.
            audioSuppressed = true;
            namProcessor.reset();
            irProcessor.resetState();
            eq.Reset();
            cpuOverloadTripped = false;
            loadMeter.Reset();
            currentSettings->namEnabled = 1;
            audioSuppressed = false;
        } else {
            currentSettings->namEnabled = 0;
        }
        hw.SetLed(1, currentSettings->namEnabled ? 1.0f : 0.0f);
        hw.UpdateLeds();
        UpdateBypassRelay();
        SaveSettingsDebounced();
    }
}

// Deadbanded knob update helper. Returns true if a change was applied.
template <typename Apply>
static bool UpdateKnob(float knobRaw, float& stored, Apply apply) {
    if (std::abs(knobRaw - stored) <= KNOB_DEADBAND) return false;
    stored = knobRaw;
    apply(knobRaw);
    return true;
}

void HandleKnobs() {
    UpdateKnob(hw.knobs[0].Value(), currentInputGainKnob,
               [](float) {});
    UpdateKnob(hw.knobs[1].Value(), currentOutputVolumeKnob,
               [](float) {});
    UpdateKnob(hw.knobs[2].Value(), currentReverbMixKnob,
               [](float) {});
    UpdateKnob(hw.knobs[3].Value(), currentBassKnob,
               [](float) {});
    UpdateKnob(hw.knobs[4].Value(), currentMidKnob,
               [](float) {});
    UpdateKnob(hw.knobs[5].Value(), currentTrebleKnob,
               [](float) {});
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

    hw.SetLed(1, 0.0f);
    UpdateBypassRelay();
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

    // Restore LEDs / bypass state from persisted settings.
    hw.SetLed(0, currentSettings->reverbEnabled ? 1.0f : 0.0f);
    hw.SetLed(1, currentSettings->namEnabled ? 1.0f : 0.0f);
    hw.UpdateLeds();
    currentBypassState = !currentSettings->namEnabled && !currentSettings->reverbEnabled;
    hw.SetAudioBypass(currentBypassState);

    hw.StartAdc();
    for (int i = 0; i < 20; ++i) {
        hw.ProcessAllControls();
        hw.DelayMs(1);
    }
    HandleKnobs();

    hw.StartAudio(AudioCallback);
    audioStarted = true;

    while (true) {
        hw.ProcessAllControls();

        HandleEncoderMovement();
        HandleEncoderClick();
        CheckPreviewTimeout();

        HandleFootswitches();
        HandleKnobs();
        HandleDspFault();

        // Software PWM: the LEDs only advance their sawtooth when Update() is
        // called. Driving it from footswitch edges alone meant they never lit.
        hw.UpdateLeds();

        // The audio thread sheds the model engine if the callback stops
        // fitting in its block period; mirror that onto the LED and relay.
        static bool overloadHandled = false;
        if (cpuOverloadTripped && !overloadHandled) {
            overloadHandled = true;
            hw.SetLed(1, 0.0f);
            hw.UpdateLeds();
            UpdateBypassRelay();
        } else if (!cpuOverloadTripped) {
            overloadHandled = false;
        }

        CheckSettingsSave();

        const uint32_t now = daisy::System::GetNow();
        if (now - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL_MS) {
            UpdateDisplay();
            lastDisplayUpdate = now;
        }
    }
}
