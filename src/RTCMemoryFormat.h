#pragma once

#include <stdint.h>
#include <stddef.h>

namespace RTCMemoryDetail {

// Shared by buffered and streaming APIs; do not change these wire constants
// without changing the persisted format version.
struct Format {
    static constexpr size_t WordBytes = 4u;
    static constexpr size_t TotalBytes = 512u;
    static constexpr size_t ReservedBytes = 128u;
    static constexpr size_t AvailableBytes = TotalBytes - ReservedBytes;
    static constexpr size_t HeaderBytes = 12u;
    static constexpr size_t MaxPayloadBytes = AvailableBytes - HeaderBytes;
    static constexpr uint32_t Magic = 0x4D435452u;
    static constexpr uint32_t Version = 1u;
    static constexpr size_t alignedSize(size_t size) { return (size + 3u) & ~size_t{3u}; }
    static bool aligned(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 3u) == 0u; }
    static bool range(size_t offset, size_t bytes, size_t limit) {
        return offset <= limit && bytes <= limit - offset;
    }
    static uint32_t get32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
            | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    static void put32(uint8_t* p, uint32_t value) {
        for (uint8_t i = 0u; i < 4u; ++i) p[i] = static_cast<uint8_t>(value >> (i * 8u));
    }
    // CRC32 MSB first, seed 0xFFFFFFFF, no final XOR. Call incrementally.
    static uint32_t crc(uint32_t value, const uint8_t* data, size_t bytes) {
        while (bytes-- != 0u) {
            value ^= static_cast<uint32_t>(*data++) << 24;
            for (uint8_t bit = 0u; bit < 8u; ++bit)
                value = (value & 0x80000000u) ? (value << 1) ^ 0x04C11DB7u : value << 1;
        }
        return value;
    }
};

} // namespace RTCMemoryDetail
