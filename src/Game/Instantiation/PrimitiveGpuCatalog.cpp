#include "PrimitiveGpuCatalog.h"

#include "../../Math/Vec3.h"
#include "../../Renderer/Culling/CullingTypes.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/Vertex.h"
#include "../RenderSystem.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace gte {

PipelineHandle PrimitiveGpuCatalog::EnsureDefaultPipeline(
    RenderSystem& renderSystem, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    if (!m_defaultPipeline.IsValid()) {
        // Shaders are compiled at build time into "<exe dir>/shaders/*.spv".
        m_defaultPipeline = renderSystem.RegisterPipeline(renderer.CreatePipeline("shaders/Triangle.vert.spv",
            "shaders/Triangle.frag.spv", VertexLayout::PositionColor, false,
            "Triangle.vert/Triangle.frag (PositionColor)", /*useInstanceBuffer=*/false, sceneServicesSetLayout));
    }
    return m_defaultPipeline;
}

MeshHandle PrimitiveGpuCatalog::EnsurePrimitiveMesh(RenderSystem& renderSystem, Renderer& renderer, PrimitiveType type)
{
    MeshHandle& cached = m_primitiveMeshes[static_cast<std::size_t>(type)];
    if (!cached.IsValid()) {
        const std::vector<Vertex> vertices = PrimitiveMeshGenerator::Generate(type);
        Mesh mesh = renderer.CreateMesh(vertices.data(),
            vertices.size() * sizeof(Vertex), static_cast<std::uint32_t>(vertices.size()), ToString(type));

        std::vector<Vec3> positions;
        positions.reserve(vertices.size());
        for (const Vertex& v : vertices) {
            positions.push_back(Vec3{ v.position[0], v.position[1], v.position[2] });
        }
        mesh.SetLocalBounds(ComputeLocalAABB(positions));

        cached = renderSystem.RegisterMesh(std::move(mesh));
    }
    return cached;
}

EntityBlueprint PrimitiveGpuCatalog::Resolve(
    RenderSystem& renderSystem, Renderer& renderer, PrimitiveType type, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    EntityBlueprint blueprint;
    blueprint.pipeline = EnsureDefaultPipeline(renderSystem, renderer, sceneServicesSetLayout);
    blueprint.mesh = EnsurePrimitiveMesh(renderSystem, renderer, type);
    blueprint.primitiveSourceType = type;
    return blueprint;
}

} // namespace gte
