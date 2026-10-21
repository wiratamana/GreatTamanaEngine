#include "RenderSystem.h"

#include "../Core/Logging.h"
#include "ECS/TransformHierarchy.h"
#include "Instantiation/MaterialLifetimeRules.h"
#include "Profiling/ScopeTimer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderGraph/BoundedOnceSet.h"
#include "Renderer/RenderGraph/CommandBuffer.h"

#include <string>

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

        // The world matrix composes the whole parent chain; identity without a Transform.
        const Mat4 model = ComputeWorldMatrix(registry, entity);

        commands.push_back(DrawCommand{ entity, meshRenderer.mesh, meshRenderer.pipeline, meshRenderer.texture, model });
    }

    return commands;
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

        // The camera follows its parents; an entity without a Transform uses identity.
        Transform transform;
        if (registry.TryGetComponent<Transform>(entity) != nullptr) {
            transform = ComputeWorldTransform(registry, entity);
        }

        return camera.ProjectionMatrix(aspectWidthOverHeight) * Camera::ViewMatrix(transform);
    }

    // No active camera: identity leaves vertices in clip space, as authored.
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
    const std::unordered_set<Entity>& batchedEntities, DescriptorSetHandle sceneServicesSet, rg::CommandBuffer* cmd)
{
    Draw(registry, renderer, ResolveActiveCameraViewProjection(registry, aspectWidthOverHeight), capture, maxDrawCount,
        batchedEntities, std::nullopt, sceneServicesSet, cmd);
}

namespace {
const char* VertexLayoutDebugName(VertexLayout layout)
{
    switch (layout) {
    case VertexLayout::PositionColor: return "PositionColor";
    case VertexLayout::PositionNormal: return "PositionNormal";
    case VertexLayout::PositionNormalUv: return "PositionNormalUv";
    case VertexLayout::PositionNormalUvTangent: return "PositionNormalUvTangent";
    case VertexLayout::PositionNormalInstanced: return "PositionNormalInstanced";
    case VertexLayout::None: return "None";
    }
    return "Unknown";
}
} // namespace

void RenderSystem::Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection,
    IFrameDebuggerCaptureRecorder* capture, std::optional<std::size_t> maxDrawCount,
    const std::unordered_set<Entity>& batchedEntities, std::optional<PipelineOverrideSet> pipelineOverrideSet,
    DescriptorSetHandle sceneServicesSet, rg::CommandBuffer* cmd, std::optional<SceneCullRequest> cull)
{
    GTE_PROFILE_SCOPE("RenderSystem::Draw");

    const std::vector<DrawCommand> commands = CollectRenderables(registry);
    const bool hasOverrideSet = pipelineOverrideSet.has_value();
    const std::array<Plane, 6> cullPlanes =
        cull.has_value() ? ExtractFrustumPlanes(cull->viewProjection) : std::array<Plane, 6>{};

    std::size_t consideredCount = 0;

    for (const DrawCommand& command : commands) {
        if (maxDrawCount.has_value() && consideredCount >= *maxDrawCount) {
            break;
        }
        ++consideredCount;

        // A batched entity is drawn by its indirect pass, but still counts toward maxDrawCount.
        if (batchedEntities.contains(command.entity)) {
            continue;
        }

        const Mesh* mesh = m_meshes.TryGet(command.mesh);
        if (cull.has_value() && mesh != nullptr && mesh->LocalBounds().has_value()) {
            const AABB worldBounds =
                PadAABB(TransformAABB(*mesh->LocalBounds(), command.model), cull->paddingFraction);
            if (IsAABBOutsideFrustum(cullPlanes, worldBounds)) {
                continue;
            }
        }

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
            if (!CanDrawWithMaterial(pipeline->HasMaterialSet(), materialTexture != nullptr)) {
                const std::string key = std::to_string(command.texture.index) + "/" + std::to_string(command.texture.generation);
                if (rg::BoundedOnceSet<64>::ShouldLog(m_warnedUnresolvedMaterials.Note(key))) {
                    GTE_LOG_WARNING("RenderSystem",
                        "Draw(): material handle " + key + " does not resolve; draws that need it are skipped.");
                }
                continue;
            }
            const DescriptorSetHandle descriptorSet =
                materialTexture != nullptr ? materialTexture->set.Handle() : DescriptorSetHandle::Invalid;

            // Costs one null check when no capture is armed.
            if (capture != nullptr) {
                capture->RecordFrameDebuggerDraw(
                    registry, renderer, command.entity, *mesh, *pipeline, materialTexture, viewProjection);
            }

            // A command buffer routes the draw through the frame debugger's per-draw capture; the GPU work is identical.
            if (cmd != nullptr) {
                cmd->Draw(*pipeline, *mesh, command.model, viewProjection, descriptorSet, sceneServicesSet);
            } else {
                renderer.Submit(*pipeline, *mesh, command.model, viewProjection, descriptorSet, sceneServicesSet);
            }
        }
    }
}

std::vector<GpuDrivenBatchFrameEntry> RenderSystem::CollectGpuDrivenBatches(Registry& registry, Renderer& renderer,
    GpuDrivenBatchCache& cache, const std::unordered_set<BufferObjectHandle>& gpuSkinnedOutputBuffersThisFrame,
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

        // A GPU-skinned mesh draws from a skinning output buffer and never joins a GPU-driven batch.
        const bool isGpuSkinned = gpuSkinnedOutputBuffersThisFrame.contains(mesh->VertexBufferHandle());

        if (!IsGpuDrivenEligible(group, mesh->HasIndexBuffer(), pipeline->VertexLayoutKind(), isGpuSkinned,
                minInstancesForGpuDrivenBatch)) {
            continue;
        }

        const GpuDrivenBatchKey key{ group.mesh, group.pipeline };
        const std::size_t instanceCount = group.commands.size();

        cache.EnsureCapacity(renderer, key, instanceCount);

        // The batch shares one mesh, so its local bounds are computed once and transformed per instance.
        const AABB localBounds = mesh->LocalBounds().value_or(AABB{});

        std::vector<GpuCullingInstanceInput> instances;
        instances.reserve(instanceCount);
        for (const DrawCommand& command : group.commands) {
            const AABB worldBounds = TransformAABB(localBounds, command.model);
            instances.push_back(PackCullingInstanceInput(
                command.model, worldBounds, /*firstIndex=*/0, mesh->IndexCount(), /*vertexOffset=*/0));
        }

        cache.PackThisFrame(key, instances);

        // Points the batch's descriptor sets at the cache's current buffers; cheap when nothing was reallocated.
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
