#pragma once

#include "../ECS/Components/Camera.h"
#include "../ECS/Components/Transform.h"
#include "../Math/Mat4.h"
#include "../Math/Vec2.h"

namespace gte {

// Fly camera of the secondary view. Not an ECS entity, so it never touches game state; no ImGui/SDL/Vulkan.
// Middle drag: pan. Wheel: dolly. Right drag: mouse look (pitch clamped).
class EditorCamera {
public:
    // Starts at (0, 0, -5) looking along +Z.
    EditorCamera() noexcept : m_transform{ Vec3{ 0.0f, 0.0f, -5.0f }, Quat::Identity(), Vec3::One() } { }

    // mouseDelta is in pixels (Y down). It only applies while middleMouseDown (pan) or
    // rightMouseDown (look) is true. scrollDelta > 0 dollies forward.
    void Update(Vec2 mouseDelta, float scrollDelta, bool middleMouseDown, bool rightMouseDown) noexcept;

    // Sets position and look direction at once. Pitch is clamped like Update(), yaw wraps to [-180, 180).
    // Returns false and changes nothing when an input is not finite or absurdly large.
    bool SetPose(Vec3 position, float yawDegrees, float pitchDegrees) noexcept;

    // Separate view and projection, as the gizmo needs them individually.
    Mat4 View() const noexcept { return Camera::ViewMatrix(m_transform); }
    Mat4 Projection(float aspectWidthOverHeight) const noexcept { return m_camera.ProjectionMatrix(aspectWidthOverHeight); }

    // Projection without the Vulkan clip-space Y flip; the gizmo library expects +Y up in NDC.
    Mat4 GizmoProjection(float aspectWidthOverHeight) const noexcept
    {
        return Mat4::PerspectiveFovLH_ZO(
            DegToRad(m_camera.fovYDegrees), aspectWidthOverHeight, m_camera.nearZ, m_camera.farZ, /*flipY=*/false);
    }

    // Projection * view for a render target of the given aspect ratio.
    Mat4 ViewProjection(float aspectWidthOverHeight) const noexcept
    {
        return Projection(aspectWidthOverHeight) * View();
    }

    const Transform& GetTransform() const noexcept { return m_transform; }
    float YawDegrees() const noexcept { return m_yawDegrees; }
    float PitchDegrees() const noexcept { return m_pitchDegrees; }

    // Largest accepted SetPose magnitudes: world units and degrees.
    static constexpr float kMaxPoseUnits = 1.0e6f;
    static constexpr float kMaxPoseDegrees = 1.0e5f;

private:
    // Scale is always one; a camera transform never uses it.
    Transform m_transform;

    // Only the lens (fov, near, far) is used.
    Camera m_camera{};

    // Kept apart from the quaternion so small per-frame deltas never accumulate drift.
    float m_yawDegrees = 0.0f;
    float m_pitchDegrees = 0.0f;

    // World units per pixel (pan), per wheel notch (dolly) and degrees per pixel (look).
    static constexpr float kPanUnitsPerPixel = 0.01f;
    static constexpr float kZoomUnitsPerWheelNotch = 0.5f;
    static constexpr float kDegreesPerPixel = 0.2f;

    // Pitch stops short of +/-90 so the view never flips upside down.
    static constexpr float kMaxPitchDegrees = 89.0f;
};

} // namespace gte
