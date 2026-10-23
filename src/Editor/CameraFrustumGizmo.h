#pragma once

// Pure geometry of a perspective camera frustum: corners, line list and plane normals.

#include "../Math/Mat4.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"
#include "../Renderer/Culling/CullingTypes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gte {

// Lens limits a valid frustum must respect.
inline constexpr float kFrustumMinFovDegrees = 1.0f;
inline constexpr float kFrustumMaxFovDegrees = 170.0f;
inline constexpr float kFrustumMaxFar = 1.0e7f;

struct FrustumInput {
    Vec3 position = Vec3::Zero();
    Quat rotation = Quat::Identity();
    float fovYDegrees = 60.0f;
    float nearZ = 0.1f;
    float farZ = 1000.0f;
    float aspect = 16.0f / 9.0f;
};

// Corner order of both rings: bottom-left, bottom-right, top-right, top-left (seen along forward).
struct FrustumCorners {
    Vec3 nearC[4];
    Vec3 farC[4];
    Vec3 apex;
    Vec3 forward;
    Vec3 up;
    Vec3 right;
};

enum class FrustumEdgeKind : std::uint8_t { Near, Far, Side, Apex, Arrow };

struct FrustumSegment {
    Vec3 a;
    Vec3 b;
    FrustumEdgeKind kind = FrustumEdgeKind::Side;
};

// Finite values, fov 1..170, 0 < near < far <= 1e7, aspect > 0, non-zero rotation.
bool FrustumInputIsValid(const FrustumInput& input) noexcept;

// Built from the camera basis, so it does not depend on the projection's Y flip.
FrustumCorners ComputeFrustumCorners(const FrustumInput& input);

// Corners of the NDC box x,y in [-1,1], z in [0,1] of a view-projection. Empty for a singular or non-finite
// matrix. `apex` is the near-plane centre; `forward`, `up` and `right` come from the near ring.
std::optional<FrustumCorners> FrustumCornersFromViewProjection(const Mat4& viewProjection);

// 4 near + 4 far + 4 side + 4 apex lines + 1 arrow (17 segments).
std::vector<FrustumSegment> BuildFrustumSegments(const FrustumCorners& corners);

// Face of the frustum a clip plane bounds: "Left", "Right", "Bottom", "Top", "Near" or "Far".
// The inward normal is matched against the six face normals of `basis`, so the plane's array index does not matter.
const char* NameFrustumPlane(const Plane& plane, const FrustumCorners& basis);

// Arrow along a plane's inward normal, starting at the centre of the face it bounds.
struct FrustumPlaneArrow {
    Vec3 from;
    Vec3 to;
    const char* name = "";
};

// One arrow per plane, 10% of the far distance long.
std::vector<FrustumPlaneArrow> BuildFrustumPlaneArrows(std::span<const Plane> planes, const FrustumCorners& corners);

} // namespace gte
