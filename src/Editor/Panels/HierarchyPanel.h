#pragma once

namespace gte {

class Registry;
struct EditorContext;

// Lists every entity that has a Transform (this engine's ECS has no
// separate "GameObject"/name concept yet - see ECS/Components/Transform.h -
// so a Transform is the closest thing to "something that belongs in the
// Hierarchy", the same role it plays in Unity). Clicking an entry writes
// ctx.selectedEntity, which InspectorPanel then displays/edits. Called once
// per frame by ImGuiEditorLayer::BuildUI(), before BuildInspectorPanel().
void BuildHierarchyPanel(Registry& registry, EditorContext& ctx);

} // namespace gte
