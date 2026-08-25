#include "PrimitiveGpuCatalog.h"

#include "../../Renderer/Renderer.h"
#include "../../Renderer/Vertex.h"
#include "../RenderSystem.h"

#include <cstddef>

namespace gte {

PipelineHandle PrimitiveGpuCatalog::EnsureDefaultPipeline(RenderSystem& renderSystem, Renderer& renderer)
{
    if (!m_defaultPipeline.IsValid()) {
        // Shader source lives at src/Shaders/Triangle.vert/.frag (version-
        // controlled); compiled to SPIR-V at build time by
        // cmake/CompileShaders.cmake into "<exe dir>/shaders/*.spv".
        m_defaultPipeline = renderSystem.RegisterPipeline(
            renderer.CreatePipeline("shaders/Triangle.vert.spv", "shaders/Triangle.frag.spv"));
    }
    return m_defaultPipeline;
}

MeshHandle PrimitiveGpuCatalog::EnsurePrimitiveMesh(RenderSystem& renderSystem, Renderer& renderer, PrimitiveType type)
{
    MeshHandle& cached = m_primitiveMeshes[static_cast<std::size_t>(type)];
    if (!cached.IsValid()) {
        const std::vector<Vertex> vertices = PrimitiveMeshGenerator::Generate(type);
        cached = renderSystem.RegisterMesh(renderer.CreateMesh(vertices.data(),
            vertices.size() * sizeof(Vertex), static_cast<std::uint32_t>(vertices.size()), ToString(type)));
    }
    return cached;
}

EntityBlueprint PrimitiveGpuCatalog::Resolve(RenderSystem& renderSystem, Renderer& renderer, PrimitiveType type)
{
    EntityBlueprint blueprint;
    blueprint.pipeline = EnsureDefaultPipeline(renderSystem, renderer);
    blueprint.mesh = EnsurePrimitiveMesh(renderSystem, renderer, type);
    return blueprint;
}

} // namespace gte
