#pragma once

#include "../../Core/FeatureKit/Texture.h"
#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace gte {

// Owner and channel of the cloud shadow map the cloud layer publishes for other features.
inline constexpr const char* kCloudShadowOwner = "VolumetricCloud";
inline constexpr const char* kCloudShadowChannel = "ShadowMap";

// Texels per side of one region, nested near-map scale and stacked texture height (far rows then near rows).
inline constexpr std::uint32_t kCloudShadowMapSize = 128;
inline constexpr std::uint32_t kCloudShadowNearScale = 8;
inline constexpr std::uint32_t kCloudShadowMapRows = 2 * kCloudShadowMapSize;
inline constexpr float kCloudShadowFarEdgeFade = 0.1f;
inline constexpr float kCloudShadowNearEdgeFade = 0.15f;

// Sun light that the cloud layer lets through, stacked as 128x128 far (rows 0..127) and 128x128 near (rows 128..255).
// `map` is valid only in the publishing frame and view; r = sun transmittance (1 = clear), g = optical depth.
struct CloudShadowMap {
    fx::Texture map;
    Vec3 sunDirection = Vec3::Up(); // Unit vector toward the sun.
    float centerX = 0.0f;           // World x of the shared far/near map centre (m).
    float centerZ = 0.0f;           // World z of the shared far/near map centre (m).
    float extentM = 1.0f;           // Side length of the far map (m).
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
    float nearScale = static_cast<float>(kCloudShadowNearScale); // Far-to-near extent ratio (<= 1 disables near region).
    std::uint32_t mapRows = kCloudShadowMapRows;                 // Total texture rows (2 * size when near region is present).
};

// A map a consumer may sample: valid texture, finite numbers, a sun above the horizon.
inline bool CloudShadowMapUsable(const CloudShadowMap& m) noexcept
{
    return m.map.IsValid() && std::isfinite(m.centerX) && std::isfinite(m.centerZ) && m.extentM > 0.0f
        && m.layerTopM > m.layerBaseM && (m.strength > 0.0f || m.shaftStrength > 0.0f) && std::isfinite(m.sunDirection.y) && m.sunDirection.y > 0.0f;
}

inline constexpr float CloudShadowNearExtentM(
    float farExtentM, float nearScale = static_cast<float>(kCloudShadowNearScale)) noexcept
{
    return nearScale > 1.0f ? (farExtentM / nearScale) : farExtentM;
}

inline constexpr float CloudShadowNearTexelM(
    float farTexelM, float nearScale = static_cast<float>(kCloudShadowNearScale)) noexcept
{
    return nearScale > 1.0f ? (farTexelM / nearScale) : farTexelM;
}

// Smooth border fade: 0 outside [0, 1]^2, rising to 1 inside [fadeWidth, 1 - fadeWidth]^2.
inline float CloudShadowEdgeFade(const Vec2& uv, float fadeWidth) noexcept
{
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) {
        return 0.0f;
    }
    const float edge = std::min(std::min(uv.x, 1.0f - uv.x), std::min(uv.y, 1.0f - uv.y));
    const float width = std::max(fadeWidth, 1e-4f);
    const float t = std::clamp(edge / width, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Far-map coordinate (0..1 inside) where the sun ray of `world` crosses the middle of the layer.
inline Vec2 CloudShadowUv(const CloudShadowMap& m, const Vec3& world) noexcept
{
    const float mid = 0.5f * (m.layerBaseM + m.layerTopM);
    const float along = (mid - world.y) / std::max(m.sunDirection.y, 1e-4f);
    const float x = world.x + m.sunDirection.x * along;
    const float z = world.z + m.sunDirection.z * along;
    return Vec2((x - m.centerX) / m.extentM + 0.5f, (z - m.centerZ) / m.extentM + 0.5f);
}

// Nested near-map coordinate (0..1 inside) sharing the far map centre at 1 / nearScale of its extent.
inline Vec2 CloudShadowNearUv(const CloudShadowMap& m, const Vec3& world) noexcept
{
    const float mid = 0.5f * (m.layerBaseM + m.layerTopM);
    const float along = (mid - world.y) / std::max(m.sunDirection.y, 1e-4f);
    const float x = world.x + m.sunDirection.x * along;
    const float z = world.z + m.sunDirection.z * along;
    const float scale = (std::isfinite(m.nearScale) && m.nearScale > 1.0f) ? m.nearScale : static_cast<float>(kCloudShadowNearScale);
    const float nearExtent = m.extentM / scale;
    return Vec2((x - m.centerX) / nearExtent + 0.5f, (z - m.centerZ) / nearExtent + 0.5f);
}

// Near-map blend weight in [0, 1] at `world`: 0 outside or without a near region, 1 inside the fade margin.
inline float CloudShadowNearBlend(const CloudShadowMap& m, const Vec3& world) noexcept
{
    if (!(std::isfinite(m.nearScale) && m.nearScale > 1.0f) || m.mapRows < 2u * kCloudShadowMapSize || !(m.extentM > 0.0f)) {
        return 0.0f;
    }
    return CloudShadowEdgeFade(CloudShadowNearUv(m, world), kCloudShadowNearEdgeFade);
}

inline bool CloudShadowNearCovers(const CloudShadowMap& m, const Vec3& world) noexcept
{
    return CloudShadowNearBlend(m, world) > 0.0f;
}

// Bilinear read of one square region [rowOffset, rowOffset + regionSize) of a row-major r-channel shadow map.
inline float SampleCloudShadowRegionBilinear(std::span<const float> texels, std::uint32_t texWidth,
    std::uint32_t rowOffset, std::uint32_t regionSize, const Vec2& uv) noexcept
{
    if (texWidth == 0 || regionSize == 0 || texels.size() < static_cast<std::size_t>(rowOffset + regionSize) * texWidth) {
        return 1.0f;
    }
    const float size = static_cast<float>(regionSize);
    const float px = uv.x * size - 0.5f;
    const float py = uv.y * size - 0.5f;
    const int i0x = static_cast<int>(std::floor(px));
    const int i0y = static_cast<int>(std::floor(py));
    const float fx = px - static_cast<float>(i0x);
    const float fy = py - static_cast<float>(i0y);
    const int hi = static_cast<int>(regionSize) - 1;
    const auto fetch = [&](int dx, int dy) noexcept {
        const std::uint32_t cx = static_cast<std::uint32_t>(std::clamp(i0x + dx, 0, hi));
        const std::uint32_t cy = static_cast<std::uint32_t>(std::clamp(i0y + dy, 0, hi)) + rowOffset;
        return texels[static_cast<std::size_t>(cy) * texWidth + cx];
    };
    const float top = fetch(0, 0) + (fetch(1, 0) - fetch(0, 0)) * fx;
    const float bottom = fetch(0, 1) + (fetch(1, 1) - fetch(0, 1)) * fx;
    return std::clamp(top + (bottom - top) * fy, 0.0f, 1.0f);
}

// Cubic B-spline (4x4) read of one square region [rowOffset, rowOffset + regionSize), matching CloudShadowLookup.glsl.
inline float SampleCloudShadowRegionCubic(std::span<const float> texels, std::uint32_t texWidth,
    std::uint32_t rowOffset, std::uint32_t regionSize, const Vec2& uv) noexcept
{
    if (texWidth == 0 || regionSize == 0 || texels.size() < static_cast<std::size_t>(rowOffset + regionSize) * texWidth) {
        return 1.0f;
    }
    const float size = static_cast<float>(regionSize);
    const float px = uv.x * size - 0.5f;
    const float py = uv.y * size - 0.5f;
    const int i0x = static_cast<int>(std::floor(px));
    const int i0y = static_cast<int>(std::floor(py));
    const float fx = px - static_cast<float>(i0x);
    const float fy = py - static_cast<float>(i0y);
    const float fx2 = fx * fx;
    const float fy2 = fy * fy;
    const float fx3 = fx2 * fx;
    const float fy3 = fy2 * fy;
    const float wx[4] = {
        (1.0f - 3.0f * fx + 3.0f * fx2 - fx3) / 6.0f,
        (4.0f - 6.0f * fx2 + 3.0f * fx3) / 6.0f,
        (1.0f + 3.0f * fx + 3.0f * fx2 - 3.0f * fx3) / 6.0f,
        fx3 / 6.0f,
    };
    const float wy[4] = {
        (1.0f - 3.0f * fy + 3.0f * fy2 - fy3) / 6.0f,
        (4.0f - 6.0f * fy2 + 3.0f * fy3) / 6.0f,
        (1.0f + 3.0f * fy + 3.0f * fy2 - 3.0f * fy3) / 6.0f,
        fy3 / 6.0f,
    };
    const int hi = static_cast<int>(regionSize) - 1;
    float sum = 0.0f;
    for (int j = 0; j < 4; ++j) {
        const std::uint32_t cy = static_cast<std::uint32_t>(std::clamp(i0y + j - 1, 0, hi)) + rowOffset;
        float row = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const std::uint32_t cx = static_cast<std::uint32_t>(std::clamp(i0x + i - 1, 0, hi));
            row += wx[i] * texels[static_cast<std::size_t>(cy) * texWidth + cx];
        }
        sum += wy[j] * row;
    }
    return std::clamp(sum, 0.0f, 1.0f);
}

// CPU twin of CloudSunTransmittance() in Shaders/CloudShadowLookup.glsl.
inline float SampleCloudSunTransmittance(std::span<const float> texels, std::uint32_t texWidth,
    std::uint32_t texHeight, const CloudShadowMap& m, const Vec3& position) noexcept
{
    const float strength = std::min(m.strength, 1.0f);
    if (strength <= 0.0f || m.sunDirection.y <= 0.02f || !(m.extentM > 0.0f) || texWidth == 0 || texHeight < texWidth
        || texels.size() < static_cast<std::size_t>(texWidth) * texHeight) {
        return 1.0f;
    }
    const float inside = std::clamp((position.y - m.layerBaseM) / std::max(m.layerTopM - m.layerBaseM, 1.0f), 0.0f, 1.0f);
    if (inside >= 1.0f) {
        return 1.0f;
    }
    const float along = (0.5f * (m.layerBaseM + m.layerTopM) - position.y) / m.sunDirection.y;
    const float dx = position.x + m.sunDirection.x * along - m.centerX;
    const float dz = position.z + m.sunDirection.z * along - m.centerZ;
    const float invExtent = 1.0f / m.extentM;
    const Vec2 uv(dx * invExtent + 0.5f, dz * invExtent + 0.5f);
    const float fade = CloudShadowEdgeFade(uv, kCloudShadowFarEdgeFade);
    if (fade <= 0.0f) {
        return 1.0f;
    }
    const std::uint32_t side = texWidth;
    const float scale = (std::isfinite(m.nearScale) && m.nearScale > 1.0f) ? m.nearScale : 0.0f;
    const Vec2 nuv(dx * (invExtent * scale) + 0.5f, dz * (invExtent * scale) + 0.5f);
    const float nfade = (scale > 1.0f && texHeight >= 2u * side && m.mapRows >= 2u * side)
        ? CloudShadowEdgeFade(nuv, kCloudShadowNearEdgeFade)
        : 0.0f;
    float transmittance = (nfade >= 1.0f)
        ? SampleCloudShadowRegionCubic(texels, texWidth, side, side, nuv)
        : SampleCloudShadowRegionCubic(texels, texWidth, 0u, side, uv);
    if (nfade > 0.0f && nfade < 1.0f) {
        const float nearT = SampleCloudShadowRegionCubic(texels, texWidth, side, side, nuv);
        transmittance += (nearT - transmittance) * nfade;
    }
    return 1.0f + (transmittance - 1.0f) * (strength * fade * (1.0f - inside));
}

// CPU twin of CloudSunTransmittanceFast() in Shaders/CloudShadowLookup.glsl.
inline float SampleCloudSunTransmittanceFast(std::span<const float> texels, std::uint32_t texWidth,
    std::uint32_t texHeight, const CloudShadowMap& m, const Vec3& position, float sharpness = 1.0f) noexcept
{
    const float strength = std::min(m.strength, 1.0f);
    if (strength <= 0.0f || m.sunDirection.y <= 0.02f || !(m.extentM > 0.0f) || texWidth == 0 || texHeight < texWidth
        || texels.size() < static_cast<std::size_t>(texWidth) * texHeight) {
        return 1.0f;
    }
    const float inside = std::clamp((position.y - m.layerBaseM) / std::max(m.layerTopM - m.layerBaseM, 1.0f), 0.0f, 1.0f);
    if (inside >= 1.0f) {
        return 1.0f;
    }
    const float along = (0.5f * (m.layerBaseM + m.layerTopM) - position.y) / m.sunDirection.y;
    const float dx = position.x + m.sunDirection.x * along - m.centerX;
    const float dz = position.z + m.sunDirection.z * along - m.centerZ;
    const float invExtent = 1.0f / m.extentM;
    const Vec2 uv(dx * invExtent + 0.5f, dz * invExtent + 0.5f);
    const float fade = CloudShadowEdgeFade(uv, kCloudShadowFarEdgeFade);
    if (fade <= 0.0f) {
        return 1.0f;
    }
    const std::uint32_t side = texWidth;
    const float scale = (std::isfinite(m.nearScale) && m.nearScale > 1.0f) ? m.nearScale : 0.0f;
    const Vec2 nuv(dx * (invExtent * scale) + 0.5f, dz * (invExtent * scale) + 0.5f);
    const float nfade = (scale > 1.0f && texHeight >= 2u * side && m.mapRows >= 2u * side)
        ? CloudShadowEdgeFade(nuv, kCloudShadowNearEdgeFade)
        : 0.0f;
    float transmittance = (nfade >= 1.0f)
        ? SampleCloudShadowRegionBilinear(texels, texWidth, side, side, nuv)
        : SampleCloudShadowRegionBilinear(texels, texWidth, 0u, side, uv);
    if (nfade > 0.0f && nfade < 1.0f) {
        const float nearT = SampleCloudShadowRegionBilinear(texels, texWidth, side, side, nuv);
        transmittance += (nearT - transmittance) * nfade;
    }
    transmittance = std::pow(transmittance, std::max(sharpness, 1.0f));
    return 1.0f + (transmittance - 1.0f) * (strength * fade * (1.0f - inside));
}

} // namespace gte
