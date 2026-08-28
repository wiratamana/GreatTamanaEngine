#pragma once

#include "../../ECS/Entity.h"
#include "../../ECS/Registry.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/MeshVertex.h"
#include "AnimationClipCache.h"
#include "GpuSkinningRigCache.h"
#include "ResolvedAnimationBindingCache.h"
#include "SkeletalRigCache.h"

#include <volk.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class Renderer;
class RenderSystem;
class MeshInstantiationSystem;
struct MeshAssetPart;

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

    // GPU Vertex Skinning campaign, Phase 5 (Runtime CPU/GPU Switch - see
    // task_manager/gpu_skinning/GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md).
    // CpuJobSystem (the default) is today's exact, unmodified behavior -
    // Job-System-dispatched CPU vertex skinning (see Update() below).
    // GpuCompute instead uploads this frame's bone matrices into each
    // playing model's GpuSkinningRigCache::GpuModelEntry::boneMatricesBuffer
    // and swaps every affected MeshRenderer onto its GPU-skinned Mesh
    // counterpart - the actual compute dispatch is issued later, by
    // whoever calls CollectModelsNeedingGpuSkinningThisFrame() (see
    // src/Application/RenderPasses.cpp's AddGpuSkinningPasses()), since
    // only the render-graph-integrated caller knows where in the frame a
    // real vkCmdDispatch may be recorded.
    enum class SkinningMode : std::uint8_t {
        CpuJobSystem,
        GpuCompute,
    };

    void SetSkinningMode(SkinningMode mode) noexcept { m_mode = mode; }
    SkinningMode GetSkinningMode() const noexcept { return m_mode; }

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

    // GPU Vertex Skinning campaign, Phase 4 (Per-Model Resource Management -
    // see task_manager/gpu_skinning/GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md).
    // The GPU-side sibling of RegisterSkinnedMesh() above - called from the
    // SAME Game::CreateMeshEntityFromGtaFile() hand-off site, ALONGSIDE
    // (never instead of) RegisterSkinnedMesh(), unconditionally for a
    // rigged model - see GpuSkinningRigCache::Register()'s own doc comment
    // for why this must never become a lazy/on-first-use registration.
    // `parts` is the same MeshAssetPart list MeshInstantiationSystem::
    // TryGetMeshAssetParts() already exposes. A no-op for a boneless/
    // riggless model, or one whose parts don't resolve to any live Mesh -
    // see GpuSkinningRigCache::Register()'s own "degrade gracefully"
    // convention.
    void RegisterGpuSkinnedMesh(Renderer& renderer, const std::string& absoluteGtaPath, const SkinnedMeshData& data,
        const std::vector<MeshAssetPart>& parts)
    {
        m_gpuSkinningPipelines.EnsureInitialized(renderer);
        m_gpuRigCache.Register(renderer, m_renderSystem, m_gpuSkinningPipelines, absoluteGtaPath, data, parts);
    }

    // Phase 5 - direct access to the shared compute-pipeline pair, needed
    // by AddGpuSkinningPasses() (src/Application/RenderPasses.cpp) to
    // actually bind/dispatch whichever of the two (untextured vs. textured)
    // kernels a given CollectModelsNeedingGpuSkinningThisFrame() request
    // needs.
    GpuSkinningPipelines& GetGpuSkinningPipelines() noexcept { return m_gpuSkinningPipelines; }

    // Mirrors Game::PlayAnimationOnEntity() exactly - same validation rules
    // (entity alive, has MeshAssetSource, its mesh has non-empty skinning
    // data in SkeletalRigCache, the animation clip resolves) and same
    // "false/no component change, never throws" failure contract.
    bool Play(Registry& registry, Entity targetEntity, const std::string& absoluteAnimationGtaPath);

    // Mirrors Game::UpdateSkeletalAnimators() exactly - iterates every live
    // SkeletalAnimator, advances frame/loop, looks up skin data + clip +
    // resolved binding via the three owned caches, calls the existing,
    // unchanged Animation/AnimationPoseEvaluator.h
    // (EvaluateAnimatedSkinningPose()), and then branches on GetSkinningMode():
    // CpuJobSystem runs Animation/VertexSkinning.h (SkinVertexRange()) -
    // dispatched across the worker pool for a big-enough model, unchanged
    // from before Phase 5 existed - and re-uploads every affected mesh
    // part's GPU vertex buffer via Mesh::UpdateVertexData(); GpuCompute
    // instead uploads this frame's bone matrices straight into the model's
    // GpuSkinningRigCache::GpuModelEntry::boneMatricesBuffer (its entire
    // per-frame CPU cost) and records the model as needing a compute
    // dispatch this frame (see CollectModelsNeedingGpuSkinningThisFrame()
    // below). Either way, every affected MeshRenderer is kept in sync with
    // the CURRENT mode (swapped onto its GPU-skinned or CPU-mode Mesh
    // counterpart, per GpuSkinningRigCache::GpuModelEntry::
    // TryGetGpuMeshHandle()/TryGetCpuMeshHandle()) every frame, so a
    // mid-session mode switch takes effect the very next frame with no
    // further caller action needed.
    void Update(Registry& registry, double deltaSeconds);

    // GPU Vertex Skinning campaign, Phase 5, Step 3.3 ("Who actually issues
    // the vkCmdDispatch?") - called from src/Application/RenderPasses.cpp's
    // AddGpuSkinningPasses(), AFTER this frame's Update() has already run
    // (and therefore already uploaded fresh bone matrices for every
    // GPU-mode-animated model) and BEFORE the offscreen render graph's
    // `build` lambda finishes declaring passes. Returns exactly one
    // GpuSkinningDispatchRequest per distinct model+OutputGroup that
    // genuinely needs a fresh compute dispatch this frame - deduplicated by
    // model path (see Update()'s own m_gpuModelsNeedingDispatchThisFrame),
    // so a caller never has to apply Phase 3's own read-before-write WAW
    // mitigation itself: this list never asks for the SAME output buffer to
    // be written twice in one frame. Empty whenever GetSkinningMode() ==
    // CpuJobSystem, or no rigged model happens to be animating at all this
    // frame.
    struct GpuSkinningDispatchRequest {
        // A stable, persistent (never a per-frame temporary) name - see
        // GpuSkinningRigCache::OutputGroup::debugName's own doc comment for
        // why this MUST NOT be a freshly-built std::string each frame.
        // Suitable to pass directly as RenderGraphBuilder::AddPass()/
        // ImportBuffer()'s own `name` parameter.
        const char* name = nullptr;

        VkBuffer outputBuffer = VK_NULL_HANDLE;
        VkDeviceSize outputBufferSize = 0;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        std::uint32_t vertexCount = 0;
        // Selects which of GetGpuSkinningPipelines()'s two pipelines
        // (PositionNormal vs. PositionNormalUv) this request's descriptor
        // set was built against.
        bool textured = false;
    };
    std::vector<GpuSkinningDispatchRequest> CollectModelsNeedingGpuSkinningThisFrame() const;

private:
    RenderSystem& m_renderSystem;
    MeshInstantiationSystem& m_meshInstantiationSystem;

    SkeletalRigCache m_rigCache;
    AnimationClipCache m_clipCache;
    ResolvedAnimationBindingCache m_bindingCache;

    // GPU Vertex Skinning campaign, Phase 4 - the GPU-side siblings of
    // m_rigCache above. m_gpuSkinningPipelines is shared across every
    // registered model (its two compute pipelines are built once, lazily,
    // on the very first RegisterGpuSkinnedMesh() call - see
    // GpuSkinningPipelines::EnsureInitialized()); m_gpuRigCache owns every
    // GPU buffer/descriptor-set/Mesh Phase 5's runtime CPU/GPU switch
    // actually dispatches a skinning compute pass against and switches a
    // MeshRenderer onto.
    GpuSkinningPipelines m_gpuSkinningPipelines;
    GpuSkinningRigCache m_gpuRigCache;

    // Phase 5 - the runtime switch itself. Snapshotted ONCE at the top of
    // every Update() call (see this phase's own strategy document, Step
    // 3.4, rule 1) so one model's entire per-frame processing is never torn
    // between two different modes mid-iteration even if a UI toggle flips
    // it concurrently (this engine is single-threaded, but this discipline
    // costs nothing and removes any doubt).
    SkinningMode m_mode = SkinningMode::CpuJobSystem;

    // Phase 5 - the distinct mesh *.gta paths that needed a fresh GPU
    // skinning compute dispatch THIS frame (GpuCompute mode only),
    // rebuilt from scratch at the top of every Update() call. Read back by
    // CollectModelsNeedingGpuSkinningThisFrame() above - kept as a member
    // (rather than returned directly from Update()) since Update() and
    // CollectModelsNeedingGpuSkinningThisFrame() are necessarily two
    // separate calls from two different call sites (Game::Update() vs.
    // src/Application/RenderPasses.cpp), per this phase's own strategy
    // document, Step 3.3.
    std::vector<std::string> m_gpuModelsNeedingDispatchThisFrame;

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
