#pragma once

#include "../ECS/Components/Camera.h"
#include "../ECS/Components/Transform.h"
#include "../Math/Mat4.h"
#include "../Math/Vec2.h"
#include "SceneViewAxes.h"

#include <cstdint>

namespace gte {

enum class CameraProjectionMode : std::uint8_t { Perspective, Orthographic };

// One frame of Scene view input. Pixels are screen pixels (y down). Seconds are real time, not game time.
struct EditorCameraInput {
    Vec2 mouseDelta = Vec2::Zero();
    float scrollNotches = 0.0f;        // Positive zooms in.
    float deltaSeconds = 0.0f;
    float viewportHeightPixels = 1.0f; // Height of the Scene image.
    bool panning = false;              // Middle button held.
    bool looking = false;              // Right button held.
};

// Camera of the Scene view. Not an ECS entity; no ImGui, SDL or Vulkan.
// The pivot is the point `distance` in front of the eye. Wheel zooms to it, middle drag pans it, a head click turns around it.
class EditorCamera {
public:
    static constexpr float kFovYDegrees = 60.0f;
    static constexpr float kMinDistance = 0.05f;
    static constexpr float kMaxDistance = 100000.0f;

    // Largest accepted SetPose magnitudes: world units and degrees.
    static constexpr float kMaxPoseUnits = 1.0e6f;
    static constexpr float kMaxPoseDegrees = 1.0e5f;

    // Starts at (0, 0, -5) looking along +Z, so the pivot is the origin.
    EditorCamera() noexcept : m_transform{ Vec3{ 0.0f, 0.0f, -kDefaultDistance }, Quat::Identity(), Vec3::One() } { }

    // Applies one frame of input in this order: look, pan, zoom. Non-finite input is ignored.
    void Update(const EditorCameraInput& input) noexcept;

    // Sets eye position and look direction. Pitch is clamped to +-90, yaw wraps to [-180, 180). The distance is kept.
    // Returns false and changes nothing when an input is not finite or absurdly large.
    bool SetPose(Vec3 position, float yawDegrees, float pitchDegrees) noexcept;

    // Looks along the axis around the pivot. Distance and projection stay.
    void AlignTo(SceneViewAxis axis) noexcept;

    // Sets the distance to the pivot and keeps the pivot where it is. False (nothing changes) when out of range or not finite.
    bool SetDistance(float distance) noexcept;

    void SetProjectionMode(CameraProjectionMode mode) noexcept { m_projectionMode = mode; }
    void ToggleProjectionMode() noexcept
    {
        m_projectionMode = IsOrthographic() ? CameraProjectionMode::Perspective : CameraProjectionMode::Orthographic;
    }

    Mat4 View() const noexcept { return Camera::ViewMatrix(m_transform); }

    // Render projection (Vulkan clip space, Y flipped).
    Mat4 Projection(float aspectWidthOverHeight) const noexcept { return ProjectionFor(aspectWidthOverHeight, true); }

    // Same lens without the Y flip; the transform gizmo expects +Y up in NDC.
    Mat4 GizmoProjection(float aspectWidthOverHeight) const noexcept { return ProjectionFor(aspectWidthOverHeight, false); }

    Mat4 ViewProjection(float aspectWidthOverHeight) const noexcept { return Projection(aspectWidthOverHeight) * View(); }

    const Transform& GetTransform() const noexcept { return m_transform; }
    float YawDegrees() const noexcept { return m_yawDegrees; }
    float PitchDegrees() const noexcept { return m_pitchDegrees; }
    float Distance() const noexcept { return m_distance; }
    CameraProjectionMode ProjectionMode() const noexcept { return m_projectionMode; }
    bool IsOrthographic() const noexcept { return m_projectionMode == CameraProjectionMode::Orthographic; }

    Vec3 Right() const noexcept { return m_transform.rotation.RotateVector(Vec3::Right()); }
    Vec3 Up() const noexcept { return m_transform.rotation.RotateVector(Vec3::Up()); }
    Vec3 Forward() const noexcept { return m_transform.rotation.RotateVector(Vec3::Forward()); }
    Vec3 Pivot() const noexcept { return m_transform.position + Forward() * m_distance; }

    // Perspective clip planes. They follow the distance, so a big scene is not cut.
    float NearClip() const noexcept;
    float FarClip() const noexcept;

    // Half of the visible height at the pivot. The orthographic view uses it as its size.
    float HalfHeightAtPivot() const noexcept;

private:
    Mat4 ProjectionFor(float aspectWidthOverHeight, bool flipY) const noexcept;
    void ApplyZoom(float notches, float deltaSeconds) noexcept;
    void ClampPosition() noexcept;

    static constexpr float kDefaultDistance = 5.0f;
    static constexpr float kDegreesPerPixel = 0.2f;
    static constexpr float kMaxPitchDegrees = 90.0f;

    // Each notch closes 10% of the distance. Under 1 unit the eye flies instead: 10% of its height above the ground,
    // and never less than 1 unit.
    static constexpr float kZoomStep = 0.10f;
    static constexpr float kMinFlyStep = 1.0f;

    // Notches closer than this in time form a streak. Each streak notch adds this much gain, up to the maximum.
    static constexpr float kZoomStreakGapSeconds = 0.25f;
    static constexpr float kZoomGainPerNotch = 0.05f;
    static constexpr float kZoomMaxGain = 2.0f;

    // One frame never counts more than this many notches (a fast wheel or a touchpad can report a burst).
    static constexpr float kZoomMaxNotchesPerFrame = 3.0f;

    // A zoom glides: each frame it applies the share 1 - exp(-dt / this) of what is still pending.
    static constexpr float kZoomSmoothSeconds = 0.06f;
    static constexpr float kZoomSettleEpsilon = 1.0e-3f;

    static constexpr float kMinNearClip = 0.1f;
    static constexpr float kNearPerDistance = 0.01f;
    static constexpr float kMinFarClip = 1000.0f;
    static constexpr float kFarPerDistance = 100.0f;

    // Scale is always one; a camera transform never uses it.
    Transform m_transform;

    // Kept apart from the quaternion so small per-frame deltas never accumulate drift.
    float m_yawDegrees = 0.0f;
    float m_pitchDegrees = 0.0f;

    float m_distance = kDefaultDistance;
    CameraProjectionMode m_projectionMode = CameraProjectionMode::Perspective;

    float m_scrollStreakNotches = 0.0f;
    float m_secondsSinceScroll = 1.0f;
    float m_pendingZoomNotches = 0.0f; // Gain-weighted notches not applied yet.
};

} // namespace gte
