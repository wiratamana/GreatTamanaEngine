#pragma once

#include "../../Core/FeatureKit/Texture.h"
#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gte {

// Owner and channel of the cloud shadow map the cloud layer publishes for other features.
inline constexpr const char* kCloudShadowOwner = "VolumetricCloud";
inline constexpr const char* kCloudShadowChannel = "ShadowMap";

// Texels per side of the map.
inline constexpr std::uint32_t kCloudShadowMapSize = 128;

// Sun light that the cloud layer lets through, as a square map over the layer. A receiver at world point P reads the
// map where its sun ray crosses the layer: xz = P.xz + sunDirection.xz / sunDirection.y * (layerMidM - P.y).
// `map` is valid only in the frame and view that published it; r = sun transmittance (1 = clear), g = optical depth.
struct CloudShadowMap {
    fx::Texture map;
    Vec3 sunDirection = Vec3::Up(); // Unit vector toward the sun.
    float centerX = 0.0f;           // World x of the map centre (m).
    float centerZ = 0.0f;           // World z of the map centre (m).
    float extentM = 1.0f;           // Side length of the map (m).
    float layerBaseM = 0.0f;
    float layerTopM = 1.0f;
    float strength = 0.0f;          // 0..1: how much of the sun a fully shadowed receiver loses.
    float shaftStrength = 0.0f;     // 0..1: how much of the sun in-scatter of the air the clouds block (light shafts).
    float shaftSharpness = 1.0f;    // >= 1: exponent on the air's sun transmittance; higher makes the beams crisper.
    float shaftHazePerM = 0.0f;     // Extinction per m of the haze near the ground that carries the beams (0 = none).
    float shaftHeightM = 1500.0f;   // Scale height of that haze (m).
    float beamHazePerKm = 0.0f;     // Extinction per km of the haze the air volume lights through the map (0 = none).
    float beamFill = 0.3f;          // Sky light in the shadowed columns of that haze, relative to the multi-scattering table.
    float skyFill = 0.0f;           // 0..1: share of the blocked sun that reaches shadowed receivers as diffuse light.
};

// A map a consumer may sample: valid texture, finite numbers, a sun above the horizon.
inline bool CloudShadowMapUsable(const CloudShadowMap& m) noexcept
{
    return m.map.IsValid() && std::isfinite(m.centerX) && std::isfinite(m.centerZ) && m.extentM > 0.0f
        && m.layerTopM > m.layerBaseM && (m.strength > 0.0f || m.shaftStrength > 0.0f) && std::isfinite(m.sunDirection.y) && m.sunDirection.y > 0.0f;
}

// Map coordinate (0..1 inside) of the point where the sun ray of `world` crosses the middle of the layer.
inline Vec2 CloudShadowUv(const CloudShadowMap& m, const Vec3& world) noexcept
{
    const float mid = 0.5f * (m.layerBaseM + m.layerTopM);
    const float along = (mid - world.y) / std::max(m.sunDirection.y, 1e-4f);
    const float x = world.x + m.sunDirection.x * along;
    const float z = world.z + m.sunDirection.z * along;
    return Vec2((x - m.centerX) / m.extentM + 0.5f, (z - m.centerZ) / m.extentM + 0.5f);
}

} // namespace gte
