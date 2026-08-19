#pragma once

namespace gte {

class Registry;
struct EditorContext;

// Shows/edits ctx.selectedEntity's (see HierarchyPanel) components -
// Transform fields are directly editable (position/scale as-is, rotation
// shown/edited as Euler degrees and converted back to the stored Quat - see
// Math/Quat.h's FromEulerDegrees()/ToEulerDegrees()); MeshRenderer's handles
// are shown read-only (no asset-picker UI exists yet to let a user reassign
// them). Called once per frame by ImGuiEditorLayer::BuildUI(), after
// BuildHierarchyPanel().
void BuildInspectorPanel(Registry& registry, EditorContext& ctx);

} // namespace gte
