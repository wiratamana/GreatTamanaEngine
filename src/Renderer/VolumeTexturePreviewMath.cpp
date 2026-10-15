#include "VolumeTexturePreviewMath.h"

#include "../Math/MathTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gte {

namespace {

// True isometric angle: atan(1/sqrt(2)).
constexpr float kAzimuthDegrees = 45.0f;
constexpr float kElevationDegrees = 35.264389682754654f;
constexpr float kFovYDegrees = 45.0f;
// Margin beyond the exact "bounding sphere touches the FOV cone" distance, so the box never clips the frame edge.
constexpr float kDistanceMargin = 1.2f;

// Aerial perspective preview box: the depth axis is much longer than X/Y so it reads as a shape receding from the camera.
constexpr float kAerialPreviewXYHalfExtent = 0.35f;
constexpr float kAerialPreviewDepthHalfExtent = 0.9f; // ~2.5x the XY half-extent.

// Near side-on view: the camera looks across the depth axis so a near-to-far gradient is visible in the frame.
constexpr float kAerialPreviewAzimuthDegrees = 85.0f; // Aligns the camera's right axis with the frustum's depth axis.
constexpr float kAerialPreviewElevationDegrees = 10.0f; // Enough tilt to read as a 3D wedge, not a flat profile.
constexpr float kAerialPreviewFovYDegrees = 40.0f;
constexpr float kAerialPreviewDistanceMargin = 1.25f; // Safe margin that still fills a legible part of the frame.

} // namespace

VolumeCameraSetup ComputeVolumeCameraSetup(int width, int height, int depth)
{
    const float w = width > 0 ? static_cast<float>(width) : 1.0f;
    const float h = height > 0 ? static_cast<float>(height) : 1.0f;
    const float d = depth > 0 ? static_cast<float>(depth) : 1.0f;
    const float maxDim = std::max(w, std::max(h, d));

    VolumeCameraSetup setup;
    setup.boxHalfExtents = Vec3(w, h, d) * (0.5f / maxDim);

    const float fovYRadians = DegToRad(kFovYDegrees);
    setup.tanHalfFovY = std::tan(fovYRadians * 0.5f);

    const float boundingRadius = Length(setup.boxHalfExtents);
    // sin(fovY/2) = boundingRadius / distance -> distance = boundingRadius / sin(fovY/2).
    const float distance = (boundingRadius / std::sin(fovYRadians * 0.5f)) * kDistanceMargin;

    const float azimuthRadians = DegToRad(kAzimuthDegrees);
    const float elevationRadians = DegToRad(kElevationDegrees);
    const float cosElevation = std::cos(elevationRadians);

    // Spherical -> Cartesian direction from the box center toward the camera (Y-up).
    const Vec3 directionFromCenter = Vec3(cosElevation * std::sin(azimuthRadians), std::sin(elevationRadians),
        cosElevation * std::cos(azimuthRadians));

    setup.eyePosition = directionFromCenter * distance;

    // Left-handed look-at basis, as Mat4::LookAtLH builds it. The target is the origin.
    setup.forward = Normalize(Vec3::Zero() - setup.eyePosition);
    const Vec3 worldUp = Vec3::Up();
    setup.right = Normalize(Cross(worldUp, setup.forward));
    setup.up = Cross(setup.forward, setup.right);

    return setup;
}

VolumeCameraSetup ComputeAerialPerspectivePreviewCameraSetup(int width, int height, int depth)
{
    (void)width;
    (void)height;
    (void)depth; // Unused on purpose: the box shape does not depend on texel counts.

    VolumeCameraSetup setup;
    setup.boxHalfExtents = Vec3(kAerialPreviewXYHalfExtent, kAerialPreviewXYHalfExtent, kAerialPreviewDepthHalfExtent);

    const float fovYRadians = DegToRad(kAerialPreviewFovYDegrees);
    setup.tanHalfFovY = std::tan(fovYRadians * 0.5f);

    const float boundingRadius = Length(setup.boxHalfExtents);
    const float distance = (boundingRadius / std::sin(fovYRadians * 0.5f)) * kAerialPreviewDistanceMargin;

    const float azimuthRadians = DegToRad(kAerialPreviewAzimuthDegrees);
    const float elevationRadians = DegToRad(kAerialPreviewElevationDegrees);
    const float cosElevation = std::cos(elevationRadians);
    const Vec3 directionFromCenter = Vec3(cosElevation * std::sin(azimuthRadians), std::sin(elevationRadians),
        cosElevation * std::cos(azimuthRadians));

    setup.eyePosition = directionFromCenter * distance;
    setup.forward = Normalize(Vec3::Zero() - setup.eyePosition);
    const Vec3 worldUp = Vec3::Up();
    setup.right = Normalize(Cross(worldUp, setup.forward));
    setup.up = Cross(setup.forward, setup.right);
    return setup;
}

bool IntersectRayBox(
    const Vec3& rayOrigin, const Vec3& rayDirection, const Vec3& boxHalfExtents, float& outTEnter, float& outTExit)
{
    const Vec3 boxMin = -boxHalfExtents;
    const Vec3 boxMax = boxHalfExtents;

    // Division by a zero component gives +-inf on purpose: the slab min/max handles it without an epsilon.
    const Vec3 invDir(1.0f / rayDirection.x, 1.0f / rayDirection.y, 1.0f / rayDirection.z);

    const Vec3 t0((boxMin.x - rayOrigin.x) * invDir.x, (boxMin.y - rayOrigin.y) * invDir.y,
        (boxMin.z - rayOrigin.z) * invDir.z);
    const Vec3 t1((boxMax.x - rayOrigin.x) * invDir.x, (boxMax.y - rayOrigin.y) * invDir.y,
        (boxMax.z - rayOrigin.z) * invDir.z);

    const Vec3 tMin(std::min(t0.x, t1.x), std::min(t0.y, t1.y), std::min(t0.z, t1.z));
    const Vec3 tMax(std::max(t0.x, t1.x), std::max(t0.y, t1.y), std::max(t0.z, t1.z));

    const float tEnter = std::max(std::max(tMin.x, tMin.y), tMin.z);
    const float tExit = std::min(std::min(tMax.x, tMax.y), tMax.z);

    if (tExit < std::max(tEnter, 0.0f)) {
        return false;
    }

    outTEnter = tEnter;
    outTExit = tExit;
    return true;
}

namespace {

struct HalfSpacePlane {
    Vec3 normal;
    float constant = 0.0f; // Half-space is "inside" where Dot(normal, pos) + constant <= 0.
};

// Clips the running [tEnter, tExit] range against one half-space; false once the range is empty.
bool ClipRayAgainstHalfSpace(
    const Vec3& rayOrigin, const Vec3& rayDirection, const HalfSpacePlane& plane, float& tEnter, float& tExit)
{
    const float denom = Dot(plane.normal, rayDirection);
    const float originValue = Dot(plane.normal, rayOrigin) + plane.constant;
    constexpr float kEpsilon = 1e-8f;

    if (std::fabs(denom) < kEpsilon) {
        // Parallel to the plane: the ray is inside for its whole length or never.
        return originValue <= 0.0f;
    }

    const float t = -originValue / denom;
    if (denom > 0.0f) {
        // Moving along the ray goes from inside to outside: this plane bounds the exit side.
        tExit = std::min(tExit, t);
    } else {
        tEnter = std::max(tEnter, t);
    }
    return tEnter <= tExit;
}

} // namespace

bool IntersectRayFrustum(
    const Vec3& rayOrigin, const Vec3& rayDirection, const FrustumProxy& frustum, float& outTEnter, float& outTExit)
{
    float tEnter = -std::numeric_limits<float>::infinity();
    float tExit = std::numeric_limits<float>::infinity();

    // Near/far caps, axis-aligned in Z.
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, 0.0f, -1.0f), -frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, 0.0f, 1.0f), -frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }

    // Four tapering side walls: half-width(z) = kx * (z + halfDepth), half-height(z) = ky * (z + halfDepth).
    const float kx = frustum.farHalfWidth / (2.0f * frustum.halfDepth);
    const float ky = frustum.farHalfHeight / (2.0f * frustum.halfDepth);

    // x <= kx*(z+halfDepth)  ->  x - kx*z - kx*halfDepth <= 0
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(1.0f, 0.0f, -kx), -kx * frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }
    // x >= -kx*(z+halfDepth)  ->  -x - kx*z - kx*halfDepth <= 0
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(-1.0f, 0.0f, -kx), -kx * frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, 1.0f, -ky), -ky * frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, -1.0f, -ky), -ky * frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }

    if (tExit < std::max(tEnter, 0.0f)) {
        return false;
    }
    outTEnter = tEnter;
    outTExit = tExit;
    return true;
}

Vec3 MapFrustumLocalPositionToUvw(const Vec3& localPos, const FrustumProxy& frustum)
{
    const float w = (localPos.z + frustum.halfDepth) / (2.0f * frustum.halfDepth); // [0,1] along depth.
    const float kx = frustum.farHalfWidth / (2.0f * frustum.halfDepth);
    const float ky = frustum.farHalfHeight / (2.0f * frustum.halfDepth);
    // Cross-section half-size at this z, floored so a point at the apex never divides by ~0.
    constexpr float kMinCrossSection = 1e-5f;
    const float halfWidthAtZ = std::max(kx * (localPos.z + frustum.halfDepth), kMinCrossSection);
    const float halfHeightAtZ = std::max(ky * (localPos.z + frustum.halfDepth), kMinCrossSection);

    const float u = (localPos.x / halfWidthAtZ) * 0.5f + 0.5f;
    const float v = (localPos.y / halfHeightAtZ) * 0.5f + 0.5f;
    return Vec3(u, v, w);
}

FrustumProxy ComputeAerialPerspectivePreviewFrustum()
{
    FrustumProxy frustum;
    frustum.halfDepth = kAerialPreviewDepthHalfExtent;
    frustum.farHalfWidth = kAerialPreviewXYHalfExtent * 2.0f; // Wide far end makes the widening obvious.
    frustum.farHalfHeight = kAerialPreviewXYHalfExtent * 2.0f;
    return frustum;
}

} // namespace gte
