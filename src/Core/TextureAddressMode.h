#pragma once

#include <cstdint>

namespace gte {

enum class TextureAddressMode : std::uint8_t {
    ClampToEdge,
    Repeat,
};

} // namespace gte
