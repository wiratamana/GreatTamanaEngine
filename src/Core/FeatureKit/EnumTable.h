#pragma once

// Name tables for enums: value to text and text to value.

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace gte::fx {

template <class E>
struct EnumEntry {
    E value;
    std::string_view name;
};

// Name of `value`; the first entry's name when `value` is not in the table.
template <class E, std::size_t N>
constexpr std::string_view EnumName(const std::array<EnumEntry<E>, N>& table, E value) noexcept
{
    for (const EnumEntry<E>& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return table[0].name;
}

template <class E, std::size_t N>
constexpr std::optional<E> ParseEnum(const std::array<EnumEntry<E>, N>& table, std::string_view text) noexcept
{
    for (const EnumEntry<E>& entry : table) {
        if (entry.name == text) {
            return entry.value;
        }
    }
    return std::nullopt;
}

template <class E, std::size_t N>
constexpr bool EnumKnown(const std::array<EnumEntry<E>, N>& table, E value) noexcept
{
    for (const EnumEntry<E>& entry : table) {
        if (entry.value == value) {
            return true;
        }
    }
    return false;
}

} // namespace gte::fx
