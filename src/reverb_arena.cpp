#include "reverb_arena.h"

// Place the reverb arena in external SDRAM. This is a NOLOAD BSS section
// so it doesn't inflate the firmware image; the SDRAM is not zeroed by
// startup so callers must not assume zero-initialisation (InterpDelay
// memsets its own slice on construction).
float g_reverb_arena[kReverbArenaFloats] __attribute__((section(".sdram_bss")));
