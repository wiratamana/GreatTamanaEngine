#pragma once

// editor-core-separation-1 campaign, PHASE13
// (PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md) - this plain,
// dependency-free struct used to be defined INLINE inside
// src/Editor/EditorLayer.h (an Editor-only header) even though it carries
// zero Editor-specific semantics (a batch's stable display name, its CPU-known
// instance count, and its GPU-computed visible count) and is genuinely
// PRODUCED by gte_core's own per-frame GPU-driven-batch-culling orchestration
// (see src/Core/Core.cpp's own BuildFrame() body, GPU-Driven Frustum Culling +
// Indirect Draw campaign, render-pass-5, PHASE6) - only ever *consumed* by the
// Editor's "Render Graph" panel via IEditorLayer::BuildUI()'s own trailing
// parameter. Since `gte_core` must never `#include` anything under
// `src/Editor/` except `EditorLayer.h` itself (PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #8), and `Core::GetGpuDrivenBatchDebugInfo()` needs
// the COMPLETE type (not just a forward declaration) to return
// `const std::vector<GpuDrivenBatchDebugInfo>&`, this struct's definition
// relocated here - co-located with GpuDrivenBatchCache.h, the subsystem it
// actually describes - with `EditorLayer.h` now `#include`-ing this header
// instead of defining the struct itself. Zero behavior/shape change - a pure,
// minimal, justified data-type relocation, not a redesign.

#include <cstdint>
#include <optional>
#include <string>

namespace gte {

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE6 (task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md)
// - one eligible batch's own "instances culled this frame" readout, ready
// for RenderGraphPanel::Build() (the panel that already shows per-pass
// draw-call/triangle stats) to display. Built fresh, every frame, by
// gte::Core::BuildFrame(), right after Renderer::EndOffscreenRenderGraphRecording()
// returns (the exact point every buffer this frame's GPU-driven culling
// dispatch wrote is already fence-proven complete - see
// GpuDrivenBatchCache::ReadLastKnownVisibleCount()'s own doc comment for why
// no NEW GPU wait is ever added to produce this number).
struct GpuDrivenBatchDebugInfo {
    // The same stable name shown in the Render Graph panel's own pass list
    // (e.g. "GpuDrivenBatch0") - NOT a per-pass name (this one line covers
    // all three/four of that batch's own passes at once).
    std::string batchName;
    // This frame's real, CPU-known instance count (the batch's own total
    // size before culling) - always > 0 (a batch with zero instances is
    // never collected at all - see RenderSystem::CollectGpuDrivenBatches()).
    std::uint32_t instanceCount = 0;
    // The GPU-computed "instances that survived frustum culling this frame"
    // count, read back from the culling compute pass's own atomic visible-
    // count buffer - std::nullopt only in the (should be unreachable in
    // practice) case the readback buffer was never created for this batch
    // yet (e.g. the very first frame this exact batch ever existed, before
    // GpuDrivenBatchCache::EnsureCapacity() ever ran for it).
    std::optional<std::uint32_t> visibleCount;
};

} // namespace gte
