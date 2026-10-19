#pragma once

#include "../../Core/FeatureKit/Texture.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>

namespace gte {

// Owner and name of the channel that publishes the sun's cascade data to receivers.
inline constexpr const char* kSunShadowReceiverOwner = "Shadow";
inline constexpr const char* kSunShadowReceiverChannel = "Receiver";
inline constexpr std::uint32_t kSunShadowReceiverMaxCascades = 4;

// Everything a feature needs to test a world point against the sun's cascade maps.
// `maps` are valid only in the frame and view that published them.
struct SunShadowReceiver {
    std::uint32_t cascadeCount = 0;
    std::uint32_t resolution = 0; // Map size in texels (square).
    std::array<fx::Texture, kSunShadowReceiverMaxCascades> maps{};
    std::array<Mat4, kSunShadowReceiverMaxCascades> viewProjection{}; // World to cascade clip.
    std::array<float, kSunShadowReceiverMaxCascades> splitFar{};      // Planar view depth where cascade i ends.
    std::array<float, kSunShadowReceiverMaxCascades> blendWidth{};    // Blend band at the end of cascade i.
    std::array<float, kSunShadowReceiverMaxCascades> texelWorld{};    // World size of one texel.
    std::array<float, kSunShadowReceiverMaxCascades> depthRange{};    // Ortho depth span, world units.
    Vec3 cameraPosition;
    Vec3 cameraForward;
    Vec3 directionTowardSun = Vec3::Up();
    float shadowDistance = 0.0f;
};

} // namespace gte
