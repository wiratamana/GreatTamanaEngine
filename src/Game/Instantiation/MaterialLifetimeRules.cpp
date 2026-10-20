#include "MaterialLifetimeRules.h"

#include "../../ECS/Components/MeshRenderer.h"

namespace gte {

std::unordered_set<MaterialTextureHandle> CollectMaterialTexturesInUse(Registry& registry)
{
    std::unordered_set<MaterialTextureHandle> inUse;
    ComponentStorage<MeshRenderer>& renderers = registry.Storage<MeshRenderer>();
    for (std::size_t i = 0; i < renderers.Size(); ++i) {
        const MaterialTextureHandle handle = renderers.ComponentAt(i).texture;
        if (handle.IsValid()) {
            inUse.insert(handle);
        }
    }
    return inUse;
}

std::vector<MaterialTextureHandle> SelectUnusedMaterials(
    std::span<const MaterialTextureHandle> cached, const std::unordered_set<MaterialTextureHandle>& inUse)
{
    std::vector<MaterialTextureHandle> unused;
    for (const MaterialTextureHandle handle : cached) {
        if (!inUse.contains(handle)) {
            unused.push_back(handle);
        }
    }
    return unused;
}

} // namespace gte
