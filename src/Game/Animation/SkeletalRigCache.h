#pragma once

#include "../../Assets/MeshData.h"
#include "../../Assets/PhysicsData.h"
#include "../../Assets/SkeletonData.h"
#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

// The CPU-side "bind pose + rig" data for one skinned model - re-homed here,
// ANIMATION-owned (see GameInstantiationRefactorProposal.txt, Step 3.5),
// rather than a Game-owned m_meshSkinningCache member the way it used to be:
// its only real consumer is animation (AnimationSystem.h) - mesh LOADING
// (MeshAssetGpuCatalog.h) merely produces it as a side effect of decoding a
// rigged *.gta. `uvs` mirrors bindPositions.size() (zero-filled where the
// source mesh has none) purely so the same skinned output can be
// reformatted into either MeshVertex (untextured parts) or MeshVertexUv
// (textured parts) with no extra branching at update time - see
// MeshVertexPacking.h. `physics` (PHASE4,
// task_manager/verlet-integration-1/PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md)
// is the model's own imported PMX RigidBody/Joint data - std::nullopt for a
// model imported before this engine extracted physics data, or a
// physics-data-less .pmx - PhysicsSystem::RegisterDynamicChains() treats a
// nullopt exactly like an empty PhysicsData (pure defaults, no per-bone
// RigidBody seeding), never a failure.
struct SkinnedMeshData {
    std::vector<Vec3> bindPositions;
    std::vector<Vec3> bindNormals;
    std::vector<Vec2> uvs;
    std::vector<VertexSkinWeights> skinWeights;
    SkeletonData skeleton;
    std::optional<PhysicsData> physics;
};

// Replaces the old Game::m_meshSkinningCache, but re-homed as an explicit,
// ANIMATION-owned cache: populated via an EXPLICIT wiring call (Register())
// right after MeshInstantiationSystem::SpawnMeshAsset() reports a freshly
// loaded model is skinned (see MeshAssetGpuCatalog::TryGetSkinnedMeshData())
// - never populated implicitly/silently the way the old shared-private-
// member coupling between mesh loading and animation used to work (see
// GameInstantiationRefactorProposal.txt, Step 2.4). Keyed by the mesh's
// absolute *.gta path, same convention as before.
class SkeletalRigCache {
public:
    void Register(const std::string& absoluteGtaPath, SkinnedMeshData data)
    {
        m_cache.insert_or_assign(absoluteGtaPath, std::move(data));
    }

    const SkinnedMeshData* TryGet(const std::string& absoluteGtaPath) const
    {
        const auto found = m_cache.find(absoluteGtaPath);
        return found != m_cache.end() ? &found->second : nullptr;
    }

private:
    std::unordered_map<std::string, SkinnedMeshData> m_cache;
};

} // namespace gte
