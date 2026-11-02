#include "StepInterpolation.h"

#include "../Math/MathTypes.h"

namespace gte {

Vec3 InterpolateToAnchor(const Mat4& anchorNow, const Vec3& prevLocal, const Vec3& currLocal, float alpha) noexcept
{
    const float t = alpha > 0.0f ? Clamp(alpha, 0.0f, 1.0f) : 0.0f; // also maps NaN to 0.
    return anchorNow.TransformPoint(Lerp(prevLocal, currLocal, t));
}

Mat4 BlendAnchorFrames(const Mat4& from, const Mat4& to, float t) noexcept
{
    const float k = t > 0.0f ? Clamp(t, 0.0f, 1.0f) : 0.0f; // also maps NaN to 0.
    Mat4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result(row, column) = from(row, column) + (to(row, column) - from(row, column)) * k;
        }
    }
    return result;
}

} // namespace gte
