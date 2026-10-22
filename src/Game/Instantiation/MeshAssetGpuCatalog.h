#pragma once

#include "../../Core/GraphicsHandles.h"

#include "../../ECS/Registry.h"
#include "EntityBlueprint.h"
#include "MaterialTextureGpuCache.h"
#include "MeshAssetPipelineKind.h"
#include "../Animation/SkeletalRigCache.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class Renderer;
class RenderSystem;
class Mesh;

// One child entity spawned for a *.gta asset.
struct MeshAssetPart {
    MeshHandle mesh;
    // kInvalidMaterialTextureHandle = untextured; otherwise the material bound by the part's pipeline.
    MaterialTextureHandle texture;
    // Originating material name; empty for the merged untextured submesh or an unnamed material.
    std::string name;
    // CPU copy of `mesh`'s triangle indices, so a GPU-skinned counterpart can own a private index buffer.
    // Empty for a boneless model.
    std::vector<std::uint32_t> indices;
    // The draw pipeline of this part.
    MeshAssetPipelineKind kind = MeshAssetPipelineKind::Untextured;
};

// Decodes an imported mesh asset once per distinct absolute *.gta path, uploads it as MeshAssetParts and
// resolves them into a ROOT+CHILDREN EntityBlueprint. Also exposes the decoded SkinnedMeshData;
// the hand-off to animation is made one level up, in MeshInstantiationSystem. Needs a live Renderer.
class MeshAssetGpuCatalog {
public:
    // Loads (cached per path) and resolves a root + per-part children blueprint. `children` is empty
    // when the file is missing, corrupt or has no geometry; callers treat that as failure.
    // `sceneServicesSetLayout` is the layout of Core's scene-services set (pipeline set 1).
    EntityBlueprint Resolve(RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath,
        DescriptorSetLayoutHandle sceneServicesSetLayout);

    // Read-only queries used by AnimationSystem (via MeshInstantiationSystem's forwarding methods).
    const std::vector<MeshAssetPart>* TryGetParts(const std::string& absoluteGtaPath) const;
    const SkinnedMeshData* TryGetSkinnedMeshData(const std::string& absoluteGtaPath) const;

    // Re-reads only the trailing jointPhysicsOverrides section of the *.gta and patches the cached
    // SkinnedMeshData in place (no mesh/GPU data touched). Returns false and changes nothing if the path
    // is not cached or the file cannot be decoded.
    bool RefreshCachedJointPhysicsOverridesFromDisk(const std::string& absoluteGtaPath);

    // Drops the cached parts/skinned data for `absoluteGtaPath` so the next Resolve() re-reads and re-uploads it.
    // Needed after in-place vertex edits (e.g. normals); already-spawned entities keep their current GPU mesh.
    void InvalidateCachedMeshAsset(const std::string& absoluteGtaPath);

    // Frees the materials (then the images) that no MeshRenderer in `registry` and no cached part uses.
    // Main thread, outside pass recording.
    MaterialReleaseResult ReleaseUnusedMaterials(Registry& registry, RenderSystem& renderSystem, Renderer& renderer);

    // Frees the meshes this catalog registered that no cached part, MeshRenderer or skinning binding uses.
    // `skinningBindings` are the mesh handles the skinning rig cache can swap to. Returns the freed handles.
    // Main thread, outside pass recording.
    std::vector<MeshHandle> ReleaseUnusedMeshes(Registry& registry, RenderSystem& renderSystem, Renderer& renderer,
        std::span<const MeshHandle> skinningBindings);

    // True while some material or mesh may be unused and a sweep should run.
    bool ReleasePending() const noexcept { return m_releasePending || m_meshReleasePending; }

private:
    PipelineHandle EnsurePipeline(MeshAssetPipelineKind kind, RenderSystem& renderSystem, Renderer& renderer,
        DescriptorSetLayoutHandle sceneServicesSetLayout);

    const std::vector<MeshAssetPart>& EnsureMeshAsset(
        RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath);

    // Registers the mesh and remembers the handle as catalog-owned.
    MeshHandle RegisterOwnedMesh(RenderSystem& renderSystem, Mesh&& mesh);

    std::array<PipelineHandle, 5> m_pipelines; // Index = MeshAssetPipelineKind.
    MaterialTextureGpuCache m_materialTextureCache;

    std::unordered_map<std::string, std::vector<MeshAssetPart>> m_meshAssetCache;
    std::unordered_map<std::string, SkinnedMeshData> m_skinnedMeshCache;

    std::vector<MeshHandle> m_ownedMeshes; // Every mesh this catalog registered and has not released.

    bool m_releasePending = false;
    bool m_meshReleasePending = false;
};

} // namespace gte
