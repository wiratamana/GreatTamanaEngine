#include "RenderSystem.h"

#include "ECS/Components/Name.h"
#include "ECS/TransformHierarchy.h"
#include "Profiling/ScopeTimer.h"
#include "Renderer/Renderer.h"

// FrameDebuggerCaptureContext (src/Editor/FrameDebuggerCapture.h) is an
// Editor-only type - RenderSystem.h above only ever forward-declares it
// (see that header's own comment). This real #include, AND every actual
// dereference of a `capture` pointer below, must stay wrapped in
// `#if GTE_ENABLE_EDITOR` - a GTE_ENABLE_EDITOR=OFF build compiles this
// whole file fine either way (the forward declaration is enough), but
// would FAIL TO LINK if an unconditional RecordDraw() call site referenced
// a type/function that's never compiled into that configuration at all.
// See task_manager/frame-debugger-3/
// PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md's own Step 3.1b.
#if GTE_ENABLE_EDITOR
#include "../Editor/FrameDebuggerCapture.h"
#endif

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
    FrameDebuggerCaptureContext* capture, std::optional<std::size_t> maxDrawCount,
    const std::unordered_set<Entity>& batchedEntities)
{
    Draw(registry, renderer, ResolveActiveCameraViewProjection(registry, aspectWidthOverHeight), capture, maxDrawCount,
        batchedEntities);
}

void RenderSystem::Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection,
    FrameDebuggerCaptureContext* capture, std::optional<std::size_t> maxDrawCount,
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

#if GTE_ENABLE_EDITOR
            // Zero-overhead-when-disarmed: this whole block collapses to
            // one already-taken "is this pointer null" branch when
            // `capture` is nullptr (the common case - every frame until
            // PHASE3 wires a real arming trigger, and every ordinary frame
            // afterward) - no string formatting, no vector work happens.
            // See task_manager/frame-debugger-3/
            // PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md's own Step 2.
            if (capture != nullptr) {
                const std::string materialTextureDebugName = materialTexture != nullptr
                    ? renderer.GetMemoryDebugName(materialTexture->texture.Handle())
                    : std::string();
                capture->RecordDraw(pipeline->DebugName(), materialTextureDebugName, viewProjection);

                // frame-debugger-6 campaign, PHASE3 - additionally record
                // this exact draw's own per-entity attribution facts (never
                // deduplicated, unlike RecordDraw()'s own name lists above -
                // see FrameDebuggerCaptureContext::RecordEntityDraw()'s own
                // doc comment). Triangle count uses the exact same
                // HasIndexBuffer() ? IndexCount()/3 : VertexCount()/3 rule
                // DrawStats.h::AccumulateDrawStats() already uses, so these
                // two counts can never drift apart.
                const std::uint32_t triangleCount =
                    mesh->HasIndexBuffer() ? (mesh->IndexCount() / 3) : (mesh->VertexCount() / 3);

                std::string displayName;
                if (const Name* name = registry.TryGetComponent<Name>(command.entity);
                    name != nullptr && !name->value.empty()) {
                    displayName = name->value;
                } else {
                    // Matches HierarchyPanel::BuildEntityLabel()'s own
                    // synthesized "Entity %u" fallback format exactly (minus
                    // its Camera-only " (Camera)" suffix, which never applies
                    // to a mesh-rendering draw) - see this phase's own Step 2.
                    displayName = "Entity " + std::to_string(command.entity.index);
                }

                capture->RecordEntityDraw(command.entity.index, command.entity.generation, displayName,
                    pipeline->DebugName(), materialTextureDebugName, triangleCount);
            }
#endif

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
