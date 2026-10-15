#pragma once

#include "RenderGraphBuilder.h"
#include "RenderGraphHandles.h"
#include "../../Core/GraphicsHandles.h"
#include "../../Math/Mat4.h"

#include <cstdint>
#include <optional>
#include <string>

namespace gte {
class Mesh;
class Pipeline;
} // namespace gte

namespace gte::rg {

struct PassContext;

// A GPU-driven indexed draw: the draw commands and the visible count live in graph buffers.
struct IndirectDrawRecord {
    TextureHandle target;   // Color and depth attachment (a view target carries both).
    BufferHandle indirect;  // VkDrawIndexedIndirectCommand array.
    BufferHandle count;     // Draw count, read on the GPU when `useCount` is set.
    BufferHandle instances; // Per-instance data read by the vertex shader.
    std::uint32_t maxDrawCount = 0;
    bool useCount = false;  // false: draw maxDrawCount commands without reading the count buffer.
};

// What the execute side needs besides the record; resolved by the caller each frame.
struct IndirectDrawInputs {
    const Pipeline* pipeline = nullptr;
    const Mesh* mesh = nullptr;
    DescriptorSetHandle instanceSet = DescriptorSetHandle::Invalid;
    DescriptorSetHandle sceneSet = DescriptorSetHandle::Invalid;
    Mat4 viewProjection = Mat4::Identity();
};

// Why the record cannot be declared, or nullopt.
std::optional<std::string> ValidateIndirectDrawRecord(const IndirectDrawRecord& record);

// Declares the attachment writes and the buffer reads (command read, count read, vertex-stage storage read).
void DeclareIndirectDraw(RenderGraphBuilder::PassBuilder& pass, const IndirectDrawRecord& record);

// Records the draw. Does nothing when the pipeline or mesh is missing.
void ExecuteIndirectDraw(PassContext& ctx, const IndirectDrawRecord& record, const IndirectDrawInputs& inputs);

} // namespace gte::rg
