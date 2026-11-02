#pragma once

#include <cstdint>

namespace gte {

// Single layer bit of a raw PMX group value. Masked to 0..15 first so the shift is always defined.
constexpr std::uint16_t GroupBit(std::uint8_t group) noexcept
{
    return static_cast<std::uint16_t>(1u << (group & 0x0Fu));
}

// Symmetric AND test: each side's group bit must be set in the other side's mask.
inline bool GroupsMayCollide(std::uint8_t groupA, std::uint16_t maskA, std::uint8_t groupB, std::uint16_t maskB) noexcept
{
    return (GroupBit(groupA) & maskB) != 0 && (GroupBit(groupB) & maskA) != 0;
}

} // namespace gte
