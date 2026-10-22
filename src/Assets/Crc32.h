#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace gte {

// CRC-32/ISO-HDLC (reflected, poly 0xEDB88320). Pass the previous result as `seed` to continue a stream.
constexpr std::uint32_t Crc32(std::span<const std::byte> data, std::uint32_t seed = 0) noexcept
{
    std::uint32_t crc = ~seed;
    for (const std::byte b : data) {
        crc ^= static_cast<std::uint32_t>(b);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

} // namespace gte
