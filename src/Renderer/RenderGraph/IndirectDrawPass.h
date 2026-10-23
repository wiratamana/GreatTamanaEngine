#pragma once

#include "RenderGraphBuilder.h"
#include "RenderGraphHandles.h"
#include "../../Core/GraphicsHandles.h"
#include "../../Math/Mat4.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace gte {
class Mesh;
class Pipeline;
} // namespace gte

namespace gte::rg {

class ClusterDrawBindings;
struct PassContext;

// A GPU-driven counted indexed draw: the commands, the visible count and the per-draw lists live in graph buffers.
struct IndirectDrawRecord {
    TextureHandle target;   // Color and depth attachment (a view target), or the depth target when `depthOnly`.
    BufferHandle indirect;  // VkDrawIndexedIndirectCommand array.
    BufferHandle count;     // Draw count, read on the GPU.
    BufferHandle instances; // Per-instance data read by the vertex shader.
    BufferHandle visible;   // Visible-cluster list read by the vertex shader.
    std::uint32_t maxDrawCount = 0;
    bool depthOnly = false; // true: write only the depth attachment of `target`; no color attachment.
    std::optional<std::array<float, 4>> clearColor; // Clears the color attachment when set (loads otherwise).
    std::optional<float> clearDepth;                // Clears the depth attachment when set (loads otherwise).
};

// What the execute side needs besides the record; resolved by the caller each frame.
// The set at `clusterBindingIndex` is rewritten from the record's `visible` and `instances` buffers right
// before the draw.
struct IndirectDrawInputs {
    const Pipeline* pipeline = nullptr;
    const Mesh* mesh = nullptr;
    DescriptorSetHandle sceneSet = DescriptorSetHandle::Invalid;
    Mat4 viewProjection = Mat4::Identity();
    ClusterDrawBindings* clusterBindings = nullptr;
    std::size_t clusterBindingIndex = 0;
};

// Why the record cannot be declared, or nullopt.
std::optional<std::string> ValidateIndirectDrawRecord(const IndirectDrawRecord& record);

// Declares the attachment writes and the buffer reads (command, count, vertex-stage storage).
void DeclareIndirectDraw(RenderGraphBuilder::PassBuilder& pass, const IndirectDrawRecord& record);

// Records the draw. Draws nothing (and asserts in debug) when the pipeline, mesh or bindings are missing.
void ExecuteIndirectDraw(PassContext& ctx, const IndirectDrawRecord& record, const IndirectDrawInputs& inputs);

} // namespace gte::rg
