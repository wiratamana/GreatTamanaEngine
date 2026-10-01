#pragma once

#include <cstdint>
#include <string>

// editor-core-separation-6 campaign, PHASE7
// (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - a plain, dependency-free debug
// snapshot of ONE loaded IRenderFeatureModule_v2 entry, produced by
// RenderFeatureCompositor::DebugSnapshot() (gte_core, src/Core/Plugins/
// RenderFeatureCompositor.h/.cpp) and consumed ONLY by the Editor's
// "Render Graph" panel (gte_editor, src/Editor/Panels/RenderGraphPanel.cpp)
// via IEditorLayer::BuildUI()'s own new trailing parameter.
//
// Mirrors src/Renderer/Culling/GpuDrivenBatchDebugInfo.h's own established
// precedent EXACTLY: a small, dependency-free struct co-located with the
// gte_core subsystem that actually produces it, rather than defined inline
// inside the much heavier producing header (RenderFeatureCompositor.h pulls
// in ComputeDescriptorSet.h/ComputePipeline.h/RenderTexture.h/
// RenderGraphBuilder.h/volk.h) or inside an Editor-tier header. This keeps
// EditorLayer.h (the one documented gte_core-visible exception file - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8, editor-core-
// separation-6/PHASE0) able to #include this one small header for the
// complete type IEditorLayer::BuildUI()'s signature needs, with zero new
// Vulkan-handle/compute-pipeline dependency added to that header.
namespace gte {

struct RenderFeatureDebugEntry {
    std::string name;      // copied from descriptor.name (bounded char[64]).
    std::string stage;     // "PostComposite" or "PreUI" (human-readable).
    std::int32_t priority = 0;
    std::string blendMode; // "Replace"/"AlphaOver"/"Additive"/"Multiply"/"ScreenSpaceMask".
    // editor-core-separation-8 campaign, PHASE2 - the host-side enable/
    // disable override (RenderFeatureCompositor::Entry::enabledOverride).
    // Defaults true - matches every existing DebugSnapshot() call site's own
    // prior output for a struct that never went through this campaign's own
    // new SetFeatureEnabled() at all.
    bool enabled = true;
    // better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
    // `isV3` (a label distinguishing a `_v3` multi-pass plugin row from a
    // `_v2` fixed-op one, editor-core-separation-9 campaign, PHASE4) removed
    // outright - meaningless once no plugin of either kind can ever load
    // again (nothing has been able to call Core::LoadPlugins() since PHASE2
    // of this same campaign).

    // editor-core-separation-23 campaign, PHASE2
    // (PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md, Step 3.5) - a
    // Project Assembly's own on-screen render feature
    // (Core::RegisterProjectRenderFeature(), editor-core-separation-23
    // campaign). Always true today - better-render-pass-2 campaign, PHASE4
    // removed every other kind of entry this struct could ever describe
    // (the `_v2`/`_v3` plugin-discovery `isV3` field, above) - kept as a
    // real, non-dead field anyway, since a future non-Project-Assembly
    // render-feature origin is not structurally impossible.
    // Populated by RenderFeatureCompositor::DebugSnapshot()'s own appendStage
    // lambda: `debugEntry.isProjectFeature = static_cast<bool>(entry.projectCallback);`.
    bool isProjectFeature = false;
};

} // namespace gte
