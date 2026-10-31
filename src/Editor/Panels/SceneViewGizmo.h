#pragma once

#include "../SceneViewAxes.h"

#include <imgui.h>

#include <optional>

namespace gte {

class EditorCamera;

// Width the gizmo takes at the top-right of the Scene image. Overlay labels keep clear of it.
inline constexpr float kSceneViewGizmoReservedWidth = 112.0f;

struct SceneViewGizmoResult {
    std::optional<SceneViewAxis> alignTo; // A head was clicked.
    bool toggleProjection = false;        // The text under the gizmo was clicked.
};

// Draws the gizmo and its text at the top-right of the Scene image and reports this frame's click.
// Draw it before anything that picks with the left mouse button.
SceneViewGizmoResult DrawSceneViewGizmo(const ImVec2& imageMin, const ImVec2& imageSize, const EditorCamera& camera);

} // namespace gte
