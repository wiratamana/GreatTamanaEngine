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

    // Sampling settings for material shaders.
    float fadeStart = 0.0f;          // View depth where the distance fade begins.
    float materialStrength = 0.0f;   // 0 = material shaders must not apply shadows.
    float filterRadiusTexels = 0.0f;
    float constBiasTexels = 0.0f;
    float normalOffsetTexels = 0.0f;
    float slopeBiasTexels = 0.0f;    // Already max(slope, filter radius).
    std::uint32_t filterSamples = 1;
};

} // namespace gte
