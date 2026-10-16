#pragma once

#include "../../Core/GraphicsHandles.h"
#include "EntityBlueprint.h"
#include "Renderer/Primitives/PrimitiveMeshGenerator.h"

#include <array>

namespace gte {

class Renderer;
class RenderSystem;

// Owns the shared default pipeline and one lazily uploaded mesh per primitive type.
class PrimitiveGpuCatalog {
public:
    // Returns a one-node blueprint for `type`, creating its pipeline and mesh on first use.
    // `sceneServicesSetLayout` is the layout of Core's scene-services set (pipeline set 1).
    EntityBlueprint Resolve(
        RenderSystem& renderSystem, Renderer& renderer, PrimitiveType type, DescriptorSetLayoutHandle sceneServicesSetLayout);

private:
    PipelineHandle EnsureDefaultPipeline(RenderSystem& renderSystem, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout);
    MeshHandle EnsurePrimitiveMesh(RenderSystem& renderSystem, Renderer& renderer, PrimitiveType type);

    PipelineHandle m_defaultPipeline;
    // Indexed by static_cast<std::size_t>(PrimitiveType) - kInvalidMeshHandle
    // (the array's default-constructed value) means "not generated yet".
    std::array<MeshHandle, 5> m_primitiveMeshes;
};

} // namespace gte
