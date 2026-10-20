#pragma once

#include "../Core/PixelFormat.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gte {

// Which material slot an image feeds. Only the color slot is sRGB; the others are linear data.
enum class MaterialImageRole : std::uint8_t { BaseColor, Normal, MetallicRoughness, Occlusion };

inline constexpr std::size_t kMaterialImageRoleCount = 4;
static_assert(kMaterialImageRoleCount == static_cast<std::size_t>(MaterialImageRole::Occlusion) + 1);

constexpr PixelFormat MaterialImageFormat(MaterialImageRole role) noexcept
{
    return role == MaterialImageRole::BaseColor ? PixelFormat::RGBA8_SRGB : PixelFormat::RGBA8_UNorm;
}

// Display name of a slot for debug tools.
constexpr const char* MaterialImageRoleLabel(MaterialImageRole role) noexcept
{
    switch (role) {
    case MaterialImageRole::BaseColor:
        return "Base Color";
    case MaterialImageRole::Normal:
        return "Normal";
    case MaterialImageRole::MetallicRoughness:
        return "Metallic-Roughness";
    case MaterialImageRole::Occlusion:
        return "Occlusion";
    }
    return "Unknown";
}

// One image to upload. `pixelsRgba8` must stay valid until the upload call returns; the name is copied.
struct MaterialImageUpload {
    const void* pixelsRgba8 = nullptr;
    int width = 0;
    int height = 0;
    MaterialImageRole role = MaterialImageRole::BaseColor;
    std::string_view debugName;
};

} // namespace gte
