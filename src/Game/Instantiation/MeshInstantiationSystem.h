#pragma once

#include "../../Core/GraphicsHandles.h"

#include "../../ECS/Entity.h"
#include "../../ECS/Registry.h"
#include "MeshAssetGpuCatalog.h"
#include "PrimitiveGpuCatalog.h"

#include <cstddef>
#include <string>

namespace gte {

class Renderer;
class RenderSystem;

// Spawn orchestrator: resolves a blueprint through the matching catalog, then instantiates it.
class MeshInstantiationSystem {
public:
    explicit MeshInstantiationSystem(RenderSystem& renderSystem)
        : m_renderSystem(renderSystem)
    {
    }

    // Spawns an entity from a built-in primitive shape.
    // `sceneServicesSetLayout` is the layout of Core's scene-services set (pipeline set 1).
    Entity SpawnPrimitive(
        Registry& registry, Renderer& renderer, PrimitiveType type, DescriptorSetLayoutHandle sceneServicesSetLayout);

    // Spawns an entity hierarchy from a *.gta mesh asset. Returns kInvalidEntity on failure.
    Entity SpawnMeshAsset(Registry& registry, Renderer& renderer, const std::string& absoluteGtaPath,
        DescriptorSetLayoutHandle sceneServicesSetLayout);

    // Read-only catalog queries used by AnimationSystem for per-frame re-upload.
    const std::vector<MeshAssetPart>* TryGetMeshAssetParts(const std::string& absoluteGtaPath) const
    {
        return m_meshAssetCatalog.TryGetParts(absoluteGtaPath);
    }
    const SkinnedMeshData* TryGetSkinnedMeshData(const std::string& absoluteGtaPath) const
    {
        return m_meshAssetCatalog.TryGetSkinnedMeshData(absoluteGtaPath);
    }

    // Forwards to the catalog; patches the cached joint physics overrides from disk.
    bool RefreshCachedJointPhysicsOverridesFromDisk(const std::string& absoluteGtaPath)
    {
        return m_meshAssetCatalog.RefreshCachedJointPhysicsOverridesFromDisk(absoluteGtaPath);
    }

    // Forwards to the catalog; drops the cached GPU mesh so the next spawn re-uploads it.
    void InvalidateCachedMeshAsset(const std::string& absoluteGtaPath)
    {
        m_meshAssetCatalog.InvalidateCachedMeshAsset(absoluteGtaPath);
    }

    // True while the catalog has materials that may be unused.
    bool MaterialReleasePending() const noexcept { return m_meshAssetCatalog.ReleasePending(); }

    // Frees materials and images that no entity and no cached asset uses; returns the material count freed.
    std::size_t ReleaseUnusedMaterials(Registry& registry, Renderer& renderer)
    {
        const MaterialReleaseResult result = m_meshAssetCatalog.ReleaseUnusedMaterials(registry, m_renderSystem, renderer);
        return result.materialsFreed;
    }

private:
    RenderSystem& m_renderSystem;
    PrimitiveGpuCatalog m_primitiveCatalog;
    MeshAssetGpuCatalog m_meshAssetCatalog;
};

} // namespace gte
