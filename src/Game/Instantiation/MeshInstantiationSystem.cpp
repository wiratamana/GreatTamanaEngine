#include "MeshInstantiationSystem.h"

#include "EntityInstantiator.h"

namespace gte {

Entity MeshInstantiationSystem::SpawnPrimitive(
    Registry& registry, Renderer& renderer, PrimitiveType type, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    const EntityBlueprint blueprint = m_primitiveCatalog.Resolve(m_renderSystem, renderer, type, sceneServicesSetLayout);
    return Instantiate(registry, blueprint);
}

Entity MeshInstantiationSystem::SpawnMeshAsset(Registry& registry, Renderer& renderer,
    const std::string& absoluteGtaPath, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    const EntityBlueprint blueprint =
        m_meshAssetCatalog.Resolve(m_renderSystem, renderer, absoluteGtaPath, sceneServicesSetLayout);
    if (blueprint.children.empty()) {
        // An empty-children blueprint means the path did not resolve to a valid, non-empty mesh asset.
        return kInvalidEntity;
    }
    return Instantiate(registry, blueprint);
}

} // namespace gte
