#pragma once

#include "EntityBlueprint.h"
#include "MaterialTextureGpuCache.h"
#include "../Animation/SkeletalRigCache.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class Renderer;
class RenderSystem;

// One entity CreateMeshEntityFromGtaFile()'s blueprint should spawn (as a
// CHILD of that call's own root node) for a given *.gta asset - see
// EnsureMeshAsset() below. Kept here (rather than nested private inside a
// class) since both this catalog AND MeshInstantiationSystem's read-only
// forwarding query for AnimationSystem's per-frame re-upload need the shape.
struct MeshAssetPart {
    MeshHandle mesh;
    // kInvalidTextureHandle means "untextured - draw via the untextured
    // Mesh.vert/.frag pipeline"; otherwise draw via the textured
    // TexturedMesh.vert/.frag pipeline with this texture bound.
    TextureHandle texture;
    // The originating PMX material's own name (Material::name - see
    // MaterialData.h), or empty when this part has no single originating
    // material with a usable name (the combined untextured submesh, which
    // may merge more than one material together, or a material whose own
    // name PMX left blank).
    std::string name;
};

// Replaces Game::EnsureMeshAsset()/EnsureMeshPipeline()/
// EnsureTexturedMeshPipeline() - decodes (once per distinct absolute *.gta
// path, then cached) an imported Mesh asset into GPU MeshAssetParts, and
// resolves them into a ready-to-instantiate ROOT+CHILDREN EntityBlueprint -
// see GameInstantiationRefactorProposal.txt, Step 3.3.
//
// Internally, "load a mesh asset" is now a clean, named SEQUENCE of steps
// instead of one 200-line function: decode *.gta + rig/material metadata ->
// MeshMaterialPartitioner (pure) -> resolve each textured slice's texture
// via MaterialTextureGpuCache (impure) -> MeshVertexPacking (pure, shared
// with the per-frame re-upload path in AnimationSystem) to build the CPU
// vertex arrays -> upload each submesh via Renderer::CreateMesh()/
// CreateSkinnedMesh() -> cache the resulting MeshAssetPart list.
//
// Also exposes whatever SkinnedMeshData it decoded for a given path (for a
// skinned model) so animation wiring can pick it up - it does NOT push this
// into any animation-owned cache itself (see SkeletalRigCache.h); that
// hand-off is made explicit one level up, in Game/MeshInstantiationSystem.
//
// Genuinely needs a live Renderer to do its real job - "Tier 2, no
// automated coverage yet" (see AGENTS.md) - the PURE parts it depends on
// (MeshMaterialPartitioner, MeshVertexPacking, EntityInstantiator) are what
// are actually tested.
class MeshAssetGpuCatalog {
public:
    // Loads (once per distinct `absoluteGtaPath`, then cached) and resolves
    // a ready-to-instantiate ROOT+CHILDREN EntityBlueprint: root node named
    // after the file stem, tagged with the source path (meshAssetSourcePath)
    // - no MeshRenderer of its own; one child node per MeshAssetPart, named
    // after its originating material where applicable. Returns a blueprint
    // whose `children` is EMPTY (and which callers should therefore treat as
    // a failure, never instantiating it) for anything that doesn't resolve
    // to a valid, non-empty *.gta AssetType::Mesh file - missing file, bad
    // magic, corrupt/truncated payload, or zero vertices/triangles.
    EntityBlueprint Resolve(RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath);

    // Read-only queries used by AnimationSystem (via MeshInstantiationSystem's
    // own forwarding methods) - see SkeletalRigCache.h for why the
    // SkinnedMeshData hand-off itself is a separate, explicit step rather
    // than living here.
    const std::vector<MeshAssetPart>* TryGetParts(const std::string& absoluteGtaPath) const;
    const SkinnedMeshData* TryGetSkinnedMeshData(const std::string& absoluteGtaPath) const;

private:
    PipelineHandle EnsureMeshPipeline(RenderSystem& renderSystem, Renderer& renderer);
    PipelineHandle EnsureTexturedMeshPipeline(RenderSystem& renderSystem, Renderer& renderer);

    const std::vector<MeshAssetPart>& EnsureMeshAsset(
        RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath);

    PipelineHandle m_meshPipeline;
    PipelineHandle m_texturedMeshPipeline;
    MaterialTextureGpuCache m_materialTextureCache;

    std::unordered_map<std::string, std::vector<MeshAssetPart>> m_meshAssetCache;
    std::unordered_map<std::string, SkinnedMeshData> m_skinnedMeshCache;
};

} // namespace gte
