#pragma once
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"

namespace gte {

// Display position between two fixed steps, from anchor-local step positions and this frame's anchor matrix.
// alpha is clamped to 0..1 (NaN gives 0).
Vec3 InterpolateToAnchor(const Mat4& anchorNow, const Vec3& prevLocal, const Vec3& currLocal, float alpha) noexcept;

// Rigid anchor matrix at fraction t (clamped to 0..1): quaternion slerp of the rotations, lerp of the translations.
// Inputs must be unit-scale; the result is always orthonormal.
Mat4 BlendAnchorFrames(const Mat4& from, const Mat4& to, float t) noexcept;

} // namespace gte
