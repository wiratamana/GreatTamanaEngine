#include "MeshInstantiationSystem.h"

#include "EntityInstantiator.h"

namespace gte {

Entity MeshInstantiationSystem::SpawnPrimitive(
    Registry& registry, Renderer& renderer, PrimitiveType type, VkDescriptorSetLayout sceneServicesSetLayout)
{
    const EntityBlueprint blueprint = m_primitiveCatalog.Resolve(m_renderSystem, renderer, type, sceneServicesSetLayout);
    return Instantiate(registry, blueprint);
}

Entity MeshInstantiationSystem::SpawnMeshAsset(Registry& registry, Renderer& renderer,
    const std::string& absoluteGtaPath, VkDescriptorSetLayout sceneServicesSetLayout)
{
    const EntityBlueprint blueprint =
        m_meshAssetCatalog.Resolve(m_renderSystem, renderer, absoluteGtaPath, sceneServicesSetLayout);
    if (blueprint.children.empty()) {
        // MeshAssetGpuCatalog::Resolve() returns an empty-children blueprint
        // to signal "this path didn't resolve to a valid, non-empty Mesh
        // asset" - see its own doc comment (MeshAssetGpuCatalog.h) for the
        // exact failure cases this covers.
        return kInvalidEntity;
    }
    return Instantiate(registry, blueprint);
}

} // namespace gte
