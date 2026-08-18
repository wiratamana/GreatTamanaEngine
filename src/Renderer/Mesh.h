#pragma once

#include "Buffer.h"

#include <cstdint>
#include <utility>

namespace gte {

// The engine's minimal "geometry" primitive: a GPU-resident vertex buffer
// plus how many vertices it holds. Construct via Renderer::CreateMesh()
// (see Renderer.h) rather than directly - same convention as Buffer/
// RenderTexture/Pipeline. No index buffer support yet - added once
// something actually needs one; the engine's first mesh is a single
// hardcoded triangle, drawn with a plain (non-indexed) vkCmdDraw.
class Mesh {
public:
    Mesh(Buffer vertexBuffer, std::uint32_t vertexCount) noexcept
        : m_vertexBuffer(std::move(vertexBuffer))
        , m_vertexCount(vertexCount)
    {
    }

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&&) noexcept = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    VkBuffer VertexBuffer() const noexcept { return m_vertexBuffer.Native(); }
    std::uint32_t VertexCount() const noexcept { return m_vertexCount; }

private:
    Buffer m_vertexBuffer;
    std::uint32_t m_vertexCount = 0;
};

} // namespace gte
