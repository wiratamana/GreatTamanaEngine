#pragma once

#include <cstdint>

namespace gte {

// Engine-native image shape - zero Vulkan dependency.
enum class TextureDimension : std::uint8_t {
    Tex2D,
    Tex3D,
    Array2D,
    Cube, // 6 layers -> cube view, otherwise cube-array view.
};

} // namespace gte
