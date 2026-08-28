#pragma once

#include "Buffer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
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
//
// SHARED vertex buffer support (multithreaded CPU-skinning optimization,
// Stage 1 - see task_manager/optimizing_multi_thread_cpu_skinning/
// MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md) was added because a
// single imported multi-material model used to get one FULL, independent
// copy of its ENTIRE vertex buffer per material "part" (each part's own
// index buffer differed, but the vertex buffer content was identical,
// duplicated verbatim) - turning both the CPU-side per-frame skinning
// re-upload cost AND the GPU-side memory footprint into
// O(vertexCount * partCount) instead of O(vertexCount). The vertex buffer
// is therefore held via std::shared_ptr rather than by value: several Mesh
// instances (one per material part) can now all point at the exact SAME
// underlying GPU buffer, each with its own, genuinely distinct index
// buffer/range - see MeshAssetGpuCatalog.cpp's textured-submesh path and
// Renderer::CreateMeshFromSharedVertexBuffer(). The non-shared constructors
// below are unaffected in behavior (still exactly one Mesh, one vertex
// buffer) - they simply now wrap that one buffer in its own shared_ptr
// internally, which costs one extra (small, one-time) heap allocation per
// Mesh, not per frame.
class Mesh {
public:
    // Non-indexed constructor (unchanged from before this engine had index
    // buffers at all) - every PrimitiveMeshGenerator-built shape still uses
    // this.
    Mesh(Buffer vertexBuffer, std::uint32_t vertexCount)
        : m_vertexBuffer(std::make_shared<Buffer>(std::move(vertexBuffer)))
        , m_vertexCount(vertexCount)
    {
    }

    // Indexed constructor - for a mesh whose CPU-side data already came
    // with a real triangle-index list (see MeshData::indices). This Mesh
    // owns its own, PRIVATE vertex buffer (not shared with any other Mesh).
    Mesh(Buffer vertexBuffer, std::uint32_t vertexCount, Buffer indexBuffer, std::uint32_t indexCount)
        : m_vertexBuffer(std::make_shared<Buffer>(std::move(vertexBuffer)))
        , m_vertexCount(vertexCount)
        , m_indexBuffer(std::move(indexBuffer))
        , m_indexCount(indexCount)
    {
    }

    // SHARED-vertex-buffer indexed constructor - `sharedVertexBuffer` is an
    // already-constructed vertex buffer (see
    // Renderer::CreateSharedMeshVertexBuffer()/
    // CreateSharedSkinnedMeshVertexBuffer()) that some OTHER Mesh may also
    // be pointing at right now; only `indexBuffer`/`indexCount` are unique
    // to this Mesh. See this class's own header comment above for why this
    // exists.
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
    std::uint32_t VertexCount() const noexcept { return m_vertexCount; }

    // True only for a Mesh built via one of the indexed constructors above -
    // a caller (FrameRecorder::Submit()) must check this before calling
    // IndexBuffer()/IndexCount(), same "check before you index" convention
    // as MeshData::skinWeights.
    bool HasIndexBuffer() const noexcept { return m_indexBuffer.has_value(); }
    VkBuffer IndexBuffer() const noexcept { return m_indexBuffer->Native(); }
    std::uint32_t IndexCount() const noexcept { return m_indexCount; }

    // A cheap, stable identity for this Mesh's own underlying vertex
    // buffer - two Mesh instances built from the SAME shared vertex buffer
    // (see the shared constructor above) return the exact same pointer
    // value here. Used by AnimationSystem::Update() (src/Game/Animation/
    // AnimationSystem.cpp) to detect that several of a model's own
    // MeshAssetParts are backed by the same physical GPU buffer, so it
    // packs/uploads that buffer's freshly-skinned vertex data exactly ONCE
    // per frame, no matter how many material parts reference it - see
    // MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md, Stage 1. Never
    // dereferenced as a real Buffer* by callers outside this class - purely
    // an opaque identity token.
    const void* VertexBufferIdentity() const noexcept { return m_vertexBuffer.get(); }

    // Overwrites this Mesh's ENTIRE vertex buffer with `data` (`size` bytes
    // - must exactly match this Mesh's own vertex buffer size, i.e.
    // whatever vertexDataSize was originally passed to
    // Renderer::CreateSkinnedMesh()/CreateSharedSkinnedMeshVertexBuffer()) -
    // the CPU vertex-skinning update path (see
    // AnimationSystem::Update(), src/Game/Animation/AnimationSystem.cpp): a
    // rigged model's Mesh is built via CreateSkinnedMesh() (a host-visible,
    // persistently-mapped vertex buffer - unlike the immutable, device-
    // local one CreateMesh() builds) specifically so this can be called
    // once per frame with freshly bone-deformed positions/normals. Throws
    // (via Buffer::Upload()) if this Mesh's own vertex buffer isn't
    // actually CPU-writable - only call this on a Mesh built via
    // CreateSkinnedMesh()/a shared skinned vertex buffer, never one built
    // via CreateMesh(). If this Mesh shares its vertex buffer with other
    // Mesh instances (see VertexBufferIdentity() above), calling this
    // updates that SAME underlying buffer for all of them - callers should
    // therefore only call this once per distinct VertexBufferIdentity()
    // per frame, not once per Mesh/part.
    void UpdateVertexData(const void* data, std::size_t size) { m_vertexBuffer->Upload(data, size); }

private:
    std::shared_ptr<Buffer> m_vertexBuffer;
    std::uint32_t m_vertexCount = 0;

    // std::optional rather than a second always-present Buffer - Buffer has
    // no default constructor (every Buffer is a real, immediately-valid GPU
    // allocation - see Buffer.h), so "no index buffer at all" (the
    // non-indexed constructor above) has to be represented by genuine
    // absence, not a zero-sized/sentinel Buffer. The index buffer is always
    // this ONE Mesh's own private buffer - never shared, even when the
    // vertex buffer is (see this class's own header comment).
    std::optional<Buffer> m_indexBuffer;
    std::uint32_t m_indexCount = 0;
};

} // namespace gte
