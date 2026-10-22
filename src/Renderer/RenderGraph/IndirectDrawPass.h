#pragma once

#include "RenderGraphBuilder.h"
#include "RenderGraphHandles.h"
#include "../../Core/GraphicsHandles.h"
#include "../../Math/Mat4.h"

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

// A GPU-driven indexed draw: the draw commands and the visible count live in graph buffers.
struct IndirectDrawRecord {
    TextureHandle target;   // Color and depth attachment (a view target), or the depth target when `depthOnly`.
    BufferHandle indirect;  // VkDrawIndexedIndirectCommand array.
    BufferHandle count;     // Draw count, read on the GPU when `useCount` is set.
    BufferHandle instances; // Per-instance data read by the vertex shader.
    BufferHandle visible;   // Optional second vertex-stage storage buffer (visible-cluster list).
    std::uint32_t maxDrawCount = 0;
    bool useCount = false;  // false: draw maxDrawCount commands without reading the count buffer.
    bool depthOnly = false; // true: write only the depth attachment of `target`; no color attachment.
};

// What the execute side needs besides the record; resolved by the caller each frame.
// With `clusterBindings` set, the set at `clusterBindingIndex` is rewritten from the record's `visible` and
// `instances` buffers right before the draw and replaces `instanceSet`.
struct IndirectDrawInputs {
    const Pipeline* pipeline = nullptr;
    const Mesh* mesh = nullptr;
    DescriptorSetHandle instanceSet = DescriptorSetHandle::Invalid;
    DescriptorSetHandle sceneSet = DescriptorSetHandle::Invalid;
    Mat4 viewProjection = Mat4::Identity();
    ClusterDrawBindings* clusterBindings = nullptr;
    std::size_t clusterBindingIndex = 0;
};

// Why the record cannot be declared, or nullopt.
std::optional<std::string> ValidateIndirectDrawRecord(const IndirectDrawRecord& record);

// Declares the attachment writes and the buffer reads (command, count, vertex-stage storage).
void DeclareIndirectDraw(RenderGraphBuilder::PassBuilder& pass, const IndirectDrawRecord& record);

// Records the draw. Draws nothing when the pipeline or mesh is missing (asserts in debug for a visible-list draw).
void ExecuteIndirectDraw(PassContext& ctx, const IndirectDrawRecord& record, const IndirectDrawInputs& inputs);

} // namespace gte::rg
