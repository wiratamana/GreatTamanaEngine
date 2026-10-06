#pragma once

#include "ShadowTypes.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

namespace gte {

// Light-space view * projection: an orthographic box of `halfExtentWorld`
// centered on `sceneCenterWorld`. `directionTowardSun` must be normalized.
// Caller must pass already-sanitized values (see SanitizeShadowSettings()).
Mat4 BuildDirectionalShadowViewProjection(const Vec3& directionTowardSun, const Vec3& sceneCenterWorld,
    float halfExtentWorld, float nearZ, float farZ) noexcept;

// World-space position from one depth sample, via a camera's own inverse view-projection.
Vec3 ReconstructWorldPositionFromDepth(
    const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept;

// Clamps every numeric field into a safe range, every frame:
//   - orthoHalfExtentWorld: min 0.01 (never zero/negative area).
//   - nearZ: min 0.01 (this engine's [0,1] depth convention needs a positive near plane).
//   - farZ: clamped so (2*halfExtentWorld + farZ) never drops below 0.01.
//   - depthBias: left unclamped (a small positive or negative PCF bias is both legitimate).
//   - strength: clamped to [0, 1].
//   - mapResolution: NOT touched here - see ShadowTypes.h.
ShadowSettings SanitizeShadowSettings(const ShadowSettings& settings) noexcept;

} // namespace gte
