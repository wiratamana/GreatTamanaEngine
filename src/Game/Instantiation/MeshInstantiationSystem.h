#pragma once

#include "../../ECS/Entity.h"
#include "../../ECS/Registry.h"
#include "MeshAssetGpuCatalog.h"
#include "PrimitiveGpuCatalog.h"

#include <string>

namespace gte {

class Renderer;
class RenderSystem;

// The thin orchestrator both of Game's spawn-facing public methods
// (CreatePrimitiveEntity()/CreateMeshEntityFromGtaFile()) forward into,
// structurally parallel to RenderSystem (the established "this one class is
// allowed to depend on both ECS and Renderer" seam - see AGENTS.md's
// Entity-Component-System section, which this refactor's job is to also
// name this class alongside).
//
// Owns one PrimitiveGpuCatalog and one MeshAssetGpuCatalog - each spawn
// method is exactly: resolve an EntityBlueprint via the relevant catalog,
// then call EntityInstantiator::Instantiate(). Also exposes the read-only
// queries AnimationSystem needs (forwarding to MeshAssetGpuCatalog) - see
// GameInstantiationRefactorProposal.txt, Step 3.3.
class MeshInstantiationSystem {
public:
    explicit MeshInstantiationSystem(RenderSystem& renderSystem)
        : m_renderSystem(renderSystem)
    {
    }

    // Spawns a new entity built from one of the engine's built-in primitive
    // shapes - see Game::CreatePrimitiveEntity()'s own doc comment (Game.h)
    // for the full behavior contract this preserves exactly.
    Entity SpawnPrimitive(Registry& registry, Renderer& renderer, PrimitiveType type);

    // Spawns a whole hierarchy of entities from an imported *.gta
    // AssetType::Mesh file - see Game::CreateMeshEntityFromGtaFile()'s own
    // doc comment (Game.h) for the full behavior contract this preserves
    // exactly, including the kInvalidEntity-on-failure convention.
    Entity SpawnMeshAsset(Registry& registry, Renderer& renderer, const std::string& absoluteGtaPath);

    // Read-only queries used by AnimationSystem (constructor-injected a
    // reference to this class - see AnimationSystem.h) to look up which GPU
    // mesh parts belong to a given mesh path for per-frame re-upload, and
    // whether/what skinning data a given mesh path has - both forwarded
    // straight through to MeshAssetGpuCatalog.
    const std::vector<MeshAssetPart>* TryGetMeshAssetParts(const std::string& absoluteGtaPath) const
    {
        return m_meshAssetCatalog.TryGetParts(absoluteGtaPath);
    }
    const SkinnedMeshData* TryGetSkinnedMeshData(const std::string& absoluteGtaPath) const
    {
        return m_meshAssetCatalog.TryGetSkinnedMeshData(absoluteGtaPath);
    }

private:
    RenderSystem& m_renderSystem;
    PrimitiveGpuCatalog m_primitiveCatalog;
    MeshAssetGpuCatalog m_meshAssetCatalog;
};

} // namespace gte
