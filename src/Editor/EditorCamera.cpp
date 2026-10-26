#include "EditorCamera.h"

#include <cmath>

namespace gte {

namespace {

// Wraps an angle to [-180, 180).
float WrapDegrees(float degrees) noexcept
{
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped - 180.0f;
}

bool IsWithin(float value, float limit) noexcept
{
    return std::isfinite(value) && std::fabs(value) <= limit;
}

} // namespace

void EditorCamera::Update(Vec2 mouseDelta, float scrollDelta, bool middleMouseDown, bool rightMouseDown) noexcept
{
    // Look first, so pan and dolly in the same frame follow the new orientation.
    if (rightMouseDown) {
        // Positive yaw turns forward toward right; positive pitch tilts forward down.
        m_yawDegrees += mouseDelta.x * kDegreesPerPixel;
        m_pitchDegrees = Clamp(m_pitchDegrees + mouseDelta.y * kDegreesPerPixel, -kMaxPitchDegrees, kMaxPitchDegrees);
        m_transform.rotation = Quat::FromEulerDegrees(m_pitchDegrees, m_yawDegrees, 0.0f);
    }

    const Vec3 right = m_transform.rotation.RotateVector(Vec3::Right());
    const Vec3 up = m_transform.rotation.RotateVector(Vec3::Up());
    const Vec3 forward = m_transform.rotation.RotateVector(Vec3::Forward());

    if (middleMouseDown) {
        // Grab and drag: the scene follows the cursor, so the camera moves the opposite way.
        m_transform.position += right * (-mouseDelta.x * kPanUnitsPerPixel) + up * (mouseDelta.y * kPanUnitsPerPixel);
    }

    if (scrollDelta != 0.0f) {
        m_transform.position += forward * (scrollDelta * kZoomUnitsPerWheelNotch);
    }
}

bool EditorCamera::SetPose(Vec3 position, float yawDegrees, float pitchDegrees) noexcept
{
    if (!IsWithin(position.x, kMaxPoseUnits) || !IsWithin(position.y, kMaxPoseUnits)
        || !IsWithin(position.z, kMaxPoseUnits) || !IsWithin(yawDegrees, kMaxPoseDegrees)
        || !IsWithin(pitchDegrees, kMaxPoseDegrees)) {
        return false;
    }
    m_yawDegrees = WrapDegrees(yawDegrees);
    m_pitchDegrees = Clamp(pitchDegrees, -kMaxPitchDegrees, kMaxPitchDegrees);
    m_transform.position = position;
    m_transform.rotation = Quat::FromEulerDegrees(m_pitchDegrees, m_yawDegrees, 0.0f);
    return true;
}

} // namespace gte
