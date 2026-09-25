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

    // editor-core-separation-9 campaign, PHASE4
    // (PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md, Step 3.3.5) - a
    // small, additive label distinguishing a `_v3` (multi-pass, generic
    // resource-graph) plugin row from a `_v2` (fixed-op) one. Added after a
    // real, live confirmed gap: without this field, the ONLY way to tell
    // which ABI a `render_features[]`/"Plugin Render Features" row belongs
    // to is by eyeballing the plugin's own chosen `name` string (e.g. it
    // happens to contain "V3") - not a structural signal a third-party
    // plugin author is in any way obligated to follow. Populated by
    // RenderFeatureCompositor::DebugSnapshot()'s own appendStage lambda:
    // `debugEntry.isV3 = (entry.moduleV3 != nullptr);` - trivial, since
    // Entry already distinguishes moduleV2/moduleV3.
    bool isV3 = false;
};

} // namespace gte
