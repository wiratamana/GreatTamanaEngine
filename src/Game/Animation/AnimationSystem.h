#pragma once

#include "../../ECS/Entity.h"
#include "../../ECS/Registry.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/MeshVertex.h"
#include "AnimationClipCache.h"
#include "ResolvedAnimationBindingCache.h"
#include "SkeletalRigCache.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class RenderSystem;
class MeshInstantiationSystem;

// The main animation orchestrator - promotes animation to a first-class,
// dedicated system (see GameInstantiationRefactorProposal.txt, Step 3.5),
// replacing Game::PlayAnimationOnEntity()/UpdateSkeletalAnimators()'s
// implementation plus the three private caches those methods used to share
// as anonymous Game member variables (m_meshSkinningCache/
// m_animationClipCache/m_resolvedAnimationBindingCache).
//
// Owns one AnimationClipCache, one ResolvedAnimationBindingCache, and one
// SkeletalRigCache. Depends on (by reference, not ownership) RenderSystem
// (to call TryGetMesh()/Mesh::UpdateVertexData() for re-upload) and
// MeshInstantiationSystem (read-only, to look up which GPU mesh parts
// belong to a given mesh path for re-upload - replacing the old direct
// Game::m_meshAssetCache reach-through).
class AnimationSystem {
public:
    AnimationSystem(RenderSystem& renderSystem, MeshInstantiationSystem& meshInstantiationSystem)
        : m_renderSystem(renderSystem)
        , m_meshInstantiationSystem(meshInstantiationSystem)
    {
    }

    // The explicit hand-off entry point that replaces the old hidden
    // "EnsureMeshAsset() writes m_meshSkinningCache, UpdateSkeletalAnimators()
    // reads it" coupling (see GameInstantiationRefactorProposal.txt, Step
    // 2.4) - called by Game right after MeshInstantiationSystem::SpawnMeshAsset()
    // succeeds for a model MeshInstantiationSystem::TryGetSkinnedMeshData()
    // reports as skinned. A real function call at a real call site, not an
    // implicit fact discoverable only by reading two functions' bodies.
    void RegisterSkinnedMesh(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
    {
        m_rigCache.Register(absoluteGtaPath, data);
    }

    // Mirrors Game::PlayAnimationOnEntity() exactly - same validation rules
    // (entity alive, has MeshAssetSource, its mesh has non-empty skinning
    // data in SkeletalRigCache, the animation clip resolves) and same
    // "false/no component change, never throws" failure contract.
    bool Play(Registry& registry, Entity targetEntity, const std::string& absoluteAnimationGtaPath);

    // Mirrors Game::UpdateSkeletalAnimators() exactly - iterates every live
    // SkeletalAnimator, advances frame/loop, looks up skin data + clip +
    // resolved binding via the three owned caches, calls the existing,
    // unchanged Animation/AnimationPoseEvaluator.h
    // (EvaluateAnimatedSkinningPose()) and Animation/VertexSkinning.h
    // (SkinVertexRange()), packs the result via the SHARED MeshVertexPacking
    // helpers (instead of duplicated inline loops), and re-uploads every
    // affected mesh part's GPU buffer - see this class's own .cpp file for
    // the multithreaded CPU-skinning optimization applied here (Stage 1's
    // shared-vertex-buffer de-duplication, Stage 2's parallelized packing,
    // and Stage 3's per-model scratch-buffer reuse - see
    // task_manager/optimizing_multi_thread_cpu_skinning/
    // MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md).
    void Update(Registry& registry, double deltaSeconds);

private:
    RenderSystem& m_renderSystem;
    MeshInstantiationSystem& m_meshInstantiationSystem;

    SkeletalRigCache m_rigCache;
    AnimationClipCache m_clipCache;
    ResolvedAnimationBindingCache m_bindingCache;

    // Stage 3 (stop re-allocating scratch buffers every frame - see
    // MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md): one model's
    // own skinned-position/normal output arrays plus its packed-vertex
    // scratch arrays, OWNED here (per distinct mesh *.gta path) instead of
    // being freshly heap-allocated on every single Update() call, for
    // every animator, every frame. A std::vector's own resize() is a no-op
    // (no reallocation) once its capacity already covers the requested
    // size, so after the first frame these buffers never allocate again
    // for as long as a given model's own vertex count stays constant
    // (which it always does after initial load).
    struct AnimatorScratchBuffers {
        std::vector<Vec3> skinnedPositions;
        std::vector<Vec3> skinnedNormals;
        std::vector<MeshVertex> packedUntextured;
        std::vector<MeshVertexUv> packedTextured;
    };
    std::unordered_map<std::string, AnimatorScratchBuffers> m_scratchBuffers;
};

} // namespace gte
