#pragma once

#include <cstdint>

// Knob dead band (prevents jitter)
constexpr float KNOB_DEADBAND = 0.01f;

// EQ threshold for processing
constexpr float EQ_PROCESS_THRESHOLD = 0.05f;

// Timing constants
constexpr uint32_t PREVIEW_TIMEOUT_MS = 10000;
// Coalesces rapid successive setting changes into one flash write. Short on
// purpose: a footswitch toggle right before the user pulls the power cable
// needs to have already landed in flash, and a couple hundred ms is still
// far longer than any real sequence of button presses.
constexpr uint32_t SAVE_DELAY_MS = 250;
// True-bypass relay sequencing, counted in samples from the audio callback
// rather than slept through on the main loop. On an effect toggle the analog
// mute engages immediately, the relay flips once the output is quiet, and the
// mute releases after the contacts have settled. Same shape and timing as
// bkshepherd's.
constexpr float BYPASS_TOGGLE_TRANSITION_S = 0.010f;  // relay flips at 10 ms
constexpr float MUTE_OFF_TRANSITION_S = 0.020f;       // unmute at 20 ms
constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 33;  // ~30 FPS
constexpr uint32_t ERROR_DISPLAY_TIME_MS = 5000;  // 5 seconds
constexpr uint32_t ENCODER_LONG_PRESS_MS = 700;  // hold to enter/exit settings
// How long the knob-value overlay stays up after the last knob movement
// before the display reverts to the model/variant screen.
constexpr uint32_t KNOB_DISPLAY_TIMEOUT_MS = 1000;

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

// Turning the model engine on makes the first block the most expensive one
// there will ever be: it runs the engine resets and the first inference with a
// cold I-cache. CpuLoadMeter::Reset() arms firstCycle_, so that one block is
// assigned straight to the average with no smoothing -- which tripped the
// overload detector on every single FS1 press, including the retry.
//
// So the load is left to settle before it is believed. Blocks, not
// milliseconds, because the callback is the only thing counting: at the 48
// sample block these are 1 ms each.
//
// WARMUP < GRACE, so the meter is re-zeroed partway through the grace period
// and the reading that can trip is built entirely from warm blocks.
constexpr int32_t CPU_LOAD_WARMUP_BLOCKS = 100;      // then re-zero the meter
constexpr int32_t CPU_OVERLOAD_GRACE_BLOCKS = 400;   // 300 ms of clean data
// The average is a 1 Hz one-pole (~160 ms), so it does not spike; requiring it
// to stay over the line is cheap insurance against a burst that does get
// through -- one partitioned-convolution block, say.
constexpr int32_t CPU_OVERLOAD_TRIP_BLOCKS = 50;

// EQ frequencies (Hz)
constexpr float BASS_FREQ = 100.0f;
constexpr float MID_FREQ = 1000.0f;
constexpr float TREBLE_FREQ = 4000.0f;

// EQ Q factors. Shelves use 0.707 (Butterworth-like slope). The mid is
// a peaking filter with a moderate Q for musical width.
constexpr float BASS_Q = 0.707f;
constexpr float MID_Q = 0.9f;
constexpr float TREBLE_Q = 0.707f;
