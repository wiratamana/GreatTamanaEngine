#include "EditorCamera.h"

#include <algorithm>
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

void EditorCamera::Update(const EditorCameraInput& input) noexcept
{
    if (!std::isfinite(input.mouseDelta.x) || !std::isfinite(input.mouseDelta.y) || !std::isfinite(input.scrollNotches)
        || !std::isfinite(input.deltaSeconds) || !std::isfinite(input.viewportHeightPixels)) {
        return;
    }

    // Look first, so pan and zoom in the same frame follow the new orientation.
    if (input.looking) {
        const Vec3 pivot = Pivot();
        m_yawDegrees += input.mouseDelta.x * kDegreesPerPixel;
        m_pitchDegrees = Clamp(m_pitchDegrees + input.mouseDelta.y * kDegreesPerPixel, -kMaxPitchDegrees, kMaxPitchDegrees);
        m_transform.rotation = Quat::FromEulerDegrees(m_pitchDegrees, m_yawDegrees, 0.0f);
        if (IsOrthographic()) {
            m_transform.position = pivot - Forward() * m_distance;
        }
    }

    if (input.panning) {
        // Grab and drag: the scene follows the cursor, so the camera moves the opposite way.
        const float unitsPerPixel =
            2.0f * m_distance * std::tan(DegToRad(kFovYDegrees) * 0.5f) / std::max(input.viewportHeightPixels, 1.0f);
        m_transform.position += Right() * (-input.mouseDelta.x * unitsPerPixel) + Up() * (input.mouseDelta.y * unitsPerPixel);
    }

    ApplyZoom(input.scrollNotches, input.deltaSeconds);
    ClampPosition();
}

void EditorCamera::ApplyZoom(float notches, float deltaSeconds) noexcept
{
    const float dt = std::max(deltaSeconds, 0.0f);
    m_secondsSinceScroll += dt;
    if (m_secondsSinceScroll > kZoomStreakGapSeconds) {
        m_scrollStreakNotches = 0.0f;
    }

    if (notches != 0.0f) {
        const float counted = std::clamp(notches, -kZoomMaxNotchesPerFrame, kZoomMaxNotchesPerFrame);
        const float gain = std::min(1.0f + m_scrollStreakNotches * kZoomGainPerNotch, kZoomMaxGain);
        m_scrollStreakNotches += std::fabs(counted);
        m_secondsSinceScroll = 0.0f;
        m_pendingZoomNotches += counted * gain;
    }
    if (m_pendingZoomNotches == 0.0f) {
        return;
    }

    // Glide: apply a share of the pending zoom, so the view eases in instead of jumping. Frame-rate independent.
    float applied = m_pendingZoomNotches * (1.0f - std::exp(-dt / kZoomSmoothSeconds));
    if (std::fabs(m_pendingZoomNotches - applied) < kZoomSettleEpsilon) {
        applied = m_pendingZoomNotches;
    }
    m_pendingZoomNotches -= applied;
    if (applied == 0.0f) {
        return;
    }

    // The eye moves along forward by a share of the distance, so the pivot stays put while the distance is 1 unit or more.
    // Closer than that the eye flies on and drags the pivot with it.
    const float factor = std::pow(1.0f - kZoomStep, applied);
    const float step = std::max(m_distance, kMinFlyStep) * (1.0f - factor);
    m_transform.position += Forward() * step;
    m_distance = std::clamp(m_distance - step, kMinDistance, kMaxDistance);
}

void EditorCamera::ClampPosition() noexcept
{
    Vec3& p = m_transform.position;
    p.x = std::clamp(p.x, -kMaxPoseUnits, kMaxPoseUnits);
    p.y = std::clamp(p.y, -kMaxPoseUnits, kMaxPoseUnits);
    p.z = std::clamp(p.z, -kMaxPoseUnits, kMaxPoseUnits);
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

void EditorCamera::AlignTo(SceneViewAxis axis) noexcept
{
    const Vec3 pivot = Pivot();
    const SceneViewAngles angles = SceneViewAnglesFor(axis);
    m_yawDegrees = WrapDegrees(angles.yawDegrees);
    m_pitchDegrees = angles.pitchDegrees;
    m_transform.rotation = Quat::FromEulerDegrees(m_pitchDegrees, m_yawDegrees, 0.0f);
    m_transform.position = pivot - Forward() * m_distance;
}

bool EditorCamera::SetDistance(float distance) noexcept
{
    if (!std::isfinite(distance) || distance < kMinDistance || distance > kMaxDistance) {
        return false;
    }
    const Vec3 pivot = Pivot();
    m_distance = distance;
    m_transform.position = pivot - Forward() * m_distance;
    return true;
}

float EditorCamera::NearClip() const noexcept
{
    return std::max(kMinNearClip, m_distance * kNearPerDistance);
}

float EditorCamera::FarClip() const noexcept
{
    return std::max(kMinFarClip, m_distance * kFarPerDistance);
}

float EditorCamera::HalfHeightAtPivot() const noexcept
{
    return m_distance * std::tan(DegToRad(kFovYDegrees) * 0.5f);
}

Mat4 EditorCamera::ProjectionFor(float aspectWidthOverHeight, bool flipY) const noexcept
{
    if (IsOrthographic()) {
        const float halfHeight = HalfHeightAtPivot();
        const float halfWidth = halfHeight * aspectWidthOverHeight;
        const float depth = FarClip(); // Symmetric around the eye plane: nothing behind the camera is lost.
        return Mat4::OrthographicLH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, -depth, depth, flipY);
    }
    return Mat4::PerspectiveFovLH_ZO(DegToRad(kFovYDegrees), aspectWidthOverHeight, NearClip(), FarClip(), flipY);
}

} // namespace gte
