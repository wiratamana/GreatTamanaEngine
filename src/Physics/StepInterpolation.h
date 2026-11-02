#pragma once
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"

namespace gte {

// Display position between two fixed steps, from anchor-local step positions and this frame's anchor matrix.
// alpha is clamped to 0..1 (NaN gives 0).
Vec3 InterpolateToAnchor(const Mat4& anchorNow, const Vec3& prevLocal, const Vec3& currLocal, float alpha) noexcept;

// Anchor matrix at fraction t (clamped to 0..1) between two frames, blended element by element.
// Exact for translations; for small rotations the result is orthonormal to within rounding.
Mat4 BlendAnchorFrames(const Mat4& from, const Mat4& to, float t) noexcept;

} // namespace gte
