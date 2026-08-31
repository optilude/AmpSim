#pragma once

#include <cstdint>

// Knob dead band (prevents jitter)
constexpr float KNOB_DEADBAND = 0.01f;

// EQ threshold for processing
constexpr float EQ_PROCESS_THRESHOLD = 0.05f;

// Timing constants
constexpr uint32_t PREVIEW_TIMEOUT_MS = 10000;
constexpr uint32_t SAVE_DELAY_MS = 2000;
// True-bypass relay sequencing, counted in samples from the audio callback
// rather than slept through on the main loop. On an effect toggle the analog
// mute engages immediately, the relay flips once the output is quiet, and the
// mute releases after the contacts have settled. Same shape and timing as
// bkshepherd's.
constexpr float BYPASS_TOGGLE_TRANSITION_S = 0.010f;  // relay flips at 10 ms
constexpr float MUTE_OFF_TRANSITION_S = 0.020f;       // unmute at 20 ms
constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 33;  // ~30 FPS
constexpr uint32_t ERROR_DISPLAY_TIME_MS = 5000;  // 5 seconds

// Knob ranges
constexpr float GAIN_RANGE_DB = 20.0f;   // ±20dB for input/output
constexpr float EQ_RANGE_DB = 12.0f;     // ±12dB for EQ bands

// How long the reverb keeps running after being switched off so the tail
// decays instead of being cut dead. Converted to blocks at init: hardcoding a
// block count silently changes the tail length whenever the block size does.
constexpr float REVERB_TAIL_SECONDS = 3.0f;

// Smoothed audio-callback load above which the model engine is shed. Beyond
// this the callback starves the main loop and the pedal stops responding.
constexpr float CPU_OVERLOAD_THRESHOLD = 0.95f;

// EQ frequencies (Hz)
constexpr float BASS_FREQ = 100.0f;
constexpr float MID_FREQ = 1000.0f;
constexpr float TREBLE_FREQ = 4000.0f;

// EQ Q factors. Shelves use 0.707 (Butterworth-like slope). The mid is
// a peaking filter with a moderate Q for musical width.
constexpr float BASS_Q = 0.707f;
constexpr float MID_Q = 0.9f;
constexpr float TREBLE_Q = 0.707f;
