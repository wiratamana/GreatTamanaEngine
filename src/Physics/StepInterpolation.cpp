#include "StepInterpolation.h"

#include "../Math/MathTypes.h"
#include "../Math/Quat.h"

namespace gte {

Vec3 InterpolateToAnchor(const Mat4& anchorNow, const Vec3& prevLocal, const Vec3& currLocal, float alpha) noexcept
{
    const float t = alpha > 0.0f ? Clamp(alpha, 0.0f, 1.0f) : 0.0f; // also maps NaN to 0.
    return anchorNow.TransformPoint(Lerp(prevLocal, currLocal, t));
}

Mat4 BlendAnchorFrames(const Mat4& from, const Mat4& to, float t) noexcept
{
    const float k = t > 0.0f ? Clamp(t, 0.0f, 1.0f) : 0.0f; // also maps NaN to 0.
    const Quat rotation = Slerp(Normalize(Quat::FromMat4(from)), Normalize(Quat::FromMat4(to)), k);
    const Vec3 position = Lerp(from.TransformPoint(Vec3::Zero()), to.TransformPoint(Vec3::Zero()), k);
    return Mat4::TRS(position, rotation, Vec3::One());
}

} // namespace gte
