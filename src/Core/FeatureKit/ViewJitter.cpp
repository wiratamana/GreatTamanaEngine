#include "ViewJitter.h"

#include <cmath>

namespace gte::fx {

bool IsValidViewJitter(Vec2 jitterNdc) noexcept
{
    return std::isfinite(jitterNdc.x) && std::isfinite(jitterNdc.y)
        && std::fabs(jitterNdc.x) <= kMaxViewJitterNdc && std::fabs(jitterNdc.y) <= kMaxViewJitterNdc;
}

Mat4 ApplyClipJitter(const Mat4& viewProjection, Vec2 jitterNdc) noexcept
{
    if (jitterNdc.x == 0.0f && jitterNdc.y == 0.0f) {
        return viewProjection;
    }
    Mat4 result = viewProjection;
    for (int c = 0; c < 4; ++c) {
        result(0, c) = viewProjection(0, c) + jitterNdc.x * viewProjection(3, c);
        result(1, c) = viewProjection(1, c) + jitterNdc.y * viewProjection(3, c);
    }
    return result;
}

} // namespace gte::fx
