#pragma once

// FNV-1a feeder used by the water hashes.

#include <bit>
#include <cstdint>
#include <string>

namespace gte::water {

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;

    void Bytes(std::uint64_t v) noexcept
    {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFFu;
            h *= 1099511628211ull;
        }
    }
    void Add(float v) noexcept { Bytes(std::bit_cast<std::uint32_t>(v)); }
    void Add(double v) noexcept { Bytes(std::bit_cast<std::uint64_t>(v)); }
    void Add(std::uint32_t v) noexcept { Bytes(v); }
    void Add(std::int32_t v) noexcept { Bytes(static_cast<std::uint32_t>(v)); }
    void Add(std::uint8_t v) noexcept { Bytes(v); }
    void Add(bool v) noexcept { Bytes(v ? 1u : 0u); }
    void Add(const std::string& s) noexcept
    {
        Bytes(s.size());
        for (const char c : s) {
            Bytes(static_cast<unsigned char>(c));
        }
    }
};

} // namespace gte::water
