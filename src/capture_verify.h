#pragma once

// CRC verification for model data read out of memory-mapped QSPI.
//
// The capture blob lives at a fixed QSPI address baked into capture_index.h by
// the build tools. If the flashed image and the header ever disagree about that
// address the firmware reads plausible-looking garbage: NAM weights that make
// the model diverge, IRs that are silent, or unwritten flash (0xFF == NaN) that
// poisons the whole signal chain. Checking the CRC at load time turns that from
// a mystery into a message on the display.

#include <cstddef>
#include <cstdint>

#include "capture_index.h"

namespace capture {

// zlib/IEEE CRC-32 (reflected, init 0xFFFFFFFF, final xor 0xFFFFFFFF), nibble
// table so the lookup costs 64 bytes of flash rather than 1 KiB.
inline uint32_t Crc32(uint32_t crc, const uint8_t* data, size_t len) {
    static const uint32_t kNibble[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        crc = (crc >> 4) ^ kNibble[crc & 0x0f];
        crc = (crc >> 4) ^ kNibble[crc & 0x0f];
    }
    return crc;
}

// Recompute the CRC the build tools recorded: NAM weight bytes followed by IR
// sample bytes, whichever the entry has.
inline uint32_t ComputeEntryCrc(const ModelEntry& entry) {
    uint32_t crc = 0xffffffffu;
    if (entry.nam_qspi_address && entry.nam_byte_count) {
        crc = Crc32(crc, reinterpret_cast<const uint8_t*>(entry.nam_qspi_address),
                    entry.nam_byte_count);
    }
    if (entry.ir_qspi_address && entry.ir_byte_count) {
        crc = Crc32(crc, reinterpret_cast<const uint8_t*>(entry.ir_qspi_address),
                    entry.ir_byte_count);
    }
    return crc ^ 0xffffffffu;
}

inline bool VerifyEntry(const ModelEntry& entry) {
    return ComputeEntryCrc(entry) == entry.crc32;
}

}  // namespace capture
