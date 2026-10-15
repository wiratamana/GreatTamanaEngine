#pragma once

// CPU oracle for the volume texture preview's default camera and raymarch proxy shapes.
// The preview compute shader transcribes this math; the CPU result wins on disagreement.
// No Vulkan dependency: plain floats and gte::Vec3.

#include "../Math/Vec3.h"

namespace gte {

// Camera and proxy-box setup derived only from a volume's texel dimensions.
struct VolumeCameraSetup {
    Vec3 eyePosition; // World space (equals the box's local space).
    Vec3 forward; // Normalized, from the eye toward the box center.
    Vec3 right; // Normalized, camera-local +X.
    Vec3 up; // Normalized, camera-local +Y.
    float tanHalfFovY = 0.0f; // Vertical half-FOV tangent, for ray construction.
    Vec3 boxHalfExtents; // Proxy box half-extents; the box is centered at the origin and spans +-boxHalfExtents.
};

// boxHalfExtents = 0.5 * (width, height, depth) / max(width, height, depth): the longest texel dimension maps to
// a full extent of 1.0. Dimensions <= 0 are clamped to 1.
// The camera sits at a fixed isometric-like angle (45 degrees azimuth, ~35.264 degrees elevation) at a distance
// that fits the box's bounding sphere in a 45 degree vertical FOV with a small margin.
VolumeCameraSetup ComputeVolumeCameraSetup(int width, int height, int depth);

// Camera and box for a camera-frustum-shaped volume. Width/height are screen-space indices and depth is a
// distance slice, which are different units, so the box is not scaled by texel counts (that would squash the
// depth axis). The parameters are unused and kept so both setup functions share a signature.
VolumeCameraSetup ComputeAerialPerspectivePreviewCameraSetup(int width, int height, int depth);

// Ray-box intersection against an origin-centered box with the given half-extents.
// Returns false (outputs untouched) if the ray misses the box or the box lies entirely behind the origin.
bool IntersectRayBox(const Vec3& rayOrigin, const Vec3& rayDirection, const Vec3& boxHalfExtents, float& outTEnter,
    float& outTExit);

// Raymarch proxy shaped like a camera frustum: local +Z spans [-halfDepth, +halfDepth] and the cross-section
// half-size grows linearly from (0, 0) at the near apex to (farHalfWidth, farHalfHeight) at the far end.
// The linear taper keeps every side wall a plane.
struct FrustumProxy {
    float halfDepth = 0.0f;
    float farHalfWidth = 0.0f;
    float farHalfHeight = 0.0f;
};

// Clips a ray against the frustum's six half-spaces (one [tEnter, tExit] range each, intersected).
// Same contract as IntersectRayBox().
bool IntersectRayFrustum(const Vec3& rayOrigin, const Vec3& rayDirection, const FrustumProxy& frustum, float& outTEnter,
    float& outTExit);

// Maps a point inside the frustum (in its local space) to normalized [0,1]^3 texture space.
// The apex (z == -halfDepth) maps to x == y == 0.5; a small epsilon floor avoids dividing by zero there.
Vec3 MapFrustumLocalPositionToUvw(const Vec3& localPos, const FrustumProxy& frustum);

// The aerial perspective preview's frustum, built from the same constants as its camera setup.
FrustumProxy ComputeAerialPerspectivePreviewFrustum();

} // namespace gte
