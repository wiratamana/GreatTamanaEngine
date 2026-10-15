#pragma once

#include <cstdint>
#include <span>

namespace gte {

// Aggregate draw-call and triangle totals of one recorded pass (FrameRecorder::RecordFrame()'s result).
struct DrawStats {
    std::uint32_t drawCallCount = 0;
    std::uint32_t triangleCount = 0;

    // Number of indirect draw calls issued. The CPU cannot know the triangles those draws produce, so this is
    // shown separately and never summed into the two counts above. Only AccumulateIndirectDrawStats() bumps it.
    std::uint32_t indirectDrawCount = 0;
};

// Adds one queued draw to `stats`: one triangle per 3 indices (indexed) or 3 vertices. Pure and allocation-free.
// Call it from the recording loop's own vkCmdDraw/vkCmdDrawIndexed branch so counts cannot diverge from draws.
// Exact for the always-triangle-list pipelines with instanceCount 1; a count not divisible by 3 truncates.
inline void AccumulateDrawStats(
    DrawStats& stats, bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) noexcept
{
    ++stats.drawCallCount;
    const std::uint32_t primitiveVertexCount = hasIndexBuffer ? indexCount : vertexCount;
    stats.triangleCount += primitiveVertexCount / 3;
}

// Adds one Renderer::SubmitIndirect() call; leaves drawCallCount and triangleCount untouched.
inline void AccumulateIndirectDrawStats(DrawStats& stats) noexcept
{
    ++stats.indirectDrawCount;
}

// One queued draw's countable shape, used only by CountDrawStats().
struct CountableDrawItem {
    bool hasIndexBuffer = false;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
};

// Applies AccumulateDrawStats() to every item in order; lets tests count without a live FrameRecorder.
DrawStats CountDrawStats(std::span<const CountableDrawItem> items) noexcept;

} // namespace gte
