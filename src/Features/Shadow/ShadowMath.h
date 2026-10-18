#pragma once

#include "ShadowTypes.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

namespace gte {

// Orthographic light-space view * projection box centered on `sceneCenterWorld`.
// `directionTowardSun` must be normalized; pass sanitized values.
Mat4 BuildDirectionalShadowViewProjection(const Vec3& directionTowardSun, const Vec3& sceneCenterWorld,
    float halfExtentWorld, float nearZ, float farZ) noexcept;

// World position of one depth sample through a camera's inverse view-projection.
Vec3 ReconstructWorldPositionFromDepth(
    const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept;

// Clamps every numeric field into a safe range (extent and near >= 0.01, box depth >= 0.01, strength in [0,1]).
// Depth bias and map resolution are left as given.
ShadowSettings SanitizeShadowSettings(const ShadowSettings& settings) noexcept;

// Parameter block of the mask compute pass. A zero resolution gives a zero texel size.
ShadowMaskParamsGpu MakeShadowMaskParams(const Mat4& invViewProjection, const Mat4& lightViewProjection,
    float depthBias, std::uint32_t mapResolution) noexcept;

} // namespace gte
