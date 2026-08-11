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
//   The current Flick/MuleBox-style plate preset fits in this 1 MiB arena.
//   Desktop tests assert that construction consumes the arena without falling
//   back to heap storage.
//
// Ownership:
//   The arena is a single .sdram_bss array. main() installs the arena
//   via InterpDelayArena::set() BEFORE constructing the reverb, so that
//   every InterpDelay carved during Dattorro construction lands here.

#include <cstddef>

constexpr size_t kReverbArenaFloats = 262144;  // 1 MiB / sizeof(float)

// Defined in reverb_arena.cpp with the .sdram_bss attribute.
extern float g_reverb_arena[kReverbArenaFloats];
