#pragma once

#include "../ECS/Components/Transform.h"
#include "../Math/Mat4.h"

namespace gte {

// The Move / Rotate / Scale choice of the Scene toolbar.
enum class GizmoOperation {
    Translate,
    Rotate,
    Scale,
};

// Call once per frame right after ImGui::NewFrame(). ImGuizmo needs it before any Manipulate().
void BeginGizmoFrame();

// Draws the Move / Rotate / Scale buttons at the current cursor position and writes the choice into `operation`.
void DrawGizmoOperationSwitcher(GizmoOperation& operation);

// Draws and handles one frame of gizmo manipulation for `transform` inside the screen rectangle given by rectX..rectHeight.
// `view` and `projection` stay separate (ImGuizmo needs both). `parentWorld` is the parent's world matrix (identity for a root).
// `orthographic` must match `projection`. Returns true while the user drags; `transform` is already updated.
bool ManipulateTransformGizmo(GizmoOperation operation, const Mat4& view, const Mat4& projection, float rectX,
    float rectY, float rectWidth, float rectHeight, Transform& transform, const Mat4& parentWorld = Mat4::Identity(),
    bool orthographic = false);

// Gives the gizmo calls made inside its lifetime their own interaction id (the default id is -1).
class GizmoIdScope {
public:
    explicit GizmoIdScope(const char* id);
    ~GizmoIdScope();

    GizmoIdScope(const GizmoIdScope&) = delete;
    GizmoIdScope& operator=(const GizmoIdScope&) = delete;
};

// True while the cursor is over any gizmo or one is in use. Global, and reports the previous frame.
bool IsGizmoOver();

// True while the gizmo of the current GizmoIdScope is being dragged.
bool IsGizmoUsing();

// Ends a drag of the current GizmoIdScope. Call when its gizmo stops being drawn.
void CancelGizmoInteraction();

} // namespace gte
