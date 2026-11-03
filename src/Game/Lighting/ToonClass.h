#pragma once

#include <cstdint>

namespace gte {

// Toon shading class of a material. Ids are append only: the toon shaders read them from MaterialParams.
enum class ToonClass : std::uint32_t {
    Cloth = 0,
    Skin = 1,
    FaceSkin = 2,
    FaceLine = 3,
    Eye = 4,
    Hair = 5,
    HairOverlay = 6,
    Gem = 7,
    ClothBack = 8,
};

inline constexpr std::uint32_t kToonClassCount = 9;

} // namespace gte
