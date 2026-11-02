#pragma once
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"

namespace gte {

// Display position between two fixed steps.
// prevLocal/currLocal are positions in the anchor frame at the end of the previous/last step;
// anchorNow is the anchor world matrix of this render frame. alpha is clamped to 0..1 (NaN gives 0).
Vec3 InterpolateToAnchor(const Mat4& anchorNow, const Vec3& prevLocal, const Vec3& currLocal, float alpha) noexcept;

} // namespace gte
