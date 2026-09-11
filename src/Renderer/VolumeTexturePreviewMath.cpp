#include "VolumeTexturePreviewMath.h"

#include "../Math/MathTypes.h"

#include <algorithm>
#include <cmath>

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

} // namespace gte
