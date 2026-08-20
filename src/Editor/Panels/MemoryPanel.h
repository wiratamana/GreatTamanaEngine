#pragma once

namespace gte {

struct EditorContext;
class Renderer;

// Unity-Memory-Profiler-style panel: a header of aggregate GPU memory
// totals (Renderer::GetMemoryTotals()) followed by a sortable table listing
// every currently-live GPU resource (Renderer::GetMemoryResources()) - name
// (if any - see CreateBuffer()/CreateRenderTexture()/CreateMesh()'s
// debugName parameter), type (Buffer/Texture), memory location (device-
// local/host-visible/shared), and size, biggest first (see
// MemoryPanelData.h's BuildMemoryRows()). Reads `renderer` only - never
// mutates it - and needs no ECS/Registry access at all, unlike Hierarchy/
// Inspector. Called once per frame by ImGuiEditorLayer::BuildUI(), after
// the other panels.
void BuildMemoryPanel(EditorContext& ctx, Renderer& renderer);

} // namespace gte
