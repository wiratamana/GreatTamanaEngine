#include "StepInterpolation.h"

#include "../Math/MathTypes.h"

namespace gte {

Vec3 InterpolateToAnchor(const Mat4& anchorNow, const Vec3& prevLocal, const Vec3& currLocal, float alpha) noexcept
{
    const float t = alpha > 0.0f ? Clamp(alpha, 0.0f, 1.0f) : 0.0f; // also maps NaN to 0.
    return anchorNow.TransformPoint(Lerp(prevLocal, currLocal, t));
}

} // namespace gte
