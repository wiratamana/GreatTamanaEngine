#include "VolumeTexturePreviewMath.h"

#include "../Math/MathTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gte {

namespace {

// True isometric angle - atan(1/sqrt(2)) - see this file's own header
// comment.
constexpr float kAzimuthDegrees = 45.0f;
constexpr float kElevationDegrees = 35.264389682754654f;
constexpr float kFovYDegrees = 45.0f;
// A small margin beyond the exact "bounding sphere just touches the FOV
// cone" distance, so the box never looks like it's clipping the frame's
// edge.
constexpr float kDistanceMargin = 1.2f;

// atmosphere-scattering-3 campaign, Phase 2 - fixed box half-extents for the Aerial
// Perspective preview: X/Y stay modest (narrower than the generic
// function's own 0.5, deliberately, to leave headroom below), while Z (the
// depth/distance axis) is given a much LARGER, fixed half-extent so it
// reads as the visually dominant axis - matching how the reference concept
// diagram (task_manager/atmosphere-scattering-3/
// aerial-persepective-lut-3d-texture.png) draws it: a shape that visibly
// RECEDES away from the camera, not a flat slab. These two constants (and
// the camera angle ones below) are explicitly re-tuned empirically in
// PHASE4_VISUAL_TUNING_AGAINST_REFERENCE_IMAGE.md - treat the values below
// as a reasonable, principled STARTING point, not a final, load-bearing
// choice.
constexpr float kAerialPreviewXYHalfExtent = 0.35f;
constexpr float kAerialPreviewDepthHalfExtent = 0.9f; // ~2.5x the XY half-extent.

// A near-SIDE-ON viewing angle - UNLIKE ComputeVolumeCameraSetup()'s own
// true-isometric 45/35.26 degree angle, this is chosen so the camera looks
// mostly ACROSS the elongated depth axis (rather than staring down its own
// barrel), which is what actually reveals a near-to-far color/opacity
// transition as a visible gradient across the rendered frame - the same
// "camera positioned to one side, subject recedes toward the other side of
// frame" composition the reference diagram itself uses.
constexpr float kAerialPreviewAzimuthDegrees = 85.0f; // atmosphere-scattering-3 Phase 4: raised from 75 -> 85 (closer to a pure side-on view) so the camera's own "right" axis aligns closer with the frustum's world-Z tapering axis, making the near->far widening read as a left-to-right sweep across the frame (matching the reference diagram's own left-to-right composition) instead of a diagonal one.
constexpr float kAerialPreviewElevationDegrees = 10.0f; // atmosphere-scattering-3 Phase 4: lowered from 18 -> 10 (still enough tilt to read as a 3D wedge, not a flat edge-on profile) to keep the widening sweep closer to the frame's horizontal middle rather than skewed toward one corner.
constexpr float kAerialPreviewFovYDegrees = 40.0f;
constexpr float kAerialPreviewDistanceMargin = 1.25f; // atmosphere-scattering-3 Phase 4: settled at 1.25 (started 1.15 -> too tight per Phase 3's own measured ~92.8% FOV-boundary margin -> tried 1.45, which fixed the margin but made the shape look too small within the 256x256 frame -> 1.25 keeps a comfortable safety margin while still filling a legible portion of the frame).

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

    // Spherical -> Cartesian direction from the origin (the box's center)
    // toward the camera - Y-up, matching this engine's own coordinate
    // convention (see Vec3.h).
    const Vec3 directionFromCenter = Vec3(cosElevation * std::sin(azimuthRadians), std::sin(elevationRadians),
        cosElevation * std::cos(azimuthRadians));

    setup.eyePosition = directionFromCenter * distance;

    // Same left-handed look-at basis Mat4::LookAtLH itself builds
    // (Mat4.cpp) - forward = Normalize(target - eye), right =
    // Normalize(Cross(up, forward)), up = Cross(forward, right). The box's
    // center (the target) is the origin, so forward = Normalize(-eyePosition).
    setup.forward = Normalize(Vec3::Zero() - setup.eyePosition);
    const Vec3 worldUp = Vec3::Up();
    setup.right = Normalize(Cross(worldUp, setup.forward));
    setup.up = Cross(setup.forward, setup.right);

    return setup;
}

VolumeCameraSetup ComputeAtmosphereAerialPerspectivePreviewCameraSetup(int width, int height, int depth)
{
    (void)width;
    (void)height;
    (void)depth; // Deliberately unused - see this function's own header comment in VolumeTexturePreviewMath.h.

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

    // IEEE-754 1.0/0.0 == +inf, -1.0/0.0 == -inf - deliberately NOT guarded
    // with an epsilon, see this feature's own strategy document (Step
    // 3.1's implementation notes) for why the slab method already handles
    // this correctly via min/max as long as this is a real division (never
    // exactly 0.0/0.0).
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
// atmosphere-scattering-3 campaign, Phase 3
// (task_manager/atmosphere-scattering-3/PHASE3_FRUSTUM_SHAPED_RAYMARCH_PROXY.md)

namespace {

struct HalfSpacePlane {
    Vec3 normal;
    float constant = 0.0f; // Half-space is "inside" where Dot(normal, pos) + constant <= 0.
};

// Clips the running [tEnter, tExit] range against ONE half-space - returns
// false the instant the range becomes empty (tEnter > tExit), exactly like
// IntersectRayBox()'s own final range check, just applied incrementally
// per-plane instead of once at the end (necessary here since, unlike the
// axis-aligned box case, there is no single component-wise min/max
// shortcut across all six planes at once).
bool ClipRayAgainstHalfSpace(
    const Vec3& rayOrigin, const Vec3& rayDirection, const HalfSpacePlane& plane, float& tEnter, float& tExit)
{
    const float denom = Dot(plane.normal, rayDirection);
    const float originValue = Dot(plane.normal, rayOrigin) + plane.constant;
    constexpr float kEpsilon = 1e-8f;

    if (std::fabs(denom) < kEpsilon) {
        // Ray direction is parallel to this plane - either the ray origin
        // is already on the "inside" side of it for its entire length, or
        // it never is.
        return originValue <= 0.0f;
    }

    const float t = -originValue / denom;
    if (denom > 0.0f) {
        // Moving along +rayDirection moves FROM inside TOWARD outside -
        // this plane bounds the EXIT side.
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

    // Near/far caps - axis-aligned in Z only.
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, 0.0f, -1.0f), -frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, 0.0f, 1.0f), -frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }

    // Four tapering side walls: half-width(z) = kx * (z + halfDepth),
    // half-height(z) = ky * (z + halfDepth) - zero at z=-halfDepth (the
    // apex), farHalfWidth/farHalfHeight at z=+halfDepth (the far cap).
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
    // Cross-section half-size AT this z - floored to a small epsilon so a
    // point exactly at (or numerically near) the apex never divides by ~0.
    constexpr float kMinCrossSection = 1e-5f;
    const float halfWidthAtZ = std::max(kx * (localPos.z + frustum.halfDepth), kMinCrossSection);
    const float halfHeightAtZ = std::max(ky * (localPos.z + frustum.halfDepth), kMinCrossSection);

    const float u = (localPos.x / halfWidthAtZ) * 0.5f + 0.5f;
    const float v = (localPos.y / halfHeightAtZ) * 0.5f + 0.5f;
    return Vec3(u, v, w);
}

FrustumProxy ComputeAtmosphereAerialPerspectivePreviewFrustum()
{
    FrustumProxy frustum;
    frustum.halfDepth = kAerialPreviewDepthHalfExtent;
    frustum.farHalfWidth = kAerialPreviewXYHalfExtent * 2.0f; // The far (wide) end deliberately exceeds the old box's own flat half-extent, so the WIDENING itself is visually obvious, not subtle.
    frustum.farHalfHeight = kAerialPreviewXYHalfExtent * 2.0f;
    return frustum;
}

} // namespace gte
