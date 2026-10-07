#include "RenderSystem.h"

#include "../Core/Logging.h"
#include "ECS/TransformHierarchy.h"
#include "Profiling/ScopeTimer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderGraph/CommandBuffer.h"

// editor-core-separation-2 campaign, PHASE2 - the free function
// gte::RecordFrameDebuggerDraws() (declared in RenderSystem.h,
// PHASE2_FRAME_DEBUGGER_CAPTURE_POINTER_SAFETY_FIX.md's own former fix) is
// GONE - Draw() below now calls capture->RecordFrameDebuggerDraw(...), a
// virtual method on the gte_core-owned IFrameDebuggerCaptureRecorder
// interface (src/Core/FrameDebuggerCaptureRecorder.h) `capture` now points
// to, instead of a gte_editor-only free function by name (closing
// "Defect A" - see task_manager/editor-core-separation-2/
// PHASE0_MASTER_STRATEGY.md). This file still contains ZERO #include of
// FrameDebuggerCapture.h and ZERO reference to the concrete
// FrameDebuggerCaptureContext type - RenderSystem.h's own #include of the
// interface header (never the concrete type) is all this file needs. A
// virtual call through a pointer to a COMPLETE abstract-interface type
// needs only a vtable read at runtime (satisfied by whichever concrete
// object - always FrameDebuggerCaptureContext, gte_editor-owned - the
// pointer actually points at), requiring ZERO link-time symbol in
// gte_core.a itself, unlike the old direct free-function call by name.

namespace gte {

std::vector<DrawCommand> RenderSystem::CollectRenderables(Registry& registry)
{
    GTE_PROFILE_SCOPE("RenderSystem::CollectRenderables");

    std::vector<DrawCommand> commands;

    ComponentStorage<MeshRenderer>& renderers = registry.Storage<MeshRenderer>();
    commands.reserve(renderers.Size());

    for (std::size_t i = 0; i < renderers.Size(); ++i) {
        const Entity entity = renderers.EntityAt(i);
        const MeshRenderer& meshRenderer = renderers.ComponentAt(i);

        // ComputeWorldMatrix() (ECS/TransformHierarchy.h) walks this
        // entity's whole parent chain, composing parentWorld * local at
        // every level - for an entity with no parent (or no Transform at
        // all) this is exactly transform->LocalToWorldMatrix()/
        // Mat4::Identity(), the same fallback this used to compute inline
        // before parenting existed.
        const Mat4 model = ComputeWorldMatrix(registry, entity);

        commands.push_back(DrawCommand{ entity, meshRenderer.mesh, meshRenderer.pipeline, meshRenderer.texture, model });
    }

    return commands;
}

// Render Pass campaign, PHASE2 - see this method's own doc comment in
// RenderSystem.h. Always empty today - `registry` is intentionally unused
// (kept, named, for signature stability - see that header comment).
std::vector<DrawCommand> RenderSystem::CollectTransparentRenderables(Registry& registry)
{
    (void)registry;
    return {};
}

Mat4 RenderSystem::ResolveActiveCameraViewProjection(Registry& registry, float aspectWidthOverHeight)
{
    ComponentStorage<Camera>& cameras = registry.Storage<Camera>();

    for (std::size_t i = 0; i < cameras.Size(); ++i) {
        const Camera& camera = cameras.ComponentAt(i);
        if (!camera.active) {
            continue;
        }

        const Entity entity = cameras.EntityAt(i);

        // ComputeWorldTransform() (ECS/TransformHierarchy.h) resolves this
        // camera entity's Transform through its whole parent chain first -
        // a Camera parented under a moving entity (e.g. a vehicle) now
        // genuinely follows it, matching Unity's own behavior. Falls back
        // to an identity Transform (origin, no rotation) when this camera
        // entity has no Transform of its own at all, same as before.
        Transform transform;
        if (registry.TryGetComponent<Transform>(entity) != nullptr) {
            transform = ComputeWorldTransform(registry, entity);
        }

        return camera.ProjectionMatrix(aspectWidthOverHeight) * Camera::ViewMatrix(transform);
    }

    // No active Camera anywhere in the Registry - Identity() is the
    // multiplicative no-op, so every draw's clip-space position ends up
    // being exactly its model matrix's output, matching this engine's
    // original (pre-Camera) triangle-demo behavior.
    return Mat4::Identity();
}

Vec3 RenderSystem::ResolveActiveCameraWorldPosition(Registry& registry) noexcept
{
    ComponentStorage<Camera>& cameras = registry.Storage<Camera>();
    for (std::size_t i = 0; i < cameras.Size(); ++i) {
        const Camera& camera = cameras.ComponentAt(i);
        if (!camera.active) {
            continue;
        }

        const Entity entity = cameras.EntityAt(i);
        if (registry.TryGetComponent<Transform>(entity) != nullptr) {
            return ComputeWorldTransform(registry, entity).position;
        }
        return Vec3::Zero();
    }
    return Vec3::Zero();
}

void RenderSystem::Draw(Registry& registry, Renderer& renderer, float aspectWidthOverHeight,
    IFrameDebuggerCaptureRecorder* capture, std::optional<std::size_t> maxDrawCount,
    const std::unordered_set<Entity>& batchedEntities, VkDescriptorSet sceneServicesSet, rg::CommandBuffer* cmd)
{
    Draw(registry, renderer, ResolveActiveCameraViewProjection(registry, aspectWidthOverHeight), capture, maxDrawCount,
        batchedEntities, std::nullopt, sceneServicesSet, cmd);
}

namespace {
// Readable name for a GTE_LOG_WARNING message - RenderSystem.cpp only, not
// part of any public header.
const char* VertexLayoutDebugName(VertexLayout layout)
{
    switch (layout) {
    case VertexLayout::PositionColor: return "PositionColor";
    case VertexLayout::PositionNormal: return "PositionNormal";
    case VertexLayout::PositionNormalUv: return "PositionNormalUv";
    case VertexLayout::PositionNormalInstanced: return "PositionNormalInstanced";
    }
    return "Unknown";
}
} // namespace

void RenderSystem::Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection,
    IFrameDebuggerCaptureRecorder* capture, std::optional<std::size_t> maxDrawCount,
    const std::unordered_set<Entity>& batchedEntities, std::optional<PipelineOverrideSet> pipelineOverrideSet,
    VkDescriptorSet sceneServicesSet, rg::CommandBuffer* cmd)
{
    GTE_PROFILE_SCOPE("RenderSystem::Draw");

    const std::vector<DrawCommand> commands = CollectRenderables(registry);
    const bool hasOverrideSet = pipelineOverrideSet.has_value();

    std::size_t consideredCount = 0;

    for (const DrawCommand& command : commands) {
        if (maxDrawCount.has_value() && consideredCount >= *maxDrawCount) {
            break;
        }
        ++consideredCount;

        // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
        // PHASE5 - a batched entity is skipped by this per-entity path (its
        // own indirect-draw pass draws it instead this frame) but STILL
        // counts toward consideredCount above, matching maxDrawCount's own
        // "iteration count, not resolved-draw count" contract - see this
        // method's own header doc comment (RenderSystem.h).
        if (batchedEntities.contains(command.entity)) {
            continue;
        }

        const Mesh* mesh = m_meshes.TryGet(command.mesh);
        const Pipeline* originalPipeline = m_pipelines.TryGet(command.pipeline);
        const Pipeline* pipeline = originalPipeline;

        if (hasOverrideSet) {
            if (originalPipeline == nullptr) {
                continue; // Unresolvable own pipeline - nothing to key the lookup on.
            }
            const VertexLayout entityLayout = originalPipeline->VertexLayoutKind();
            const std::optional<PipelineHandle> perLayout = ResolveOverridePipeline(*pipelineOverrideSet, entityLayout);
            if (!perLayout.has_value()) {
                if (m_warnedMissingOverrideLayouts.insert(entityLayout).second) {
                    GTE_LOG_WARNING("RenderSystem",
                        std::string("Draw(): pipelineOverrideSet has no entry for VertexLayout::") +
                        VertexLayoutDebugName(entityLayout) +
                        " - entities with this layout are skipped this call. Logged once per layout.");
                }
                continue; // This entity's layout has no override entry - skip, never fall back.
            }
            pipeline = m_pipelines.TryGet(*perLayout);
            if (pipeline == nullptr) {
                if (m_warnedStaleOverrideSetHandles.insert(*perLayout).second) {
                    GTE_LOG_WARNING("RenderSystem",
                        "Draw(): pipelineOverrideSet has an entry that does not resolve to a live Pipeline "
                        "(stale handle) - entities needing this layout are skipped. Logged once per handle.");
                }
                continue;
            }
        }

        if (mesh != nullptr && pipeline != nullptr) {
            const MaterialTexture* materialTexture = m_textures.TryGet(command.texture);
            const VkDescriptorSet descriptorSet =
                materialTexture != nullptr ? materialTexture->descriptorSet : VK_NULL_HANDLE;

            // editor-core-separation-2 campaign, PHASE2 -
            // zero-overhead-when-disarmed: this call collapses to one
            // already-taken "is this pointer null" branch when `capture` is
            // nullptr (the common case) - no string formatting, no vector
            // work happens.
            if (capture != nullptr) {
                capture->RecordFrameDebuggerDraw(
                    registry, renderer, command.entity, *mesh, *pipeline, materialTexture, viewProjection);
            }

            // Per-draw-call granularity: a non-null `cmd` routes this draw
            // through gte::rg::CommandBuffer::Draw() instead, which feeds
            // FrameDebuggerEventSink::NoteCommandResult() for the Frame
            // Debugger's per-draw-call texture capture - see RenderSystem.h's
            // own `cmd` doc comment. Both paths issue identical Vulkan work.
            if (cmd != nullptr) {
                cmd->Draw(*pipeline, *mesh, command.model, viewProjection, descriptorSet, sceneServicesSet);
            } else {
                renderer.Submit(*pipeline, *mesh, command.model, viewProjection, descriptorSet, sceneServicesSet);
            }
        }
    }
}

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE4 - see this method's own doc comment in RenderSystem.h.
std::vector<GpuDrivenBatchFrameEntry> RenderSystem::CollectGpuDrivenBatches(Registry& registry, Renderer& renderer,
    GpuDrivenBatchCache& cache, const std::unordered_set<VkBuffer>& gpuSkinnedOutputBuffersThisFrame,
    std::size_t minInstancesForGpuDrivenBatch)
{
    GTE_PROFILE_SCOPE("RenderSystem::CollectGpuDrivenBatches");

    std::vector<GpuDrivenBatchFrameEntry> result;

    const std::vector<DrawCommand> commands = CollectRenderables(registry);
    const std::vector<RenderBatchGroup> groups = GroupDrawCommandsByMeshAndPipeline(commands);

    for (const RenderBatchGroup& group : groups) {
        const Mesh* mesh = m_meshes.TryGet(group.mesh);
        const Pipeline* pipeline = m_pipelines.TryGet(group.pipeline);
        if (mesh == nullptr || pipeline == nullptr) {
            continue; // Unresolvable handle - never eligible, mirrors Draw()'s own "skip, never crash" convention.
        }

        // Locked Design Decision 7(d), PHASE0 - see this method's own doc
        // comment in RenderSystem.h for the full cross-reference reasoning.
        const bool isGpuSkinned = gpuSkinnedOutputBuffersThisFrame.contains(mesh->VertexBuffer());

        if (!IsGpuDrivenEligible(group, mesh->HasIndexBuffer(), pipeline->VertexLayoutKind(), isGpuSkinned,
                minInstancesForGpuDrivenBatch)) {
            continue;
        }

        const GpuDrivenBatchKey key{ group.mesh, group.pipeline };
        const std::size_t instanceCount = group.commands.size();

        cache.EnsureCapacity(renderer, key, instanceCount);

        // Every instance in this batch shares the exact same Mesh, so its
        // LOCAL bounds are computed once here and re-transformed per
        // instance below - mirrors this frame's own per-entity Transform
        // resolution cost exactly (TransformAABB() is as cheap as the
        // world-matrix multiply CollectRenderables() already did).
        const AABB localBounds = mesh->LocalBounds().value_or(AABB{});

        std::vector<GpuCullingInstanceInput> instances;
        instances.reserve(instanceCount);
        for (const DrawCommand& command : group.commands) {
            const AABB worldBounds = TransformAABB(localBounds, command.model);
            instances.push_back(PackCullingInstanceInput(
                command.model, worldBounds, /*firstIndex=*/0, mesh->IndexCount(), /*vertexOffset=*/0));
        }

        cache.PackThisFrame(key, instances);

        // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
        // PHASE5 - (re)allocates/rewrites this batch's two persistent
        // descriptor sets (culling + instance-buffer) so they always point
        // at whatever buffers `cache` currently holds for `key` this frame -
        // a no-op cost-wise on a frame where EnsureCapacity() above didn't
        // reallocate (Rewrite() is cheap - see ComputeDescriptorSet's own
        // "safe/expected every frame" convention). Also lazily
        // EnsureInitialized()s `cache`'s own shared CullingPipelines the
        // first time any batch is ever collected this session - see
        // GpuDrivenBatchCache.h's own doc comment on why PHASE4 deliberately
        // never did this itself.
        cache.EnsureDescriptorSetsWritten(renderer, key);

        GpuDrivenBatchFrameEntry entry;
        entry.mesh = group.mesh;
        entry.pipeline = group.pipeline;
        entry.instanceCount = instanceCount;
        entry.commands = group.commands;
        result.push_back(std::move(entry));
    }

    return result;
}

} // namespace gte
