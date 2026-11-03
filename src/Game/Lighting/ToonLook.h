#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gte {

// Owner and name of the channel that publishes the toon look values, once per view.
inline constexpr const char* kToonLookOwner = "ToonCharacter";
inline constexpr const char* kToonLookChannel = "Look";

// Priority of the publisher. Same-stage readers must use a larger number.
inline constexpr std::int32_t kToonLookPriority = 15;

// Toon shading values for one view. The four vectors are copied unchanged into the scene uniform block.
// A zero record (v0.x <= 0) means "never published"; shaders then use built-in defaults.
struct alignas(16) ToonLook {
    float v0[4] = {}; // x band threshold, y band softness, z deep threshold, w shadow strength
    float v1[4] = {}; // x rim strength, y rim width, z hair highlight, w emissive boost
    float v2[4] = {}; // x face flatten, y outline thickness (NDC y units), z outline darkness, w debug view
    float v3[4] = {}; // x shade blend, y gem heuristic (0/1), z gold glint (0/1), w viewport aspect (width / height)
};
static_assert(sizeof(ToonLook) == 64);
static_assert(alignof(ToonLook) == 16);
static_assert(offsetof(ToonLook, v1) == 16 && offsetof(ToonLook, v2) == 32 && offsetof(ToonLook, v3) == 48);
static_assert(std::is_trivially_copyable_v<ToonLook>);

} // namespace gte
