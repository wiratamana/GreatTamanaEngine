#pragma once

#include "Buffer.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace gte {

// The engine's minimal "geometry" primitive: a GPU-resident vertex buffer
// plus how many vertices it holds, PLUS an OPTIONAL index buffer (see
// HasIndexBuffer()/IndexBuffer()/IndexCount() below). Construct via
// Renderer::CreateMesh() (see Renderer.h) rather than directly - same
// convention as Buffer/RenderTexture/Pipeline.
//
// Index-buffer support was added once something actually needed one - a
// real imported mesh (a *.gta AssetType::Mesh - see src/Assets/MeshData.h)
// has genuine vertex reuse worth sharing via its own triangle-index list,
// unlike PrimitiveMeshGenerator's built-in shapes, which stay plain,
// NON-INDEXED triangle lists (duplicated-per-triangle vertices) and keep
// using the non-indexed constructor below unchanged. FrameRecorder::
// RecordFrame() picks vkCmdDrawIndexed() over vkCmdDraw() per-DrawItem based
// purely on whether HasIndexBuffer() was true at Submit() time - see
// FrameRecorder.h/.cpp.
class Mesh {
public:
    // Non-indexed constructor (unchanged from before this engine had index
    // buffers at all) - every PrimitiveMeshGenerator-built shape still uses
    // this.
    Mesh(Buffer vertexBuffer, std::uint32_t vertexCount) noexcept
        : m_vertexBuffer(std::move(vertexBuffer))
        , m_vertexCount(vertexCount)
    {
    }

    // Indexed constructor - for a mesh whose CPU-side data already came
    // with a real triangle-index list (see MeshData::indices).
    Mesh(Buffer vertexBuffer, std::uint32_t vertexCount, Buffer indexBuffer, std::uint32_t indexCount) noexcept
        : m_vertexBuffer(std::move(vertexBuffer))
        , m_vertexCount(vertexCount)
        , m_indexBuffer(std::move(indexBuffer))
        , m_indexCount(indexCount)
    {
    }

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&&) noexcept = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    VkBuffer VertexBuffer() const noexcept { return m_vertexBuffer.Native(); }
    std::uint32_t VertexCount() const noexcept { return m_vertexCount; }

    // True only for a Mesh built via the indexed constructor above - a
    // caller (FrameRecorder::Submit()) must check this before calling
    // IndexBuffer()/IndexCount(), same "check before you index" convention
    // as MeshData::skinWeights.
    bool HasIndexBuffer() const noexcept { return m_indexBuffer.has_value(); }
    VkBuffer IndexBuffer() const noexcept { return m_indexBuffer->Native(); }
    std::uint32_t IndexCount() const noexcept { return m_indexCount; }

private:
    Buffer m_vertexBuffer;
    std::uint32_t m_vertexCount = 0;

    // std::optional rather than a second always-present Buffer - Buffer has
    // no default constructor (every Buffer is a real, immediately-valid GPU
    // allocation - see Buffer.h), so "no index buffer at all" (the
    // non-indexed constructor above) has to be represented by genuine
    // absence, not a zero-sized/sentinel Buffer.
    std::optional<Buffer> m_indexBuffer;
    std::uint32_t m_indexCount = 0;
};

} // namespace gte
