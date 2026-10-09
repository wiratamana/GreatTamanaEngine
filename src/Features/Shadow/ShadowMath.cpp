#include "ShadowMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gte {

Mat4 BuildDirectionalShadowViewProjection(
    const Vec3& directionTowardSun, const Vec3& sceneCenterWorld, float halfExtentWorld, float nearZ, float farZ) noexcept
{
    const Vec3 forward = Normalize(-directionTowardSun); // Light travels from the sun toward the scene.

    // Sun straight overhead collapses LookAtLH's basis - swap the up hint.
    const Vec3 referenceUp = (std::abs(Dot(forward, Vec3::Up())) > 0.999f) ? Vec3::Forward() : Vec3::Up();

    const Vec3 eye = sceneCenterWorld - forward * (halfExtentWorld + nearZ);
    const Mat4 view = Mat4::LookAtLH(eye, sceneCenterWorld, referenceUp);

    // Far plane = box depth + caller's own farZ margin - never collapses onto
    // nearZ as long as the caller ran SanitizeShadowSettings() first.
    const Mat4 proj = Mat4::OrthographicLH_ZO(-halfExtentWorld, halfExtentWorld, -halfExtentWorld, halfExtentWorld,
        nearZ, nearZ + halfExtentWorld * 2.0f + farZ, /*flipY=*/true);
    return proj * view;
}

Vec3 ReconstructWorldPositionFromDepth(
    const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept
{
    const Vec4 clip(ndcX, ndcY, depthZeroToOne, 1.0f);
    const Vec4 world = invViewProjection * clip;
    return world.w != 0.0f ? world.XYZ() / world.w : Vec3::Zero();
}

ShadowSettings SanitizeShadowSettings(const ShadowSettings& settings) noexcept
{
    ShadowSettings sanitized = settings;

    constexpr float kMinHalfExtent = 0.01f;
    constexpr float kMinNearZ = 0.01f;
    constexpr float kMinBoxDepth = 0.01f; // Minimum (2*halfExtent + farZ) span.

    sanitized.orthoHalfExtentWorld = std::max(kMinHalfExtent, settings.orthoHalfExtentWorld);
    sanitized.nearZ = std::max(kMinNearZ, settings.nearZ);

    const float boxDepth = sanitized.orthoHalfExtentWorld * 2.0f + settings.farZ;
    sanitized.farZ = boxDepth >= kMinBoxDepth ? settings.farZ : (kMinBoxDepth - sanitized.orthoHalfExtentWorld * 2.0f);

    sanitized.strength = std::clamp(settings.strength, 0.0f, 1.0f);
    sanitized.mapResolution = settings.mapResolution; // Deliberately not touched - see header.

    return sanitized;
}

SceneLightingUniformData PackSceneLightingUniformData(const ResolvedDirectionalLight& sun,
    const Mat4& lightViewProjection, const ShadowSettings& safeSettings, std::uint32_t mapResolutionInUse) noexcept
{
    SceneLightingUniformData data{};
    std::memcpy(data.lightViewProjection, lightViewProjection.Data(), sizeof(data.lightViewProjection));

    data.sunDirectionAndBias[0] = sun.directionTowardSun.x;
    data.sunDirectionAndBias[1] = sun.directionTowardSun.y;
    data.sunDirectionAndBias[2] = sun.directionTowardSun.z;
    data.sunDirectionAndBias[3] = safeSettings.depthBias;

    data.sunIlluminanceAndStrength[0] = sun.sunIlluminance.x;
    data.sunIlluminanceAndStrength[1] = sun.sunIlluminance.y;
    data.sunIlluminanceAndStrength[2] = sun.sunIlluminance.z;
    data.sunIlluminanceAndStrength[3] = safeSettings.strength;

    data.shadowTexelSizeAndPad[0] = mapResolutionInUse > 0 ? 1.0f / static_cast<float>(mapResolutionInUse) : 0.0f;
    data.shadowTexelSizeAndPad[1] = 0.0f;
    data.shadowTexelSizeAndPad[2] = 0.0f;
    data.shadowTexelSizeAndPad[3] = 0.0f;

    return data;
}

} // namespace gte
