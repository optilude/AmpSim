#pragma once
// Reverb delay-line arena
// -----------------------
// The Dattorro plate reverb allocates ~850 KB of delay-line memory at
// 48 kHz / maxTimeScale=4. That is far larger than the Daisy Seed SRAM
// heap (~450 KB after BSS on the QSPI boot image), so we back the delay
// lines with SDRAM instead. This header declares a single SDRAM arena
// used by InterpDelayArena.
//
// Sizing:
//   Measured budget at 48 kHz, maxTimeScale=4, maxLfoDepth=16:
//     tank delays:        ~787 KB
//     input APFs (4):     ~28  KB
//     pre-delay (200 ms): ~38  KB
//     ------------------------------
//     total:              ~853 KB
//   We reserve 1 MiB (~262144 floats) for headroom and future changes.
//
// Ownership:
//   The arena is a single .sdram_bss array. main() installs the arena
//   via InterpDelayArena::set() BEFORE constructing the reverb, so that
//   every InterpDelay carved during Dattorro construction lands here.

#include <cstddef>

constexpr size_t kReverbArenaFloats = 262144;  // 1 MiB / sizeof(float)

// Defined in reverb_arena.cpp with the .sdram_bss attribute.
extern float g_reverb_arena[kReverbArenaFloats];
