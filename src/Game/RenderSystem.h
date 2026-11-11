#pragma once

#include "DrawCommand.h"
#include "../Core/GraphicsHandles.h" // DescriptorSetHandle
#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Math/Mat4.h"
#include "Math/Vec3.h"
#include "Renderer/MaterialTexture.h"
#include "Renderer/Mesh.h"
#include "Renderer/Pipeline.h"
#include "Renderer/PipelineHandle.h"
#include "Renderer/RenderGraph/BoundedOnceSet.h"
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

// The only layer that knows both the ECS (Registry, Transform, MeshRenderer) and the Renderer (Mesh, Pipeline).
// It owns the Mesh/Pipeline/MaterialTexture objects; components hold their handles.
class RenderSystem {
public:
    RenderSystem() = default;

    // Takes ownership of a created resource and returns the handle a MeshRenderer stores.
    MeshHandle RegisterMesh(Mesh&& mesh) { return m_meshes.Insert(std::move(mesh)); }
    PipelineHandle RegisterPipeline(Pipeline&& pipeline) { return m_pipelines.Insert(std::move(pipeline)); }
    MaterialTextureHandle RegisterTexture(MaterialTexture&& texture) { return m_textures.Insert(std::move(texture)); }

    // Destroys the material; the handle goes stale. Only safe when no entity uses it and the GPU is done with it.
    bool UnregisterTexture(MaterialTextureHandle handle) { return m_textures.Remove(handle); }

    // Destroys the mesh; the handle goes stale. Only safe when no entity uses it and the GPU is done with it.
    bool ReleaseMesh(MeshHandle handle) { return m_meshes.Remove(handle); }

    // Number of live Mesh objects in the pool.
    std::size_t MeshCount() const noexcept { return m_meshes.LiveCount(); }

    // Frees a pipeline slot. Only safe for a handle no MeshRenderer references: the pool keeps no back-references,
    // and entities holding a removed handle silently stop drawing. For rollback and owner teardown.
    bool UnregisterPipeline(PipelineHandle handle) { return m_pipelines.Remove(handle); }

    // Null for an invalid or stale handle; callers skip the part.
    Mesh* TryGetMesh(MeshHandle handle) { return m_meshes.TryGet(handle); }
    const Pipeline* TryGetPipeline(PipelineHandle handle) const { return m_pipelines.TryGet(handle); }

    // Descriptor set of a live material, or Invalid for an invalid or stale handle.
    DescriptorSetHandle MaterialSetOf(MaterialTextureHandle handle) const
    {
        const MaterialTexture* material = m_textures.TryGet(handle);
        return material != nullptr ? material->set.Handle() : DescriptorSetHandle::Invalid;
    }

    // True when Draw() with `overrides` would submit a draw for `renderer` (live mesh, a pipeline for its layout, material).
    bool CanDrawWithOverrides(const MeshRenderer& renderer, const PipelineOverrideSet& overrides) const;

    // One DrawCommand per entity with a MeshRenderer, with its world matrix. Touches only `registry`.
    static std::vector<DrawCommand> CollectRenderables(Registry& registry);

    // Same list written into `out`, whose capacity is reused.
    static void CollectRenderables(Registry& registry, std::vector<DrawCommand>& out);

    // The render list of one frame. While it is built, every Draw() of the same registry reads it instead of
    // collecting again. Nothing may add, remove or move renderables until the scope ends.
    class SharedListScope {
    public:
        explicit SharedListScope(RenderSystem& system) noexcept : m_system(system) {}
        ~SharedListScope() { m_system.EndSharedList(); }
        SharedListScope(const SharedListScope&) = delete;
        SharedListScope& operator=(const SharedListScope&) = delete;

        // Collects the list and returns it; later Draw() calls on `registry` reuse it.
        const std::vector<DrawCommand>& Build(Registry& registry) { return m_system.BuildSharedList(registry); }

        // Grows whenever the list content may differ from the previous frame's; equal when it was reused as is.
        std::uint64_t Epoch() const noexcept { return m_system.m_sharedListEpoch; }

    private:
        RenderSystem& m_system;
    };

    // True when some Camera component is active.
    static bool HasActiveCamera(Registry& registry) noexcept;

    // View-projection of the first active Camera, or identity when there is none.
    static Mat4 ResolveActiveCameraViewProjection(Registry& registry, float aspectWidthOverHeight);

    // Same camera with the eye at the origin: ResolveActiveCameraViewProjection() * Translation(eye) without the
    // rounding of a large eye position. Nothing when there is no active Camera.
    static std::optional<Mat4> ResolveActiveCameraEyeRelativeViewProjection(Registry& registry, float aspectWidthOverHeight);

    // Eye position of the first active Camera, or zero when there is none.
    static Vec3 ResolveActiveCameraWorldPosition(Registry& registry) noexcept;

    // Submits every renderable to `renderer` for a target of the given aspect, using the active Camera.
    // Handles that no longer resolve are skipped. Parameters are documented on the view-projection overload.
    void Draw(Registry& registry, Renderer& renderer, float aspectWidthOverHeight,
        IFrameDebuggerCaptureRecorder* capture = nullptr, std::optional<std::size_t> maxDrawCount = std::nullopt,
        const EntityMarkSet& batchedEntities = {},
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid,
        rg::CommandBuffer* cmd = nullptr, std::optional<SceneCullRequest> cull = std::nullopt);

    // Draw() with an explicit view-projection and extras. `onlyEntities`, when non-null, limits the draw to its entities.
    void Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection,
        IFrameDebuggerCaptureRecorder* capture = nullptr, std::optional<std::size_t> maxDrawCount = std::nullopt,
        const EntityMarkSet& batchedEntities = {},
        std::optional<PipelineOverrideSet> pipelineOverrideSet = std::nullopt,
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid, rg::CommandBuffer* cmd = nullptr,
        std::optional<SceneCullRequest> cull = std::nullopt, const EntityMarkSet* onlyEntities = nullptr);

private:
    const std::vector<DrawCommand>& BuildSharedList(Registry& registry);
    void EndSharedList() noexcept;

    // One renderable as the last collect saw it: everything its DrawCommand was computed from.
    struct RenderListEntry {
        Entity entity;
        MeshHandle mesh;
        PipelineHandle pipeline;
        MaterialTextureHandle texture;
        bool hasTransform = false;
        Vec3 position;
        Quat rotation;
        Vec3 scale;
    };
    bool SharedListMatchesSnapshot(Registry& registry) const;
    bool SnapshotSharedList(Registry& registry); // False when a renderable has a parent (list not reusable).

    // The list keeps its content between frames; `m_sharedListRegistry` is set only while a frame may read it.
    std::vector<DrawCommand> m_sharedList;
    // m_sharedList[i].entity, dense, so the claimed-entity skip of Draw() reads 8 bytes per command.
    std::vector<Entity> m_sharedEntities;
    const Registry* m_sharedListRegistry = nullptr;
    std::uint64_t m_sharedListEpoch = 0;
    std::vector<RenderListEntry> m_listSnapshot;
    const Registry* m_listSnapshotRegistry = nullptr;
    bool m_listSnapshotValid = false;
    std::uint32_t m_listMissStreak = 0; // Consecutive frames the list changed while a snapshot existed.
    std::uint32_t m_listCooldown = 0;   // Frames left before a snapshot is tried again.
    ResourcePool<Mesh, MeshHandle> m_meshes;
    ResourcePool<Pipeline, PipelineHandle> m_pipelines;
    ResourcePool<MaterialTexture, MaterialTextureHandle> m_textures;

    // Warn-once state for pipelineOverrideSet; single-threaded, bounded by the distinct keys ever seen.
    std::unordered_set<VertexLayout> m_warnedMissingOverrideLayouts;
    std::unordered_set<PipelineHandle> m_warnedStaleOverrideSetHandles;
    rg::BoundedOnceSet<64> m_warnedUnresolvedMaterials; // One warning per unresolved material handle.
};

} // namespace gte
