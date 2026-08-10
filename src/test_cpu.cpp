// CPU load test - include in main.cpp temporarily
// Add to AudioCallback to measure CPU usage

#ifdef CPU_MEASUREMENT
#include "daisy_core.h"

// Add these globals
uint32_t cpu_cycles = 0;
uint32_t max_cycles = 0;
uint32_t avg_cycles = 0;
uint32_t cycle_count = 0;

// In AudioCallback, wrap the processing:
void AudioCallback(...) {
    uint32_t start = DWT->CYCCNT;
    
    // ... your audio processing here ...
    
    uint32_t end = DWT->CYCCNT;
    cpu_cycles = end - start;
    if (cpu_cycles > max_cycles) max_cycles = cpu_cycles;
    avg_cycles = (avg_cycles * 99 + cpu_cycles) / 100;
    cycle_count++;
    
    // Expected at 48kHz: 400,000,000 / 48,000 = 8,333 cycles available
    // If you use > 7,500 cycles, you're at risk of dropout
}

// In main(), enable cycle counter:
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

// Display CPU usage on OLED:
sprintf(line, "CPU: %lu/%lu cyc", avg_cycles, 8333);
#endif
