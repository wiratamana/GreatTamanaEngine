#pragma once

#include "../../Renderer/Buffer.h"
#include "../../Renderer/ComputeDescriptorSet.h"
#include "../../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../../Renderer/MeshHandle.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class Renderer;
class RenderSystem;
struct SkinnedMeshData;
struct MeshAssetPart;

// ============================================================================
// GPU Vertex Skinning campaign - Phase 4: Per-Model Resource Management.
// ============================================================================
// See task_manager/gpu_skinning/GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md
// for the full design. The GPU-side sibling of SkeletalRigCache
// (SkeletalRigCache.h) - owns every GPU buffer/descriptor-set/Mesh a
// GPU-skinned model needs, mirroring SkeletalRigCache's own
// "Register() once, TryGet() every frame after" shape exactly, and the
// exact same "load once, cache, never mutate/evict again for the process's
// lifetime" convention every cache in this module already follows.
//
// Owned by AnimationSystem (see AnimationSystem.h's own m_gpuRigCache
// member) alongside a single, shared GpuSkinningPipelines instance - this
// class never builds its own compute pipelines, it only ever consumes an
// already-`EnsureInitialized()`d one handed to it by Register()'s caller.
//
// Registered ONCE per model, up front, from the SAME explicit hand-off call
// site the CPU path's SkeletalRigCache::Register() already uses
// (Game::CreateMeshEntityFromGtaFile(), via
// AnimationSystem::RegisterGpuSkinnedMesh()) - unconditionally, regardless
// of which skinning mode (CPU/GPU) is currently active, so a future runtime
// CPU/GPU switch (Phase 5 - not part of this phase) never needs a lazy
// "oh, I need to register this now" fallback path. A model registered here
// pays real, one-time GPU memory/setup cost the moment it's spawned - see
// this phase's own strategy document, Step 4, for why this is a deliberate
// trade against lazy/deferred registration.
//
// GPU-only, compute-write-only output buffers/descriptor sets/Mesh objects
// built here are NOT wired into any runtime CPU/GPU switch yet - that is
// explicitly Phase 5's job (GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md).
// This phase's own deliverable stops at "the per-model GPU resources exist,
// are correctly laid out/bound, and are ready for Phase 5 to actually
// dispatch a compute pass against and swap a MeshRenderer's MeshHandle
// onto" - see TryGet()/GpuModelEntry below.
class GpuSkinningRigCache {
public:
    // One shared GPU skinning-output buffer/descriptor-set/Mesh set for a
    // group of MeshAssetParts that all share ONE underlying CPU-side
    // vertex buffer identity (see
    // src/Game/Instantiation/MeshAssetPartGrouping.h's
    // GroupMeshAssetPartsBySharedVertexBuffer() - the exact same
    // de-duplication the CPU path's own AnimationSystem::Update() already
    // performs, now shared rather than duplicated).
    struct OutputGroup {
        // The GPU-only (BufferMemoryUsage::GpuOnly), compute-write-and-
        // vertex-input-readable output buffer this group's compute
        // dispatch (Phase 3/5) writes into and this group's own GPU-skinned
        // Mesh(es) below read from as their vertex buffer - see
        // Renderer::CreateGpuSkinningTargetBuffer(). Shared (std::shared_ptr)
        // across every GPU-skinned Mesh built from it below, mirroring
        // Renderer::CreateMeshFromSharedVertexBuffer()'s own existing
        // shared-vertex-buffer convention exactly.
        std::shared_ptr<Buffer> outputVertexBuffer;

        // Bound, once, at registration time, against this model's shared
        // bind-pose/skin-weights/bone-matrices buffers plus THIS group's
        // own outputVertexBuffer (+ this model's shared UV buffer, only for
        // a textured group) - per Phase 1's binding table
        // (src/Renderer/GpuSkinning/GpuSkinningTypes.h). Deliberately
        // never re-`Rewrite()`d after this - every one of these buffers has
        // a permanently stable identity for the model's entire lifetime
        // (see this phase's own strategy document, Step 3.4), unlike the
        // general ComputeDescriptorSet convention of "safe/expected to call
        // every frame" for a resource whose physical identity may change
        // frame to frame.
        ComputeDescriptorSet descriptorSet;

        // Whether this group's own output buffer uses the
        // GpuSkinnedVertexPositionNormalUv layout (true) or the
        // GpuSkinnedVertexPositionNormal layout (false) - see
        // src/Renderer/GpuSkinning/GpuSkinningTypes.h.
        bool isTextured = false;

        // Always the FULL model vertex count (skinData.bindPositions.size())
        // - every group's own output buffer covers every vertex, exactly
        // mirroring how MeshAssetGpuCatalog.cpp's CPU-side packing already
        // always packs the whole model's vertex array regardless of which
        // material subset a given part actually draws (only its INDEX
        // buffer/range differs per part).
        std::uint32_t vertexCount = 0;

        // Maps each CPU-mode MeshAssetPart's own MeshHandle (the ORIGINAL,
        // CPU-skinned/static Mesh - see MeshAssetGpuCatalog.cpp) to its
        // GPU-skinned counterpart, built here from THIS group's own shared
        // outputVertexBuffer plus that same part's own index data (see
        // MeshAssetPart::indices) - a future Phase 5 runtime switch swaps a
        // MeshRenderer's MeshHandle between the two, per this mapping, with
        // no further GPU work needed at switch time.
        struct PartMeshBinding {
            MeshHandle cpuMeshHandle;
            MeshHandle gpuMeshHandle;
        };
        std::vector<PartMeshBinding> partMeshBindings;
    };

    // The full set of GPU resources for one registered model.
    struct GpuModelEntry {
        // Immutable, uploaded exactly once, at registration time - never
        // touched again for the model's whole lifetime.
        Buffer bindPoseBuffer;
        Buffer skinWeightsBuffer;
        // Only present when at least one OutputGroup below is textured -
        // an untextured-only model never needs a UV buffer at all (see
        // src/Shaders/SkinVerticesPositionNormalUv.comp's own binding-4
        // addition, Phase 2 of this campaign).
        std::optional<Buffer> uvBuffer;

        // The ONE genuinely per-frame-rewritten input - see this phase's
        // own strategy document, Step 3.3. Sized once, here, to this
        // model's own bone count; a future Phase 5 uploads fresh skinning
        // matrices into it every frame this model is animated in GPU mode.
        // Left at whatever undefined contents CreateStructuredBuffer()
        // leaves it in until Phase 5's first real upload - never read by
        // anything until then.
        Buffer boneMatricesBuffer;

        std::vector<OutputGroup> outputGroups;

        // Best-effort lookup used by a future Phase 5: returns the
        // GPU-skinned counterpart of `cpuMeshHandle` if this model has one
        // registered, or kInvalidMeshHandle otherwise (e.g. a stale/
        // never-registered handle, or a model with no output groups at
        // all).
        MeshHandle TryGetGpuMeshHandle(MeshHandle cpuMeshHandle) const;
    };

    // Mirrors SkeletalRigCache::Register() - called once, from the SAME
    // Game::CreateMeshEntityFromGtaFile() hand-off site (alongside, never
    // instead of, AnimationSystem::RegisterSkinnedMesh()), regardless of
    // which skinning mode is currently active - see this phase's own
    // strategy document, Step 4 ("no lazy/deferred GPU buffer creation").
    //
    // `pipelines` is EnsureInitialized() here (idempotent, safe to call
    // every time) - the very first Register() call across the whole engine
    // session is what actually compiles/loads the two skinning compute
    // pipelines, not some earlier, unrelated call site.
    //
    // A no-op (nothing registered, TryGet() keeps returning nullptr for
    // this path) for a boneless/riggless model (`data.skeleton.bones`
    // empty), an empty model (`data.bindPositions` empty), or a model with
    // no parts/no groups at all - mirrors every other cache in this module's
    // "degrade gracefully, never throw" convention.
    void Register(Renderer& renderer, RenderSystem& renderSystem, GpuSkinningPipelines& pipelines,
        const std::string& absoluteGtaPath, const SkinnedMeshData& data, const std::vector<MeshAssetPart>& parts);

    const GpuModelEntry* TryGet(const std::string& absoluteGtaPath) const;

private:
    std::unordered_map<std::string, GpuModelEntry> m_models;
};

} // namespace gte
