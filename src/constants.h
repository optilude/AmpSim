#pragma once

#include <cstdint>

// Knob dead band (prevents jitter)
constexpr float KNOB_DEADBAND = 0.01f;

// EQ threshold for processing
constexpr float EQ_PROCESS_THRESHOLD = 0.05f;

// Timing constants
constexpr uint32_t PREVIEW_TIMEOUT_MS = 10000;
constexpr uint32_t SAVE_DELAY_MS = 2000;
constexpr uint32_t MUTE_DELAY_MS = 30;  // 30ms for better pop prevention
constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 33;  // ~30 FPS
constexpr uint32_t ERROR_DISPLAY_TIME_MS = 5000;  // 5 seconds

// Knob ranges
constexpr float GAIN_RANGE_DB = 20.0f;   // ±20dB for input/output
constexpr float EQ_RANGE_DB = 12.0f;     // ±12dB for EQ bands

// Blocks the reverb keeps running after being switched off (~3 s at 48 kHz
// with a 128 sample block) so the tail decays instead of being cut dead.
constexpr int32_t REVERB_TAIL_BLOCKS = 1125;

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
