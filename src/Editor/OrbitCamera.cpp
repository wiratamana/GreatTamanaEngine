#include "OrbitCamera.h"

#include "../Math/MathTypes.h"

#include <algorithm>
#include <cmath>

namespace gte {

Vec3 OrbitCamera::Eye() const noexcept
{
    const float yaw = DegToRad(yawDeg);
    const float pitch = DegToRad(pitchDeg);
    const Vec3 toEye(std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
    return target + toEye * distance;
}

Mat4 OrbitCamera::View() const noexcept
{
    return Mat4::LookAtLH(Eye(), target, Vec3::Up());
}

Mat4 OrbitCamera::Projection(float aspect) const noexcept
{
    return Mat4::PerspectiveFovLH_ZO(DegToRad(kFovYDegrees), aspect, NearZ(), FarZ(), /*flipY=*/true);
}

Mat4 OrbitCamera::GizmoProjection(float aspect) const noexcept
{
    return Mat4::PerspectiveFovLH_ZO(DegToRad(kFovYDegrees), aspect, NearZ(), FarZ(), /*flipY=*/false);
}

float OrbitCamera::MinDistance() const noexcept
{
    return std::max(0.01f, radius * 0.001f);
}

float OrbitCamera::MaxDistance() const noexcept
{
    return std::max(MinDistance(), radius * kMaxDistanceInRadii);
}

float OrbitCamera::NearZ() const noexcept
{
    return std::max(0.01f, std::min(radius * 0.002f, distance * 0.05f));
}

float OrbitCamera::FarZ() const noexcept
{
    return std::max(distance + radius * 6.0f, NearZ() * 2.0f);
}

void OrbitCamera::Frame(const Vec3& center, float meshRadius) noexcept
{
    const bool finite = std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z);
    target = finite ? center : Vec3::Zero();
    radius = (std::isfinite(meshRadius) && meshRadius > 1.0e-4f) ? meshRadius : 1.0f;
    yawDeg = 20.0f;
    pitchDeg = 12.0f;
    const float fit = radius / std::tan(DegToRad(kFovYDegrees) * 0.5f) * 1.6f;
    distance = std::clamp(fit, MinDistance(), MaxDistance());
}

void OrbitCamera::Rotate(float dx, float dy) noexcept
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return;
    }
    yawDeg += dx * kDegreesPerPixel;
    pitchDeg = Clamp(pitchDeg + dy * kDegreesPerPixel, -kMaxPitchDegrees, kMaxPitchDegrees);
}

void OrbitCamera::Pan(float dx, float dy) noexcept
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return;
    }
    const Vec3 forward = Normalize(target - Eye());
    Vec3 right = Normalize(Cross(Vec3::Up(), forward));
    if (LengthSquared(right) < kEpsilon) {
        right = Vec3::Right();
    }
    const Vec3 up = Cross(forward, right);
    const float speed = distance * kPanPerPixelPerDistance;
    target += right * (-dx * speed) + up * (dy * speed);
}

void OrbitCamera::Dolly(float wheel) noexcept
{
    if (!std::isfinite(wheel)) {
        return;
    }
    distance = std::clamp(distance - wheel * distance * kWheelStep, MinDistance(), MaxDistance());
}

} // namespace gte
