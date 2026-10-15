#pragma once

#include "DrawCommand.h"
#include "RenderBatching.h"
#include "../Core/GraphicsHandles.h" // DescriptorSetHandle
#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Math/Mat4.h"
#include "Math/Vec3.h"
#include "Renderer/Culling/GpuDrivenBatchCache.h"
#include "Renderer/MaterialTexture.h"
#include "Renderer/Mesh.h"
#include "Renderer/Pipeline.h"
#include "Renderer/PipelineHandle.h"
#include "Renderer/ResourcePool.h"
#include "Renderer/MaterialTextureHandle.h"
// SceneQuery.h only forward-declares RenderSystem; never include this header back from it.
#include "SceneQuery.h"
#include "../Core/FrameDebuggerCaptureRecorder.h"

#include <cstddef>
#include <optional>
#include <unordered_set>
#include <vector>

namespace gte::rg {
class CommandBuffer;
}

namespace gte {

class Renderer;

// One GPU-driven batch for this frame: which (mesh, pipeline), how many instances now, and the draws it replaces.
struct GpuDrivenBatchFrameEntry {
    MeshHandle mesh;
    PipelineHandle pipeline;
    std::size_t instanceCount = 0;
    std::vector<DrawCommand> commands;
};

// The only layer that knows both the ECS (Registry, Transform, MeshRenderer) and the Renderer (Mesh, Pipeline).
// It owns the Mesh/Pipeline/MaterialTexture objects; components hold their handles.
class RenderSystem {
public:
    RenderSystem() = default;

    // Takes ownership of a created resource and returns the handle a MeshRenderer stores.
    MeshHandle RegisterMesh(Mesh&& mesh) { return m_meshes.Insert(std::move(mesh)); }
    PipelineHandle RegisterPipeline(Pipeline&& pipeline) { return m_pipelines.Insert(std::move(pipeline)); }
    MaterialTextureHandle RegisterTexture(MaterialTexture&& texture) { return m_textures.Insert(std::move(texture)); }

    // Frees a pipeline slot. Only safe for a handle no MeshRenderer references: the pool keeps no back-references,
    // and entities holding a removed handle silently stop drawing. For rollback and owner teardown.
    bool UnregisterPipeline(PipelineHandle handle) { return m_pipelines.Remove(handle); }

    // Null for an invalid or stale handle; callers skip the part.
    Mesh* TryGetMesh(MeshHandle handle) { return m_meshes.TryGet(handle); }
    const Pipeline* TryGetPipeline(PipelineHandle handle) const { return m_pipelines.TryGet(handle); }

    // One DrawCommand per entity with a MeshRenderer, with its world matrix. Touches only `registry`.
    static std::vector<DrawCommand> CollectRenderables(Registry& registry);

    // Transparent renderables. Always empty: nothing marks a renderable transparent yet.
    static std::vector<DrawCommand> CollectTransparentRenderables(Registry& registry);

    // View-projection of the first active Camera, or identity when there is none.
    static Mat4 ResolveActiveCameraViewProjection(Registry& registry, float aspectWidthOverHeight);

    // Eye position of the first active Camera, or zero when there is none.
    static Vec3 ResolveActiveCameraWorldPosition(Registry& registry) noexcept;

    // Submits every renderable to `renderer` for a target of the given aspect, using the active Camera.
    // Handles that no longer resolve are skipped. Parameters are documented on the view-projection overload.
    void Draw(Registry& registry, Renderer& renderer, float aspectWidthOverHeight,
        IFrameDebuggerCaptureRecorder* capture = nullptr, std::optional<std::size_t> maxDrawCount = std::nullopt,
        const std::unordered_set<Entity>& batchedEntities = {},
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid,
        rg::CommandBuffer* cmd = nullptr);

    // Draw() with an explicit view-projection. `capture` records frame-debugger facts, `maxDrawCount` caps the commands
    // considered, `batchedEntities` are drawn by GPU-driven batches instead, `pipelineOverrideSet` picks a pipeline per
    // vertex layout (an entity without an entry is skipped), `cmd` receives the draws instead of renderer.Submit().
    void Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection,
        IFrameDebuggerCaptureRecorder* capture = nullptr, std::optional<std::size_t> maxDrawCount = std::nullopt,
        const std::unordered_set<Entity>& batchedEntities = {},
        std::optional<PipelineOverrideSet> pipelineOverrideSet = std::nullopt,
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid, rg::CommandBuffer* cmd = nullptr);

    // Groups this frame's draws by (mesh, pipeline), keeps the eligible groups and packs their instances into
    // `cache`. `gpuSkinnedOutputBuffersThisFrame` holds the vertex-buffer handles of meshes skinned on the GPU
    // this frame; those never join a batch. Builds no render-graph passes and changes nothing Draw() does.
    std::vector<GpuDrivenBatchFrameEntry> CollectGpuDrivenBatches(Registry& registry, Renderer& renderer,
        GpuDrivenBatchCache& cache, const std::unordered_set<BufferObjectHandle>& gpuSkinnedOutputBuffersThisFrame = {},
        std::size_t minInstancesForGpuDrivenBatch = kMinInstancesForGpuDrivenBatch);

private:
    ResourcePool<Mesh, MeshHandle> m_meshes;
    ResourcePool<Pipeline, PipelineHandle> m_pipelines;
    ResourcePool<MaterialTexture, MaterialTextureHandle> m_textures;

    // Warn-once state for pipelineOverrideSet; single-threaded, bounded by the distinct keys ever seen.
    std::unordered_set<VertexLayout> m_warnedMissingOverrideLayouts;
    std::unordered_set<PipelineHandle> m_warnedStaleOverrideSetHandles;
};

} // namespace gte
