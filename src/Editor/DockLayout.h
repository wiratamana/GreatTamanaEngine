#pragma once

namespace gte {

struct EditorContext;

// Hosts a full-viewport, invisible window carrying the top menu bar (File >
// Exit, ...) and the DockSpace every other Editor panel docks into, and
// ensures the default Unity-style Hierarchy/Inspector/Scene+Game layout gets
// built exactly once (see DockLayout.cpp for the full one-shot rationale -
// it matters for correctness, not just as an optimization). Called once per
// frame by ImGuiEditorLayer::BuildUI(), before any panel builder
// (Panels/*.h) runs.
void BuildDockspaceAndMenuBar(EditorContext& ctx);

} // namespace gte
