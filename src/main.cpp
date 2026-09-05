// AmpSim - Guitar Amp Simulator based on Daisy Seed
// Copyright (c) 2025 AmpSim Contributors
//
// LICENSING NOTE: This project includes GPL v3-licensed components
// (Dattorro reverb from Flick project) and an LGPL v2.1-licensed component
// (DaisySP-LGPL's ReverbSc, behind the Settings menu's "Simple" reverb
// engine). See LICENSE and THIRD_PARTY.md for full licensing details and
// compliance information.
//
// NAM A2 processing with a choice of two reverb engines (Dattorro plate or
// ReverbSc, selectable in the Settings menu) and full control scheme.
//
// Signal path (per audio block):
//   in[0] -> [inputGain] -> [NAM or IR] -> [3-band EQ] -> [reverb wet/dry] -> [outputVolume] -> out[0,1]
//
// A model is a NAM capture or a cabinet IR, never both: chaining them costs
// both engines' budgets in one block and does not fit alongside the reverb.
//
// The dry+wet mix is applied inside ReverbProcessor::process (Dattorro) or
// SimpleReverbProcessor::process (ReverbSc). When both effects are off, the
// true-bypass relay handles the analog path and the callback echoes silence
// (the codec output is muted while the relay flips and the analog signal is
// routed around the DSP).

#include "guitar_pedal_125b.h"
#include "nam_processor.h"
#include "reverb_processor.h"
#include "simple_reverb_processor.h"
#include "reverb_arena.h"
#include "dattorro/dsp/delays/InterpDelay.hpp"
#include "capture_index.h"
#include "capture_verify.h"
#include "ir_processor.h"
#include "settings.h"
#include "gain_stage.h"
#include "guitar_eq.h"
#include "callback_noise_filter.h"
#include "tuner_processor.h"
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
// ReverbSc keeps its delay buffers inline in the object (~386 KiB) rather
// than behind a pluggable arena, so the whole object -- not just some
// member -- has to be placed in SDRAM directly. See simple_reverb_processor.h.
SimpleReverbProcessor DSY_SDRAM_BSS simpleReverbProcessor;
IRProcessor irProcessor;
GainStage inputGain;
GainStage outputVolume;
GuitarEQ eq;
CallbackNoiseFilter callbackNoiseFilter;
volatile bool callbackNoiseFilterEnabled = true;
// Allocates its ~40KB Cycfi Q detector state from the heap once, in
// TunerProcessor::init() -- see the call in main() and tuner_processor.h.
TunerProcessor tuner;

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

// Debounced settings save: SaveSettingsDebounced() marks this dirty, and
// CheckSettingsSave() in the main loop flushes it to QSPI once SAVE_DELAY_MS
// has passed with no further changes.
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

void SaveSettingsDebounced();

// ---------------------------------------------------------------------------
// Settings mode
//
// A long-press on the encoder takes over the whole screen and suspends audio.
// SettingsMenu scrolls between items; SettingsEdit scrolls between an item's
// options. A second click in SettingsEdit commits the value and returns to
// SettingsMenu. Reset is modeled as a two-option item (Cancel/Confirm) so it
// goes through the same click-to-edit / click-to-commit flow as a real
// setting, rather than firing on a single click.
// ---------------------------------------------------------------------------
enum class UiMode { Normal, SettingsMenu, SettingsEdit };
enum BufferedBypassMode { BypassRelay = 0, BypassDirect = 1, BypassMonoToStereo = 2 };
enum ReverbEngineType { ReverbEngineDattorro = 0, ReverbEngineSimple = 1 };

volatile UiMode uiMode = UiMode::Normal;
static inline bool InSettingsMode() { return uiMode != UiMode::Normal; }

int settingsCursor = 0;                  // menu item under the cursor
int settingsEditValue = 0;               // candidate value while in SettingsEdit
bool encoderLongPressFired = false;      // latch so long-press fires once per hold
volatile bool pendingReset = false;      // main loop performs the reboot

// ---------------------------------------------------------------------------
// Tuner mode
//
// Not folded into UiMode: InSettingsMode() (uiMode != Normal) disables
// footswitches entirely (see AudioCallback), but FS2 must stay live while the
// tuner is open so it can exit. A separate flag keeps that independent.
// ---------------------------------------------------------------------------
volatile bool tunerMode = false;
bool fs2LongPressFired = false;          // latch so the hold fires once per press
bool reverbBeforeFs2Press = false;       // undoes FS2's instant toggle if the hold completes

static const char* const kMonoOutOptions[] = {"Off", "On"};
static const char* const kBypassOptions[] = {"True", "Direct", "Mono>Str"};
static const char* const kReverbEngineOptions[] = {"Dattorro", "Simple"};
static const char* const kResetOptions[] = {"Cancel", "Confirm"};

struct SettingsMenuItem {
    const char* name;
    const char* const* options;   // nullptr => immediate action on click (Back only)
    uint8_t optionCount;
};

static const SettingsMenuItem kSettingsMenu[] = {
    {"Mono out", kMonoOutOptions,       2},
    {"Bypass",   kBypassOptions,        3},
    {"Reverb",   kReverbEngineOptions,  2},
    {"Reset",    kResetOptions,         2},
    {"Back",     nullptr,               0},
};
static constexpr int kSettingsMenuCount = sizeof(kSettingsMenu) / sizeof(kSettingsMenu[0]);

// Current stored value for a menu item, as an option index.
static int GetMenuItemValue(int item) {
    switch (item) {
        case 0: return currentSettings->monoOutput ? 1 : 0;
        case 1: return currentSettings->bufferedBypassMode;
        case 2: return currentSettings->reverbEngine;
        case 3: return 1;  // Reset defaults to Confirm: click, click resets
        default: return 0;
    }
}

// Commit an edited option index back into settings/state.
static void CommitMenuItemValue(int item, int value) {
    switch (item) {
        case 0:
            currentSettings->monoOutput = (uint8_t)value;
            SaveSettingsDebounced();
            break;
        case 1:
            currentSettings->bufferedBypassMode = (uint8_t)value;
            SaveSettingsDebounced();
            break;
        case 2:
            currentSettings->reverbEngine = (uint8_t)value;
            SaveSettingsDebounced();
            break;
        case 3:
            if (value == 1) pendingReset = true;
            break;
        default:
            break;
    }
}

static void EnterSettingsMode() {
    // Cancel any in-flight preview/load so it cannot paint over the menu.
    isPreviewingModel = false;
    pendingLoadIndex = -1;
    settingsCursor = 0;
    uiMode = UiMode::SettingsMenu;
}

static void ExitSettingsMode() {
    uiMode = UiMode::Normal;
}

// Audio callback timing. The heaviest chain (NAM + EQ + reverb) is close to
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

// Which knob (if any) was most recently turned, and when. The display uses
// this to show a transient name/value overlay instead of the model screen;
// see UpdateDisplay().
enum class ActiveKnob { None, InputGain, OutputVolume, ReverbMix, Bass, Mid, Treble };
ActiveKnob lastActiveKnob = ActiveKnob::None;
uint32_t lastKnobActivityMs = 0;

// Audio block size. 48 matches the NAM A2 inference block exactly (one
// inference per callback, flat load instead of the 2-or-3 the old 128 gave)
// and puts the control/LED update rate at 1 kHz, which is what libDaisy's
// Switch and Encoder debouncing is tuned for. Same value bkshepherd uses.
static constexpr size_t kAudioBlock = 48;

// Scratch buffers for block-based DSP.
static constexpr size_t kMaxBlock = 128;
float scratchDry[kMaxBlock];
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
// The buffered bypass modes (Direct / Mono-to-Stereo) keep the relay
// permanently disengaged -- that is the point of them, signal always runs
// through the codec. Settings mode forces the relay disengaged too, so the
// DSP-silence fast path is what actually mutes the output there.
static inline void UpdateBypassRelay() {
    const bool shouldBypass = currentSettings->bufferedBypassMode == BypassRelay
                           && !InSettingsMode()
                           && !tunerMode
                           && !currentSettings->namEnabled
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
// 128x64 SSD1306. Font_6x8 => max 21 chars per line; Font_7x10 => max 18;
// Font_11x18 => max 11 chars. Text longer than that is truncated by the OLED
// driver, not our concern beyond aesthetics — we still snprintf-clip our own
// buffers.
//
// Normal display is deliberately sparse: everything a knob position or an LED
// already tells the player (gain, EQ, effect on/off) is left off the screen.
// The main area shows the model and variant; turning a knob temporarily
// replaces that with its name and value (see ActiveKnob / UpdateDisplay), and
// a one-line status bar at the bottom carries CPU load plus which of
// IR/NAM/REV are actually in circuit.

// A CpuLoadMeter reading as a percent, clamped to three digits so it cannot
// push the rest of the line off the display. NaN is what the meter holds
// between Reset() and the first block end.
static int LoadPercent(float load) {
    if (fguard::IsNonFinite(load) || load <= 0.0f) return 0;
    const int pct = (int)std::lround(load * 100.0f);
    return pct > 999 ? 999 : pct;
}

// Font_11x18 fits 11 whole characters per 128px line (12 would need 132px);
// see ClipForDisplay.
static constexpr size_t kBigFontChars = 11;
// Font_7x10 fits 18 whole characters per 128px line (19 would need 133px).
// Used while previewing a model on the encoder: that's exactly when the
// player is comparing similar names, so trading the bigger font for less
// truncation is worth it.
static constexpr size_t kSmallFontChars = 18;

// Clips src to at most maxChars characters, marking a real cut with a
// trailing '~' so a shortened model/variant name reads as shortened rather
// than as a different, shorter one. Without this the OLED driver's own
// per-character bounds check silently drops whatever doesn't fit.
static void ClipForDisplay(char* dst, size_t dstSize, const char* src, size_t maxChars) {
    const size_t limit = std::min(maxChars, dstSize - 1);
    if (strlen(src) <= limit) {
        snprintf(dst, dstSize, "%s", src);
        return;
    }
    snprintf(dst, dstSize, "%.*s", (int)limit, src);
    if (limit > 0) dst[limit - 1] = '~';
}

// Right-edge-aligned WriteString: draws str so its last character lands on
// the display's right edge instead of growing rightward from the cursor.
static void WriteStringRightAligned(const char* str, FontDef font, int y) {
    const int w = (int)(strlen(str) * font.FontWidth);
    const int x = std::max(0, (int)hw.display.Width() - w);
    hw.display.SetCursor(x, y);
    hw.display.WriteString(str, font, true);
}

// Name/value strings for the knob currently shown in the overlay. Both
// buffers are expected to be at least 12 bytes (11 chars + NUL, Font_11x18's
// limit).
static void GetKnobDisplayText(ActiveKnob knob, char* nameBuf, size_t nameLen,
                               char* valueBuf, size_t valueLen) {
    switch (knob) {
        case ActiveKnob::InputGain:
            snprintf(nameBuf, nameLen, "In Gain");
            snprintf(valueBuf, valueLen, "%+d dB",
                     (int)std::lround(KnobToNormalized(currentInputGainKnob) * GAIN_RANGE_DB));
            break;
        case ActiveKnob::OutputVolume:
            snprintf(nameBuf, nameLen, "Out Vol");
            snprintf(valueBuf, valueLen, "%+d dB",
                     (int)std::lround(KnobToNormalized(currentOutputVolumeKnob) * GAIN_RANGE_DB));
            break;
        case ActiveKnob::ReverbMix:
            snprintf(nameBuf, nameLen, "Reverb");
            snprintf(valueBuf, valueLen, "%d%%", (int)std::lround(currentReverbMixKnob * 100.0f));
            break;
        case ActiveKnob::Bass:
            snprintf(nameBuf, nameLen, "Bass");
            snprintf(valueBuf, valueLen, "%+d dB",
                     (int)std::lround(KnobToNormalized(currentBassKnob) * EQ_RANGE_DB));
            break;
        case ActiveKnob::Mid:
            snprintf(nameBuf, nameLen, "Mid");
            snprintf(valueBuf, valueLen, "%+d dB",
                     (int)std::lround(KnobToNormalized(currentMidKnob) * EQ_RANGE_DB));
            break;
        case ActiveKnob::Treble:
            snprintf(nameBuf, nameLen, "Treble");
            snprintf(valueBuf, valueLen, "%+d dB",
                     (int)std::lround(KnobToNormalized(currentTrebleKnob) * EQ_RANGE_DB));
            break;
        case ActiveKnob::None:
            nameBuf[0] = '\0';
            valueBuf[0] = '\0';
            break;
    }
}

// Full-screen settings menu/edit paint. Follows the same Fill/SetCursor/
// WriteString/Update idiom as UpdateDisplay() below.
static void DrawSettingsScreen() {
    hw.display.Fill(false);

    char line[32];

    hw.display.SetCursor(0, 0);
    hw.display.WriteString("SETTINGS", Font_7x10, true);

    for (int i = 0; i < kSettingsMenuCount; ++i) {
        const SettingsMenuItem& item = kSettingsMenu[i];
        const bool onCursor = (i == settingsCursor);
        const bool editingThis = onCursor && uiMode == UiMode::SettingsEdit;

        char valueBuf[16] = "";
        if (item.options != nullptr) {
            const int value = editingThis ? settingsEditValue : GetMenuItemValue(i);
            if (editingThis) {
                snprintf(valueBuf, sizeof valueBuf, "[%s]", item.options[value]);
            } else {
                snprintf(valueBuf, sizeof valueBuf, "%s", item.options[value]);
            }
        }

        hw.display.SetCursor(0, 14 + i * 10);
        snprintf(line, sizeof line, "%c%-9s %10s",
                 onCursor ? '>' : ' ', item.name, valueBuf);
        hw.display.WriteString(line, Font_6x8, true);
    }

    hw.display.Update();
}

// Ported from bkshepherd's DaisySeedProjects GuitarPedal TunerModule::DrawUI
// (Effect-Modules/tuner_module.cpp, MIT) -- same 21-block strip, note/octave
// readout and cents-to-blocks mapping, adapted to this project's manual
// SetCursor/WriteString-and-WriteStringAligned display convention.
static void DrawTunerScreen() {
    hw.display.Fill(false);

    const Rectangle bounds(0, 0, 128, 64);
    static const char* const kNoteNames[12] =
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    const bool hasPitch = tuner.hasPitch();

    if (hasPitch) {
        char noteBuf[4];
        snprintf(noteBuf, sizeof noteBuf, "%s", kNoteNames[((tuner.noteIndex() % 12) + 12) % 12]);
        hw.display.WriteStringAligned(noteBuf, Font_16x26, bounds, Alignment::topCentered, true);

        char octaveBuf[4];
        snprintf(octaveBuf, sizeof octaveBuf, "%d", tuner.octave());
        hw.display.WriteStringAligned(octaveBuf, Font_11x18, bounds, Alignment::topRight, true);
    }

    // This has to be an odd count so the middle block can mean "in tune".
    constexpr int kBlockCount = 21;
    constexpr int kInTuneBlockIndex = (kBlockCount - 1) / 2;
    constexpr int kNumBlocksOutOfTune = (kBlockCount - 1) / 2;
    constexpr float kCloseThresholdCents = 1.0f;
    // Nearly half a semitone, so the strip fills up just before the note name
    // would flip to its sharp/flat neighbour.
    constexpr float kFarLimitCents = 45.0f;

    bool blockActive[kBlockCount] = {false};

    if (hasPitch) {
        blockActive[kInTuneBlockIndex] = true;

        const float percentage = std::clamp(std::abs(tuner.cents()) / kFarLimitCents, 0.0f, 1.0f);
        int blocksToLight = static_cast<int>(kNumBlocksOutOfTune * percentage);
        if (blocksToLight < 1) blocksToLight = 1;
        if (std::abs(tuner.cents()) < kCloseThresholdCents) blocksToLight = 0;

        if (tuner.cents() < 0.0f) {
            for (int i = kInTuneBlockIndex - 1; i >= 0 && blocksToLight > 0; --i, --blocksToLight)
                blockActive[i] = true;
        } else {
            for (int i = kInTuneBlockIndex + 1; i < kBlockCount && blocksToLight > 0; ++i, --blocksToLight)
                blockActive[i] = true;
        }
    }

    // The 3-arg Rectangle overload of DrawRect (declared in the
    // OneBitGraphicsDisplay base) is hidden here: OneBitGraphicsDisplayImpl
    // redeclares DrawRect with the 6-coordinate signature and there's no
    // `using` to unhide the base overload, so this calls that one directly
    // (x1,y1)-(x2,y2) corners, not (x,y,width,height).
    const int blockWidth = 128 / kBlockCount;
    constexpr int kStripTop = 30;
    int x = 0;
    for (int block = 0; block < kBlockCount; ++block) {
        if (block == kInTuneBlockIndex) {
            hw.display.DrawRect(x, kStripTop - 5, x + blockWidth, kStripTop - 5 + 20, true, blockActive[block]);
        } else {
            hw.display.DrawRect(x, kStripTop, x + blockWidth, kStripTop + blockWidth, true, blockActive[block]);
        }
        x += blockWidth;
    }

    if (hasPitch) {
        // No %f: this build links --specs=nano.specs with no _printf_float.
        const float freq = tuner.frequency();
        char freqBuf[16];
        snprintf(freqBuf, sizeof freqBuf, "%d.%02d", (int)freq, (int)((freq - (int)freq) * 100));
        hw.display.WriteStringAligned(freqBuf, Font_7x10, bounds, Alignment::bottomCentered, true);
    }

    hw.display.Update();
}

void UpdateDisplay() {
    if (tunerMode) { DrawTunerScreen(); return; }
    if (InSettingsMode()) { DrawSettingsScreen(); return; }

    hw.display.Fill(false);

    char line[32];

    // Main area: the knob overlay wins over the model/variant screen for
    // KNOB_DISPLAY_TIMEOUT_MS after the last knob movement, then yields back.
    const uint32_t now = daisy::System::GetNow();
    const bool knobActive = lastActiveKnob != ActiveKnob::None
                          && (now - lastKnobActivityMs) < KNOB_DISPLAY_TIMEOUT_MS;

    if (knobActive) {
        char name[12], value[12];
        GetKnobDisplayText(lastActiveKnob, name, sizeof name, value, sizeof value);
        hw.display.SetCursor(0, 0);
        hw.display.WriteString(name, Font_11x18, true);
        hw.display.SetCursor(0, 20);
        hw.display.WriteString(value, Font_11x18, true);
    } else {
        const int shownIndex = isPreviewingModel ? previewModelIndex : currentSettings->modelIndex;
        const bool haveModels = (MODEL_COUNT > 0) && shownIndex >= 0 && shownIndex < MODEL_COUNT;

        // Line 0: model name (with preview arrow if browsing). Confirmed
        // selections get the big font, same as the knob overlay; while
        // previewing (still scrolling the encoder, before the click that
        // confirms) it drops to the smaller font so more of the name is
        // visible -- exactly when the player is comparing similar-looking
        // names. Either way, names longer than the font's width are clipped
        // with a trailing '~' rather than silently cut off by the display
        // driver's own per-character bounds check -- a clipped name should
        // read as clipped, not as a different, shorter name.
        const FontDef& font = isPreviewingModel ? Font_7x10 : Font_11x18;
        const size_t maxChars = isPreviewingModel ? kSmallFontChars : kBigFontChars;
        const int line1Y = isPreviewingModel ? 14 : 20;

        char clipped[kSmallFontChars + 1];
        hw.display.SetCursor(0, 0);
        if (haveModels) {
            const char* prefix = isPreviewingModel ? "> " : "";
            ClipForDisplay(clipped, sizeof clipped, model_entries[shownIndex].model_name,
                           maxChars - strlen(prefix));
            snprintf(line, sizeof line, "%s%s", prefix, clipped);
        } else {
            snprintf(line, sizeof line, "No models");
        }
        hw.display.WriteString(line, font, true);

        // Line 1: variant name. The model type ([NAM]/[IR]) is left off --
        // that's what the IR/NAM status-bar indicator is for.
        hw.display.SetCursor(0, line1Y);
        ClipForDisplay(line, sizeof line,
                       haveModels ? model_entries[shownIndex].variant_name : "regenerate",
                       maxChars);
        hw.display.WriteString(line, font, true);
    }

    // Status bar: one line at the bottom, always visible outside settings
    // mode. Priority: safety states first, then the transient preview hint,
    // then the steady-state CPU/effect readout (indicators left, CPU meter
    // right-aligned).
    if (cpuOverloadTripped) {
        // The numbers matter: "97/104" is a chain that needs trimming,
        // "180/240" is one that needs rethinking.
        snprintf(line, sizeof line, "OVL%3d/%3d%% FS1 retry",
                 LoadPercent(overloadAvgAtTrip), LoadPercent(overloadMaxAtTrip));
        hw.display.SetCursor(0, 54);
        hw.display.WriteString(line, Font_6x8, true);
    } else if (dspFaultLatched) {
        hw.display.SetCursor(0, 54);
        hw.display.WriteString("FAULT: model off", Font_6x8, true);
    } else if (isPreviewingModel) {
        hw.display.SetCursor(0, 54);
        hw.display.WriteString("Click to load", Font_6x8, true);
    } else {
        const bool haveModels = (MODEL_COUNT > 0) && currentSettings->modelIndex >= 0
                              && currentSettings->modelIndex < MODEL_COUNT;
        const bool modelActive = currentSettings->namEnabled && haveModels;
        const bool irActive = modelActive
                            && model_entries[currentSettings->modelIndex].type == ModelType::IrOnly;
        const bool namActive = modelActive
                             && model_entries[currentSettings->modelIndex].type == ModelType::NamOnly;

        int off = 0;
        line[0] = '\0';
        off += snprintf(line + off, sizeof(line) - off, "NTCH %s ",
                        callbackNoiseFilterEnabled ? "ON" : "OFF");
        if (off > 0) line[off - 1] = '\0';  // drop the trailing separator space
        hw.display.SetCursor(0, 54);
        hw.display.WriteString(line, Font_6x8, true);

        char cpuStr[16];
        snprintf(cpuStr, sizeof cpuStr, "CPU%3d%%", LoadPercent(loadMeter.GetAvgCpuLoad()));
        WriteStringRightAligned(cpuStr, Font_6x8, 54);
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
    ShowMessage("Loading...", entry.variant_name);

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
    
    if (entry.type == ModelType::NamOnly) {
        ok &= namProcessor.loadModel(entry);
    } else if (entry.type == ModelType::IrOnly) {
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
// Handles the encoder's rotation and click, dispatched on the current UI
// mode. A long-press toggles settings mode; the matching release is latched
// out so it is not also read as a click.
//
// Model loading now commits on button release rather than press: a
// press-triggered click would fire before the long-press threshold could be
// reached. Imperceptible in use.
void HandleEncoder() {
    if (hw.encoders[0].Pressed() && !encoderLongPressFired
        && hw.encoders[0].TimeHeldMs() >= ENCODER_LONG_PRESS_MS) {
        encoderLongPressFired = true;
        if (InSettingsMode()) ExitSettingsMode(); else EnterSettingsMode();
    }

    bool click = false;
    if (hw.encoders[0].FallingEdge()) {
        click = !encoderLongPressFired;
        encoderLongPressFired = false;
    }

    const int32_t inc = hw.encoders[0].Increment();

    switch (uiMode) {
        case UiMode::Normal: {
            if (MODEL_COUNT > 0 && inc != 0) {
                int newIndex = previewModelIndex + inc;
                if (newIndex < 0) newIndex = MODEL_COUNT - 1;
                if (newIndex >= MODEL_COUNT) newIndex = 0;

                previewModelIndex = newIndex;
                isPreviewingModel = true;
                previewStartTime = daisy::System::GetNow();
            }
            if (click && isPreviewingModel) {
                // Snapshot the target before we clear the flag so that if the
                // timeout races with this callback we still load what the
                // user saw. Loading reads QSPI and paints the display, so
                // hand it to the main loop rather than doing it here.
                pendingLoadIndex = previewModelIndex;
                isPreviewingModel = false;
            } else if (click) {
                callbackNoiseFilterEnabled = !callbackNoiseFilterEnabled;
                callbackNoiseFilter.Reset();
            }
            break;
        }
        case UiMode::SettingsMenu: {
            if (inc != 0) {
                // Clamp rather than wrap: scrolling past "Back" must not
                // land the cursor back on "Reset".
                int newCursor = settingsCursor + inc;
                if (newCursor < 0) newCursor = 0;
                if (newCursor >= kSettingsMenuCount) newCursor = kSettingsMenuCount - 1;
                settingsCursor = newCursor;
            }
            if (click) {
                const SettingsMenuItem& item = kSettingsMenu[settingsCursor];
                if (item.options == nullptr) {
                    ExitSettingsMode();  // Back
                } else {
                    settingsEditValue = GetMenuItemValue(settingsCursor);
                    uiMode = UiMode::SettingsEdit;
                }
            }
            break;
        }
        case UiMode::SettingsEdit: {
            const SettingsMenuItem& item = kSettingsMenu[settingsCursor];
            if (inc != 0) {
                int newValue = settingsEditValue + inc;
                if (newValue < 0) newValue = 0;
                if (newValue >= item.optionCount) newValue = item.optionCount - 1;
                settingsEditValue = newValue;
            }
            if (click) {
                CommitMenuItemValue(settingsCursor, settingsEditValue);
                uiMode = UiMode::SettingsMenu;
            }
            break;
        }
    }
}

void HandleFootswitches() {
    // switches[0] (FS2 on the enclosure): Reverb on/off, or the tuner on a
    // 2-second hold. FS2 keeps its instant toggle-on-press feel; a hold that
    // completes undoes that toggle before opening the tuner, and a press
    // while the tuner is open closes it without touching reverb at all.
    if (hw.switches[0].RisingEdge()) {
        if (tunerMode) {
            tunerMode = false;
            fs2LongPressFired = true;  // suppress immediate re-entry this hold
        } else {
            reverbBeforeFs2Press = currentSettings->reverbEnabled;
            currentSettings->reverbEnabled = !currentSettings->reverbEnabled;
            if (currentSettings->reverbEnabled) {
                reverbTailBlocks = 0;
            } else {
                // Let the tail ring out, then stop paying for the reverb.
                reverbTailBlocks = reverbTailBlocksFull;
            }
            SaveSettingsDebounced();
        }
    }

    if (!tunerMode && !fs2LongPressFired && hw.switches[0].Pressed() &&
        hw.switches[0].TimeHeldMs() >= FS2_TUNER_HOLD_MS) {
        fs2LongPressFired = true;

        // Undo the instant toggle the press fired on its way in.
        currentSettings->reverbEnabled = reverbBeforeFs2Press;
        reverbTailBlocks = currentSettings->reverbEnabled ? 0 : reverbTailBlocksFull;
        SaveSettingsDebounced();

        tuner.reset();
        tunerMode = true;
    }

    if (hw.switches[0].FallingEdge()) fs2LongPressFired = false;

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

// Marks which knob just moved so the display can show its name/value; see
// ActiveKnob and UpdateDisplay().
static inline void NoteKnobActivity(ActiveKnob knob) {
    lastActiveKnob = knob;
    lastKnobActivityMs = daisy::System::GetNow();
}

void HandleKnobs() {
    if (UpdateKnob(hw.knobs[0].Value(), currentInputGainKnob)) {
        inputGain.SetGain(KnobToNormalized(currentInputGainKnob));
        NoteKnobActivity(ActiveKnob::InputGain);
    }
    if (UpdateKnob(hw.knobs[1].Value(), currentOutputVolumeKnob)) {
        outputVolume.SetGain(KnobToNormalized(currentOutputVolumeKnob));
        NoteKnobActivity(ActiveKnob::OutputVolume);
    }
    if (UpdateKnob(hw.knobs[2].Value(), currentReverbMixKnob)) {
        // Both engines track the same knob so switching engines in the
        // Settings menu doesn't need to re-apply Mix.
        reverbProcessor.setMix(currentReverbMixKnob);
        simpleReverbProcessor.setMix(currentReverbMixKnob);
        NoteKnobActivity(ActiveKnob::ReverbMix);
    }
    if (UpdateKnob(hw.knobs[3].Value(), currentBassKnob)) {
        eq.SetBass(KnobToNormalized(currentBassKnob));
        NoteKnobActivity(ActiveKnob::Bass);
    }
    if (UpdateKnob(hw.knobs[4].Value(), currentMidKnob)) {
        eq.SetMid(KnobToNormalized(currentMidKnob));
        NoteKnobActivity(ActiveKnob::Mid);
    }
    if (UpdateKnob(hw.knobs[5].Value(), currentTrebleKnob)) {
        eq.SetTreble(KnobToNormalized(currentTrebleKnob));
        NoteKnobActivity(ActiveKnob::Treble);
    }
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
    // Tuner mode feeds every sample to the detector and outputs silence,
    // skipping NAM/EQ/reverb entirely so the tuner never competes with the
    // amp chain for CPU. Checked before the audioSuppressed early-out below
    // since that's the settings-mode-derived suppression, not this one.
    if (tunerMode) {
        for (size_t i = 0; i < size; ++i) {
            tuner.process(in[0][i]);
            out[0][i] = 0.0f;
            out[1][i] = 0.0f;
        }
        return;
    }

    // Fast-path silence when audio is suppressed (mute window / model load /
    // settings mode).
    if (audioSuppressed || InSettingsMode()) {
        for (size_t i = 0; i < size; ++i) { out[0][i] = 0.0f; out[1][i] = 0.0f; }
        return;
    }

    // Bypass path (both effects off). In the default (relay) mode the relay
    // routes the analog signal around the DSP and the codec path stays dual
    // mono. In Direct and Mono-to-Stereo the relay never engages, so this is
    // the actual signal path: Direct copies each input channel through
    // separately (the input is physically mono, so the right channel is
    // silence -- deliberate per spec); Mono-to-Stereo copies the left input
    // to both outputs, same as the relay mode's codec path.
    if (!currentSettings->namEnabled && !currentSettings->reverbEnabled) {
        if (currentSettings->bufferedBypassMode == BypassDirect) {
            for (size_t i = 0; i < size; ++i) {
                out[0][i] = in[0][i];
                out[1][i] = in[1][i];
            }
        } else {
            for (size_t i = 0; i < size; ++i) {
                const float x = in[0][i];
                out[0][i] = x;
                out[1][i] = x;
            }
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
    if (callbackNoiseFilterEnabled) callbackNoiseFilter.ProcessBlock(scratchDry, size);

    // 2) Selected model engine: NAM A2 Lite and/or cabinet IR.
    if (currentSettings->namEnabled && IsValidModelIndex(currentSettings->modelIndex)) {
        const ModelEntry& entry = model_entries[currentSettings->modelIndex];

        // One engine or the other, never both -- running a NAM into a cabinet
        // IR costs both their budgets in the same block, which does not fit
        // alongside the reverb. So this is a single pass with no intermediate
        // buffer rather than the NAM-then-IR chain it used to be.
        if (entry.type == ModelType::NamOnly && namProcessor.isModelLoaded()) {
            namProcessor.process(scratchDry, scratchWet, size);
        } else if (entry.type == ModelType::IrOnly && irProcessor.isLoaded()) {
            irProcessor.processBlock(scratchDry, scratchWet, size);
        } else {
            for (size_t i = 0; i < size; ++i) scratchWet[i] = scratchDry[i];
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
        // Hoisted out of the per-sample loop below -- one branch per block,
        // not one per sample.
        const bool useSimpleReverb = currentSettings->reverbEngine == ReverbEngineSimple;
        for (size_t i = 0; i < size; ++i) {
            float l, r;
            // If reverb is disabled, we feed silence (0.0f) to the reverb input to let it decay,
            // while mixing the dry signal as normal.
            const float reverbInput = currentSettings->reverbEnabled ? scratchWet[i] : 0.0f;
            if (useSimpleReverb) {
                simpleReverbProcessor.process(reverbInput, scratchWet[i], &l, &r);
            } else {
                reverbProcessor.process(reverbInput, scratchWet[i], &l, &r);
            }
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

// Sum both output channels to the left only, at half gain so a centered
// signal keeps its original level instead of gaining up to 6 dB. Applied
// uniformly after ProcessAudioDsp rather than duplicated at each of its
// return points.
static inline void ApplyMonoOutput(daisy::AudioHandle::OutputBuffer out, size_t size) {
    if (!currentSettings->monoOutput) return;
    for (size_t i = 0; i < size; ++i) {
        out[0][i] = 0.5f * (out[0][i] + out[1][i]);
        out[1][i] = 0.0f;
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
    // Footswitches are ignored in settings mode: toggling an effect the menu
    // screen isn't showing would be silently confusing.
    if (!InSettingsMode()) HandleFootswitches();
    HandleEncoder();

    // -- True-bypass relay and analog mute --
    // Re-evaluated every block rather than only on footswitch edges, so the
    // relay also tracks the overload and DSP-fault paths that turn the model
    // engine off from underneath the UI.
    UpdateBypassRelay();
    StepBypassTiming((int32_t)size);
    hw.SetAudioBypass(relayBypassOn);
    hw.SetAudioMute(relayMuteOn || InSettingsMode() || tunerMode);

    ProcessAudioDsp(in, out, size);
    ApplyMonoOutput(out, size);

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
    simpleReverbProcessor.clear();
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

// The settings menu's Reset->Confirm action. Reboots into the bootloader,
// same as the physical reset button. Runs on the main loop, not the audio
// ISR: it blocks, writes a GPIO and never returns.
void HandlePendingReset() {
    if (!pendingReset) return;
    pendingReset = false;

    // Flush now: the debounced save is up to SAVE_DELAY_MS away and the
    // reboot would beat it.
    if (settingsDirty) {
        settings.Save();
        settingsDirty = false;
    }

    ShowMessage("Resetting...");

    // DAISY, not STM: this firmware is APP_TYPE = BOOT_SRAM, so a plain
    // reboot lands in the Daisy bootloader and its normal DFU timeout
    // window -- what the physical reset button does. STM mode would instead
    // park in the ROM bootloader until reflashed over DFU.
    daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY);
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

    // The Simple engine's delay lines are embedded directly in
    // simpleReverbProcessor's own SDRAM placement, not carved from the
    // Dattorro arena above, so it inits unconditionally.
    simpleReverbProcessor.init(hw.AudioSampleRate());
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
    callbackNoiseFilter.Init(hw.AudioSampleRate());
    irProcessor.init(g_ir_freq_buf, g_ir_fdl_buf);
    loadMeter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());

    // Tuner: allocates its Cycfi Q detector state from the heap (~40KB, once).
    // Must happen here at boot, never lazily from the audio callback.
    tuner.init(hw.AudioSampleRate());
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
    simpleReverbProcessor.setMix(currentReverbMixKnob);
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
        HandlePendingReset();
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
