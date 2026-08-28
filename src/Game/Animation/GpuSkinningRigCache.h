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
// CPU/GPU switch (Phase 5 - see AnimationSystem::SkinningMode) never needs a
// lazy "oh, I need to register this now" fallback path. A model registered
// here pays real, one-time GPU memory/setup cost the moment it's spawned -
// see this phase's own strategy document, Step 4, for why this is a
// deliberate trade against lazy/deferred registration.
//
// Phase 5 (GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md) is
// what actually DISPATCHES a compute pass against these resources (see
// AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()) and swaps a
// MeshRenderer's MeshHandle onto GpuModelEntry::TryGetGpuMeshHandle()'s
// result (and back, via TryGetCpuMeshHandle(), when switching back to CPU
// mode) - this class itself still owns nothing about WHEN any of that
// happens, only the resources themselves.
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

        // Phase 5 (GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md)
        // - a stable, PERSISTENT (lives as long as this OutputGroup does,
        // never a per-frame temporary) name for this group's own compute
        // pass/imported-buffer, e.g. "<absoluteGtaPath>#SkinGroup0". Set
        // exactly once, in Register() below. RenderGraphBuilder::AddPass()/
        // ImportBuffer()'s own `name` parameter REQUIRES a string literal or
        // otherwise static-storage-duration const char* (see
        // RenderGraphBuilder.h) - a fresh std::string built every frame
        // would dangle the instant that temporary is destroyed, so this
        // field's whole reason to exist is to give
        // AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame() a
        // pointer (via .c_str()) that stays valid for as long as this
        // OutputGroup (and therefore this GpuModelEntry) does - i.e. for the
        // rest of the process's lifetime, per this cache's own "load once,
        // never evict" convention.
        std::string debugName;

        // Maps each CPU-mode MeshAssetPart's own MeshHandle (the ORIGINAL,
        // CPU-skinned/static Mesh - see MeshAssetGpuCatalog.cpp) to its
        // GPU-skinned counterpart, built here from THIS group's own shared
        // outputVertexBuffer plus that same part's own index data (see
        // MeshAssetPart::indices) - Phase 5's runtime switch swaps a
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
        // model's own bone count; Phase 5 uploads fresh skinning matrices
        // into it every frame this model is animated in GPU mode (see
        // AnimationSystem::Update()). Left at whatever undefined contents
        // CreateStructuredBuffer() leaves it in until that first real
        // upload - never read by anything until then.
        // Deliberately `mutable`: TryGet() (below) returns a const
        // GpuModelEntry* (this cache's own resources are otherwise never
        // mutated after Register()), but this ONE buffer is the documented
        // exception - AnimationSystem::Update() calls Upload() on it, every
        // frame, for a model currently animated in GPU mode. Marking just
        // this one field mutable (rather than dropping const off TryGet()'s
        // whole return type) keeps every OTHER field's real immutability
        // guarantee visible/enforced by the type system.
        mutable Buffer boneMatricesBuffer;

        std::vector<OutputGroup> outputGroups;

        // Best-effort lookup used by Phase 5's runtime switch: returns the
        // GPU-skinned counterpart of `cpuMeshHandle` if this model has one
        // registered, or kInvalidMeshHandle otherwise (e.g. a stale/
        // never-registered handle, or a model with no output groups at
        // all).
        MeshHandle TryGetGpuMeshHandle(MeshHandle cpuMeshHandle) const;

        // The reverse of TryGetGpuMeshHandle() above - resolves an already
        // GPU-skinned MeshHandle back to its ORIGINAL CPU-mode counterpart,
        // used by Phase 5's runtime switch when switching a model back OUT
        // of GPU mode (see this phase's own strategy document, Step 3.4:
        // "switching modes mid-session is safe by construction... switching
        // OUT of GPU mode needs no cleanup since the two Mesh objects are
        // fully independent"). Returns kInvalidMeshHandle for a handle that
        // isn't one of this model's own GPU-skinned meshes.
        MeshHandle TryGetCpuMeshHandle(MeshHandle gpuMeshHandle) const;
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
