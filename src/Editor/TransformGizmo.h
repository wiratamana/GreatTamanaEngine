#pragma once

#include "../ECS/Components/Transform.h"
#include "../Math/Mat4.h"

namespace gte {

// Which manipulation the Scene-view gizmo (see ManipulateTransformGizmo()
// below) is currently performing - Unity's own "Move / Rotate / Scale"
// toolbar switcher, one entry per ImGuizmo::OPERATION bitmask this engine
// actually exposes (ImGuizmo also has a combined UNIVERSAL gizmo and
// per-axis-only operations - not used here, see TransformGizmo.cpp). Stored
// in EditorContext::gizmoOperation (see EditorContext.h) so ScenePanel.cpp's
// own top-left switcher (DrawGizmoOperationSwitcher() below) and the actual
// gizmo manipulation stay in sync frame to frame.
enum class GizmoOperation {
    Translate,
    Rotate,
    Scale,
};

// Call exactly once per frame, right after ImGui::NewFrame() (see
// ImGuiEditorLayer::NewFrame()) - required by ImGuizmo before any
// Manipulate() call that same frame (see ImGuizmo.h's own "call BeginFrame
// right after ImGui_XXXX_NewFrame()" comment). Kept as a tiny wrapper here,
// rather than having ImGuiEditorLayer.cpp include <ImGuizmo.h> itself, so
// this file stays the one and only place in the engine that knows ImGuizmo
// exists (mirroring how EditorCamera.h/.cpp is the one place that knows
// about Scene-camera math, and DockLayout.cpp is the one place that needs
// imgui_internal.h).
void BeginGizmoFrame();

// Draws Unity's own top-left "Move / Rotate / Scale" toolbar switcher as an
// overlay inside the CURRENT ImGui window (see ScenePanel.cpp for the call
// site - it repositions the cursor to the Scene image's top-left corner
// first via ImGui::SetCursorScreenPos() before calling this). Writes the
// user's choice straight into `operation` - callers don't need to inspect
// this function's own return value, there isn't one.
void DrawGizmoOperationSwitcher(GizmoOperation& operation);

// Draws + handles one frame of Unity-style gizmo manipulation for
// `transform`, inside the on-screen pixel rectangle
// [rectX, rectY, rectX+rectWidth, rectY+rectHeight] (screen-space, i.e. the
// same coordinate space ImGui::GetItemRectMin()/GetItemRectSize() report -
// see ScenePanel.cpp), using the given camera view/projection matrices
// (kept SEPARATE, never pre-multiplied - ImGuizmo needs them individually,
// unlike RenderSystem::Draw()'s combined view-projection). Must be called
// while the target ImGui window (e.g. "Scene") is the current window, since
// ImGuizmo::SetDrawlist() (called internally) defaults to appending to
// whichever window's ImDrawList is current.
//
// `parentWorld` is `transform`'s OWN parent's fully-resolved world matrix
// (ECS/TransformHierarchy.h's ComputeWorldMatrix(registry, parentEntity),
// or Mat4::Identity() - the default - for a root/unparented entity, i.e.
// every entity before Transform gained a parent-hierarchy field). This
// engine has no Registry/ECS dependency in this file at all (see the class
// comment below), so the caller (ScenePanel.cpp, which already has a
// Registry in hand) resolves it and passes it in rather than this function
// reaching for it itself. The gizmo manipulates in WORLD space
// (parentWorld * transform's own local matrix) and converts the result back
// into `transform`'s own LOCAL fields afterwards (parentWorld's inverse *
// the manipulated world matrix) - for a root entity (parentWorld ==
// Identity()) this is exactly the old "manipulate transform.
// LocalToWorldMatrix() directly" behavior, unchanged.
//
// Always LOCAL-space manipulation from ImGuizmo's own point of view
// (ImGuizmo::LOCAL, i.e. the gizmo's handles are oriented along `transform`'s
// own rotated axes rather than the world axes) - orthogonal to the
// world-vs-parent-local conversion above; see TransformGizmo.cpp for why
// ImGuizmo::LOCAL was chosen.
//
// Returns true while the user is actively dragging the gizmo this frame
// (ImGuizmo::Manipulate()'s own return value) - `transform` has already been
// updated in place either way by the time this returns; the bool is purely
// informational for a caller that might want to react to it (e.g. suppress
// something else while a drag is in progress), nothing in this engine reads
// it yet.
bool ManipulateTransformGizmo(GizmoOperation operation, const Mat4& view, const Mat4& projection, float rectX,
    float rectY, float rectWidth, float rectHeight, Transform& transform, const Mat4& parentWorld = Mat4::Identity());

} // namespace gte
