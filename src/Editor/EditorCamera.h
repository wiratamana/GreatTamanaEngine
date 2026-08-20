#pragma once

#include "../ECS/Components/Camera.h"
#include "../ECS/Components/Transform.h"
#include "../Math/Mat4.h"
#include "../Math/Vec2.h"

namespace gte {

// Editor-only fly camera, exclusively for the "Scene" panel
// (Panels/ScenePanel.cpp) - deliberately separate from anything in Game's
// ECS world (never an Entity/Transform/Camera component Game owns), so
// looking around the Scene view never touches - or is touched by -
// gameplay state. This is what finally makes "Scene" genuinely different
// from "Game" (see the "REMAINING LIMITATION" this used to be, noted in
// ImGuiEditorLayer.cpp/README.md before this file existed): "Game" still
// renders through whichever ECS entity has the active Camera component
// (RenderSystem::ResolveActiveCameraViewProjection()), while "Scene" now
// renders through THIS camera instead (see
// IEditorLayer::SceneViewProjection()).
//
// Mirrors Unity's own Scene view controls:
//   - Middle-mouse drag: pan (truck/pedestal) along the camera's own local
//     right/up axes.
//   - Mouse wheel:       dolly along the camera's own local forward axis.
//   - Right-mouse drag:  look around - yaw around the WORLD up axis, pitch
//     around the camera's own local right axis (clamped so it can never
//     flip upside down), i.e. ordinary FPS-camera mouselook.
//
// Deliberately pure logic - no ImGui/SDL/Vulkan dependency at all, unlike
// most of src/Editor/ - so it is Tier-1-testable exactly like the rest of
// the engine (see AGENTS.md, "Testability & Regression Safety", and
// tests/Editor/EditorCameraTests.cpp). Panels/ScenePanel.cpp is the one
// place that actually reads ImGui's mouse state (hover, drag deltas, wheel,
// button state) and calls Update() below with plain values - this class
// itself never touches ImGui.
class EditorCamera {
public:
    // Starts positioned back along -Z looking toward the origin with an
    // identity rotation, deliberately matching Game's own demo-scene Camera
    // entity (see Game::EnsureDemoSceneBuilt()) - purely so the very first
    // time a user opens "Scene" it already shows something, not a blank
    // RenderTexture; there is no other link between the two cameras.
    EditorCamera() noexcept : m_transform{ Vec3{ 0.0f, 0.0f, -5.0f }, Quat::Identity(), Vec3::One() } { }

    // Advances this camera by one frame's worth of Scene-panel mouse input,
    // entirely in terms of plain values (see class comment for why this
    // takes no ImGui type at all). `mouseDelta` is this frame's raw
    // mouse-motion delta in pixels (X, Y - Y positive downward, matching
    // screen-space convention); it is only actually applied while
    // `middleMouseDown` (pan) or `rightMouseDown` (rotate) is true -
    // ScenePanel.cpp is responsible for only ever passing true for either
    // once the corresponding drag actually started while hovering the Scene
    // panel itself (see its own comment for why, and for why both keep
    // responding even if the cursor later drifts outside the panel mid-
    // drag). `scrollDelta` is this frame's raw mouse-wheel delta (positive
    // = scrolled away from the user, i.e. "zoom/dolly in").
    void Update(Vec2 mouseDelta, float scrollDelta, bool middleMouseDown, bool rightMouseDown) noexcept;

    // Combined projection * view matrix for a render target of the given
    // aspect ratio - what ImGuiEditorLayer::SceneViewProjection() hands
    // straight to RenderSystem::Draw() for the Scene view, in place of
    // RenderSystem::ResolveActiveCameraViewProjection() (which is what the
    // Game view still uses). Reuses Camera's own pure-math helpers directly
    // (see ECS/Components/Camera.h) rather than duplicating that math here.
    Mat4 ViewProjection(float aspectWidthOverHeight) const noexcept
    {
        return m_camera.ProjectionMatrix(aspectWidthOverHeight) * Camera::ViewMatrix(m_transform);
    }

    const Transform& GetTransform() const noexcept { return m_transform; }
    float YawDegrees() const noexcept { return m_yawDegrees; }
    float PitchDegrees() const noexcept { return m_pitchDegrees; }

private:
    // Position/rotation this camera is currently at - edited in place by
    // Update() above. Scale is always Vec3::One() (unused - see
    // Camera::ViewMatrix()'s own comment on why a camera Transform's scale
    // is never meaningful).
    Transform m_transform;

    // Only its ProjectionMatrix()/field-of-view/near-far are ever used
    // here; `active` is meaningless for this camera (it is never a
    // Registry entry RenderSystem could pick up - see class comment).
    Camera m_camera{};

    // Yaw/pitch, in degrees, tracked independently of m_transform.rotation
    // (which is only ever the QUAT rebuilt FROM these two each time they
    // change) - see Update() - so repeatedly nudging them by a small
    // per-frame delta never accumulates floating-point drift the way
    // repeatedly re-deriving yaw/pitch back out of a quaternion each frame
    // would.
    float m_yawDegrees = 0.0f;
    float m_pitchDegrees = 0.0f;

    // Arbitrary, hand-tuned-to-feel-right constants - world units per pixel
    // of drag (pan/zoom) or degrees per pixel of drag (rotate). Free to
    // retune later; nothing else in the engine depends on their exact
    // values.
    static constexpr float kPanUnitsPerPixel = 0.01f;
    static constexpr float kZoomUnitsPerWheelNotch = 0.5f;
    static constexpr float kDegreesPerPixel = 0.2f;

    // Pitch is clamped to just short of +/-90 degrees so the camera can
    // never rotate past looking straight up/down into looking upside-down
    // (the same gimbal-adjacent flip every FPS-style mouselook camera
    // avoids this way).
    static constexpr float kMaxPitchDegrees = 89.0f;
};

} // namespace gte
