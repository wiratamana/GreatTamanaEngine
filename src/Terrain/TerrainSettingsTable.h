#pragma once

// Slider table of every TerrainSettings field: group, label, range and member pointer. The editor panel
// draws from it and tests check it against Sanitize and the name table, so a new field cannot be forgotten.

#include "TerrainSettings.h"

#include <cstdint>
#include <span>

namespace gte::terrain {

enum class FieldKind : std::uint8_t { Float, Int, Bool, Seed, Choice };

struct FieldDesc {
    const char* name;  // C++ field name (the HTTP key).
    const char* label;
    const char* group;
    FieldKind kind;
    double min;
    double max;
    bool logarithmic;
    const char* format; // printf format for sliders.
    const char* tip;
    float TerrainSettings::* floatMember;
    std::int32_t TerrainSettings::* intMember;
    bool TerrainSettings::* boolMember;
    std::uint32_t TerrainSettings::* seedMember;
};

// Allowed values of the Choice field `chunkQuads`.
std::span<const std::int32_t> ChunkQuadChoices() noexcept;

// Group names in display order.
std::span<const char* const> SettingGroups() noexcept;

// Every field once, grouped in display order.
std::span<const FieldDesc> SettingFields() noexcept;

// Current value as a double (bool is 0 or 1), and the inverse. Set does not sanitize.
double GetFieldValue(const TerrainSettings& settings, const FieldDesc& field) noexcept;
void SetFieldValue(TerrainSettings& settings, const FieldDesc& field, double value) noexcept;

} // namespace gte::terrain
