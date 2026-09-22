#pragma once

#include <cstdint>
#include <span>

namespace gte {

// One recorded pass's aggregate draw-call/triangle totals - see
// FrameRecorder::RecordFrame()'s return value, and AGENTS.md's "Profiling"
// section. Part of Phase 3 (PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md)
// of PROFILER_STRATEGY_v2.md.
struct DrawStats {
    std::uint32_t drawCallCount = 0;
    std::uint32_t triangleCount = 0;

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE2 (task_manager/render-pass-5/
    // PHASE2_INSTANCED_DRAW_PRIMITIVE_AND_INDIRECT_SUBMIT.md) - a COUNT OF
    // INDIRECT DRAW CALLS ISSUED (e.g. one Renderer::SubmitIndirect() call
    // == +1 here), which the CPU genuinely does know - NOT a count of the
    // objects/triangles actually drawn by them, which only the GPU knows
    // (this campaign's whole point is to avoid a blocking CPU readback to
    // find that out - mirrors GpuSampleStatus::Absent's own "never
    // fabricate a bare numeric 0" rule, GpuTiming.h). A future consumer
    // (the Editor's "Profiler"/"Render Graph" panels) must display this
    // SEPARATELY from drawCallCount/triangleCount above, never summed into
    // them - they measure fundamentally different things (an exact,
    // CPU-known count vs. "at least one indirect batch ran, real
    // object/triangle count is GPU-side only"). Never incremented by
    // Renderer::Submit()/AccumulateDrawStats() above - only by
    // AccumulateIndirectDrawStats() below, mirroring
    // Renderer::SubmitIndirect()/FrameRecorder::IssueIndirectDrawCommand()'s
    // own, completely separate call path.
    std::uint32_t indirectDrawCount = 0;
};

// Accumulates ONE queued draw's contribution into `stats` - pure,
// allocation-free, no Vulkan dependency. `hasIndexBuffer`/`vertexCount`/
// `indexCount` mirror FrameRecorder::DrawItem's own fields exactly (see
// FrameRecorder.cpp's RecordFrame(): vkCmdDrawIndexed() vs. vkCmdDraw()).
//
// DELIBERATELY meant to be called from INSIDE FrameRecorder::RecordFrame()'s
// existing per-item loop, at the exact same branch that already decides
// vkCmdDraw vs. vkCmdDrawIndexed - never from a separate pass over a
// separately-built list. This is a correctness decision, not a style
// preference: a separate counting pass over the same queue would be a
// second, independent place that has to keep agreeing with whatever the
// real recording loop actually does (including any future skip/validity
// branch added there) - fusing the two into one loop makes divergence
// between "what was counted" and "what was actually drawn" structurally
// impossible instead of something that has to be remembered.
//
// One triangle per 3 indices (when hasIndexBuffer) or 3 vertices
// (otherwise) - mirroring FrameRecorder::RecordFrame()'s own branch
// exactly. Dividing by 3 is always EXACT for this engine today: every
// Pipeline is unconditionally built with VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
// (see Pipeline.cpp), and every draw THROUGH THIS FUNCTION today has
// instanceCount == 1 (this function is only ever called from
// Renderer::Submit()'s own per-entity path - GPU-Driven Frustum Culling +
// Indirect Draw campaign, render-pass-5, PHASE2 added real instancing via a
// SEPARATE call path, Renderer::SubmitIndirect()/
// AccumulateIndirectDrawStats() below, which never touches this function or
// this formula). A future non-triangle-list pipeline variant would need
// this formula (and this comment) revisited - see AGENTS.md's "Profiling"
// section. A malformed count not evenly divisible by 3 (never produced by
// any real importer today) simply truncates via integer division,
// mirroring what the GPU itself would do with a truncated final
// primitive - never a crash.
inline void AccumulateDrawStats(
    DrawStats& stats, bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) noexcept
{
    ++stats.drawCallCount;
    const std::uint32_t primitiveVertexCount = hasIndexBuffer ? indexCount : vertexCount;
    stats.triangleCount += primitiveVertexCount / 3;
}

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE2 (task_manager/render-pass-5/
// PHASE2_INSTANCED_DRAW_PRIMITIVE_AND_INDIRECT_SUBMIT.md) - accumulates ONE
// Renderer::SubmitIndirect() call's contribution into `stats` - pure,
// allocation-free, no Vulkan dependency. Mirrors AccumulateDrawStats()'s
// own "fused with the real call site" discipline; a future render-graph-
// wired consumer (PHASE5 onward, once a real pass actually calls
// Renderer::SubmitIndirect()) is expected to call this from that exact
// call site - out of scope for PHASE2 itself (see that phase's own "no
// render-graph/ECS wiring yet" note). Deliberately does NOT touch
// drawCallCount/triangleCount - see DrawStats::indirectDrawCount's own doc
// comment for why those two remain completely unaffected by an indirect
// draw.
inline void AccumulateIndirectDrawStats(DrawStats& stats) noexcept
{
    ++stats.indirectDrawCount;
}

// One queued draw's pure, countable shape - used ONLY by the test-facing
// batch wrapper below (CountDrawStats()), never by production code, which
// calls AccumulateDrawStats() directly inline instead (see its own comment
// above for why).
struct CountableDrawItem {
    bool hasIndexBuffer = false;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
};

// Test-facing convenience wrapper: applies AccumulateDrawStats() once per
// item, in order, over an already-built list. Exists so
// tests/Renderer/DrawStatsTests.cpp can write plain, table-driven
// "items in, DrawStats out" cases without needing a live FrameRecorder -
// see this file's own header comment and AGENTS.md's "Profiling" section.
// Never allocates on the caller's behalf beyond what `items` itself
// already occupies; safe to call every frame if a future caller ever
// prefers this shape over the inline accumulator.
DrawStats CountDrawStats(std::span<const CountableDrawItem> items) noexcept;

} // namespace gte
