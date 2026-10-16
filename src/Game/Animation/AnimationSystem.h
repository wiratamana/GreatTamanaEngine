#pragma once

#include "../../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../../Renderer/RenderGraph/GpuBuffer.h"
#include "../../ECS/Entity.h"
#include "../../ECS/Registry.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/MeshVertex.h"
#include "AnimationClipCache.h"
#include "GpuSkinningRigCache.h"
#include "ResolvedAnimationBindingCache.h"
#include "SkeletalRigCache.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class Renderer;
class RenderSystem;
class MeshInstantiationSystem;
struct MeshAssetPart;

// Plays animations and skins meshes, on the CPU or the GPU.
// Owns the clip, binding and rig caches; uses the render system to re-upload meshes and the instantiation system
// to find a model's mesh parts.
class AnimationSystem {
public:
    AnimationSystem(RenderSystem& renderSystem, MeshInstantiationSystem& meshInstantiationSystem)
        : m_renderSystem(renderSystem)
        , m_meshInstantiationSystem(meshInstantiationSystem)
    {
    }

    // CpuJobSystem skins on the job system and re-uploads the vertex buffers. GpuCompute uploads each playing
    // model's bone matrices and swaps its renderers onto their GPU-skinned meshes; the compute dispatch is recorded
    // later, by whoever calls CollectModelsNeedingGpuSkinningThisFrame().
    enum class SkinningMode : std::uint8_t {
        CpuJobSystem,
        GpuCompute,
    };

    void SetSkinningMode(SkinningMode mode) noexcept { m_mode = mode; }
    SkinningMode GetSkinningMode() const noexcept { return m_mode; }

    // Called by Game after a skinned model is spawned.
    void RegisterSkinnedMesh(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
    {
        m_rigCache.Register(absoluteGtaPath, data);
    }

    // The GPU-side sibling of RegisterSkinnedMesh(), called alongside it for every rigged model whatever the mode.
    // `parts` are the model's mesh parts. No-op for a boneless model or one whose parts resolve to no live mesh.
    void RegisterGpuSkinnedMesh(Renderer& renderer, const std::string& absoluteGtaPath, const SkinnedMeshData& data,
        const std::vector<MeshAssetPart>& parts)
    {
        m_gpuRigCache.Register(renderer, m_renderSystem, absoluteGtaPath, data, parts);
    }

    // The shared skinning shaders, for the pass that dispatches them.
    GpuSkinningPipelines& GetGpuSkinningPipelines() noexcept { return m_gpuSkinningPipelines; }

    // Starts a clip on an entity. False, with no component change, when the entity is dead, has no mesh asset
    // source or skinning data, or the clip does not resolve. Never throws.
    bool Play(Registry& registry, Entity targetEntity, const std::string& absoluteAnimationGtaPath);

    // Frame order: EvaluatePoses() -> PhysicsSystem::Update() -> SkinAndUpload(), linked by ResolvedAnimationPose.
    // Advances every playing animator and writes its pose. Enabled DynamicChainRig entities without an animated pose
    // get a bind-pose baseline. Touches no GPU state.
    void EvaluatePoses(Registry& registry, double deltaSeconds);

    // Skins and uploads from the ResolvedAnimationPose (physics-adjusted or not), each model once: the CPU path
    // re-uploads vertex buffers, the GPU path uploads bone matrices and records a dispatch. Models are processed
    // strictly one at a time because instances of one model share GPU mesh buffers.
    void SkinAndUpload(Registry& registry);

    // One compute dispatch the skinning pass must record this frame, per model output group. A model is listed once,
    // so no output buffer is written twice. Empty in CpuJobSystem mode, with nothing animating, and on frozen frames.
    struct GpuSkinningDispatchRequest {
        // Stable name for the imported output buffer; lives as long as the rig cache.
        const char* name = nullptr;

        // Buffers and sizes of the dispatch, owned by the rig cache and valid for this frame.
        // `outputHandle` is filled by whoever imports the output into the graph.
        GpuSkinningDispatch dispatch;
    };
    std::vector<GpuSkinningDispatchRequest> CollectModelsNeedingGpuSkinningThisFrame() const;

    // Called instead of SkinAndUpload() on a frozen frame, so no stale dispatch list is reported.
    void ClearGpuSkinningDispatchThisFrame() noexcept { m_gpuModelsNeedingDispatchThisFrame.clear(); }

private:
    // Skins and uploads one entity's model. `mode` is the mode captured once at the start of SkinAndUpload().
    void SkinAndUploadOneEntity(Registry& registry, Entity entity, const std::string& meshGtaPath, SkinningMode mode);

    RenderSystem& m_renderSystem;
    MeshInstantiationSystem& m_meshInstantiationSystem;

    SkeletalRigCache m_rigCache;
    AnimationClipCache m_clipCache;
    ResolvedAnimationBindingCache m_bindingCache;

    // GPU-side siblings of m_rigCache; the pipelines are built on the first RegisterGpuSkinnedMesh() call.
    GpuSkinningPipelines m_gpuSkinningPipelines;
    GpuSkinningRigCache m_gpuRigCache;

    SkinningMode m_mode = SkinningMode::CpuJobSystem;

    // Model paths that need a GPU skinning dispatch this frame; rebuilt by SkinAndUpload().
    std::vector<std::string> m_gpuModelsNeedingDispatchThisFrame;

    // Per-model scratch arrays kept between frames so skinning does not allocate every frame.
    struct AnimatorScratchBuffers {
        std::vector<Vec3> skinnedPositions;
        std::vector<Vec3> skinnedNormals;
        std::vector<MeshVertex> packedUntextured;
        std::vector<MeshVertexUv> packedTextured;
    };
    std::unordered_map<std::string, AnimatorScratchBuffers> m_scratchBuffers;
};

} // namespace gte
