#pragma once

#include "../../Core/FeatureKit/Texture.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    bool depthFitted = false;        // Splits were fitted to a measured visible depth range.
};

// True when every cascade the receiver claims is complete and finite, so a shader can sample it.
inline bool SunShadowReceiverUsable(const SunShadowReceiver& r) noexcept
{
    constexpr float kForwardTolerance = 0.01f;
    const auto finite = [](const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    const auto finiteMat = [](const Mat4& m) {
        const float* data = m.Data();
        return std::all_of(data, data + 16, [](float f) { return std::isfinite(f); });
    };

    if (r.cascadeCount < 1 || r.cascadeCount > kSunShadowReceiverMaxCascades || r.resolution == 0) {
        return false;
    }
    if (!finite(r.cameraPosition) || !finite(r.cameraForward)
        || std::abs(Length(r.cameraForward) - 1.0f) > kForwardTolerance) {
        return false;
    }
    float previousSplit = 0.0f;
    for (std::uint32_t c = 0; c < r.cascadeCount; ++c) {
        const bool good = r.maps[c].IsValid() && finiteMat(r.viewProjection[c]) && std::isfinite(r.splitFar[c])
            && r.splitFar[c] > 0.0f && r.splitFar[c] >= previousSplit && std::isfinite(r.blendWidth[c])
            && std::isfinite(r.texelWorld[c]) && r.texelWorld[c] > 0.0f && std::isfinite(r.depthRange[c])
            && r.depthRange[c] > 0.0f;
        if (!good) {
            return false;
        }
        previousSplit = r.splitFar[c];
    }
    return true;
}

} // namespace gte
