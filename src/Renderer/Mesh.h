#pragma once

#include "Buffer.h"
#include "Culling/CullingTypes.h" // AABB
#include "RenderGraph/GpuBuffer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace gte {

// A vertex buffer, its vertex count and an optional index buffer. Create through Renderer.
// The vertex buffer is shared, so several meshes (one per material part) can draw from one buffer with their own indices.
class Mesh {
public:
    // Non-indexed.
    Mesh(Buffer vertexBuffer, std::uint32_t vertexCount)
        : m_vertexBuffer(std::make_shared<Buffer>(std::move(vertexBuffer)))
        , m_vertexCount(vertexCount)
    {
    }

    // Indexed, with a private vertex buffer.
    Mesh(Buffer vertexBuffer, std::uint32_t vertexCount, Buffer indexBuffer, std::uint32_t indexCount)
        : m_vertexBuffer(std::make_shared<Buffer>(std::move(vertexBuffer)))
        , m_vertexCount(vertexCount)
        , m_indexBuffer(std::move(indexBuffer))
        , m_indexCount(indexCount)
    {
    }

    // Indexed, sharing a vertex buffer that other meshes may point at; only the index buffer is private.
    Mesh(std::shared_ptr<Buffer> sharedVertexBuffer, std::uint32_t vertexCount, Buffer indexBuffer,
        std::uint32_t indexCount) noexcept
        : m_vertexBuffer(std::move(sharedVertexBuffer))
        , m_vertexCount(vertexCount)
        , m_indexBuffer(std::move(indexBuffer))
        , m_indexCount(indexCount)
    {
    }

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&&) noexcept = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    VkBuffer VertexBuffer() const noexcept { return m_vertexBuffer->Native(); }
    // Engine handle of the vertex buffer; equal for meshes sharing one buffer.
    BufferObjectHandle VertexBufferHandle() const noexcept { return m_vertexBuffer->ObjectHandle(); }
    std::uint32_t VertexCount() const noexcept { return m_vertexCount; }

    // Check before calling IndexBuffer() or IndexCount().
    bool HasIndexBuffer() const noexcept { return m_indexBuffer.has_value(); }
    VkBuffer IndexBuffer() const noexcept { return m_indexBuffer->Native(); }
    std::uint32_t IndexCount() const noexcept { return m_indexCount; }

    // Identity of the vertex buffer: equal for meshes sharing it. An opaque token, never dereferenced.
    const void* VertexBufferIdentity() const noexcept { return m_vertexBuffer.get(); }

    // Overwrites the whole vertex buffer (`size` must match it). Throws unless the buffer is host-writable, which
    // only skinned meshes have. A shared buffer changes for every mesh using it: call once per identity per frame.
    void UpdateVertexData(const void* data, std::size_t size) { m_vertexBuffer->Upload(data, size); }

    // Local-space bounds of the geometry, set once at load. Empty for meshes whose bounds were never computed.
    void SetLocalBounds(const AABB& bounds) noexcept { m_localBounds = bounds; }
    const std::optional<AABB>& LocalBounds() const noexcept { return m_localBounds; }

    // Cluster table over this mesh's index buffer (read-only storage buffer). Owned by the mesh.
    // `clusterBounds` is the union of every cluster box; empty when unknown.
    bool HasClusters() const noexcept { return m_clusterTable.has_value() && m_clusterCount > 0; }
    std::uint32_t ClusterCount() const noexcept { return m_clusterCount; }
    const rg::GpuBuffer* ClusterTable() const noexcept { return m_clusterTable ? &*m_clusterTable : nullptr; }
    const std::optional<AABB>& ClusterBounds() const noexcept { return m_clusterBounds; }
    void SetClusterTable(rg::GpuBuffer table, std::uint32_t count, const std::optional<AABB>& clusterBounds) noexcept
    {
        m_clusterTable.emplace(std::move(table));
        m_clusterCount = count;
        m_clusterBounds = clusterBounds;
    }

private:
    std::shared_ptr<Buffer> m_vertexBuffer;
    std::uint32_t m_vertexCount = 0;

    // Absent for a non-indexed mesh; always private to this mesh.
    std::optional<Buffer> m_indexBuffer;
    std::uint32_t m_indexCount = 0;

    std::optional<AABB> m_localBounds;

    std::optional<rg::GpuBuffer> m_clusterTable;
    std::uint32_t m_clusterCount = 0;
    std::optional<AABB> m_clusterBounds;
};

} // namespace gte
