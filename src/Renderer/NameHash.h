#pragma once

// Stable 32-bit name hash (FNV-1a) used to identify passes and owners in GPU records without storing strings.

#include <cstdint>
#include <string_view>

namespace gte {

constexpr std::uint32_t NameHash32(std::string_view text) noexcept
{
    std::uint32_t hash = 2166136261u;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

} // namespace gte
