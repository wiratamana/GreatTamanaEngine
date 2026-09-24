#include "RenderSystem.h"

#include "ECS/TransformHierarchy.h"
#include "Profiling/ScopeTimer.h"
#include "Renderer/Renderer.h"

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

void RenderSystem::Draw(Registry& registry, Renderer& renderer, float aspectWidthOverHeight,
    IFrameDebuggerCaptureRecorder* capture, std::optional<std::size_t> maxDrawCount,
    const std::unordered_set<Entity>& batchedEntities)
{
    Draw(registry, renderer, ResolveActiveCameraViewProjection(registry, aspectWidthOverHeight), capture, maxDrawCount,
        batchedEntities);
}

void RenderSystem::Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection,
    IFrameDebuggerCaptureRecorder* capture, std::optional<std::size_t> maxDrawCount,
    const std::unordered_set<Entity>& batchedEntities)
{
    GTE_PROFILE_SCOPE("RenderSystem::Draw");

    const std::vector<DrawCommand> commands = CollectRenderables(registry);

    // task_manager/frame-debugger-7 campaign, PHASE3
    // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.2) - `maxDrawCount`, when set, stops iterating after that many
    // COMMANDS have been considered (the loop's own iteration count - see
    // this method's own header doc comment for why this is NOT the same
    // thing as "successfully resolved draws"). std::nullopt (every
    // pre-existing call site) means "no cutoff" - iterate every command,
    // exactly the pre-PHASE3 behavior.
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
        const Pipeline* pipeline = m_pipelines.TryGet(command.pipeline);
        if (mesh != nullptr && pipeline != nullptr) {
            const MaterialTexture* materialTexture = m_textures.TryGet(command.texture);
            const VkDescriptorSet descriptorSet =
                materialTexture != nullptr ? materialTexture->descriptorSet : VK_NULL_HANDLE;

            // editor-core-separation-2 campaign, PHASE2 -
            // zero-overhead-when-disarmed: this call collapses to one
            // already-taken "is this pointer null" branch when `capture` is
            // nullptr (the common case - every frame until PHASE3 wires a
            // real arming trigger, and every ordinary frame afterward) - no
            // string formatting, no vector work happens. See
            // task_manager/frame-debugger-3/
            // PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md's own Step 2. The
            // `#if GTE_ENABLE_EDITOR` wrapper that used to surround this
            // block is GONE - the null-check itself is now the only gating
            // needed, since RecordFrameDebuggerDraw() is a virtual method on
            // the complete IFrameDebuggerCaptureRecorder interface `capture`
            // points to (RenderSystem.h's own #include of src/Core/
            // FrameDebuggerCaptureRecorder.h), reachable through gte_core.a
            // alone with zero undefined-reference risk - never a
            // gte_editor-only free-function symbol called by name anymore
            // (see this file's own updated top-of-file comment).
            if (capture != nullptr) {
                capture->RecordFrameDebuggerDraw(
                    registry, renderer, command.entity, *mesh, *pipeline, materialTexture, viewProjection);
            }

            renderer.Submit(*pipeline, *mesh, command.model, viewProjection, descriptorSet);
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
