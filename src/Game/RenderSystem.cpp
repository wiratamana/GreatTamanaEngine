#include "RenderSystem.h"

#include "../Core/Logging.h"
#include "ECS/TransformHierarchy.h"
#include "Instantiation/MaterialLifetimeRules.h"
#include "Renderer/Culling/CullingTypes.h"
#include "Profiling/ScopeTimer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderGraph/BoundedOnceSet.h"
#include "Renderer/RenderGraph/CommandBuffer.h"

#include <array>
#include <cstring>
#include <optional>
#include <string>

namespace gte {

std::vector<DrawCommand> RenderSystem::CollectRenderables(Registry& registry)
{
    std::vector<DrawCommand> commands;
    CollectRenderables(registry, commands);
    return commands;
}

void RenderSystem::CollectRenderables(Registry& registry, std::vector<DrawCommand>& commands)
{
    GTE_PROFILE_SCOPE("RenderSystem::CollectRenderables");

    commands.clear();

    ComponentStorage<MeshRenderer>& renderers = registry.Storage<MeshRenderer>();
    commands.reserve(renderers.Size());

    for (std::size_t i = 0; i < renderers.Size(); ++i) {
        const Entity entity = renderers.EntityAt(i);
        const MeshRenderer& meshRenderer = renderers.ComponentAt(i);

        // The world matrix composes the whole parent chain; identity without a Transform.
        const Mat4 model = ComputeWorldMatrix(registry, entity);

        commands.push_back(DrawCommand{ entity, meshRenderer.mesh, meshRenderer.pipeline, meshRenderer.texture, model });
    }
}

namespace {

// Frames to wait before snapshotting again after the list kept changing, or held a parented renderable.
constexpr std::uint32_t kChangingListCooldown = 30;
constexpr std::uint32_t kParentedListCooldown = 120;

bool SameBytes(const void* a, const void* b, std::size_t size) noexcept
{
    return std::memcmp(a, b, size) == 0;
}

} // namespace

bool RenderSystem::SharedListMatchesSnapshot(Registry& registry) const
{
    ComponentStorage<MeshRenderer>& renderers = registry.Storage<MeshRenderer>();
    ComponentStorage<Transform>& transforms = registry.Storage<Transform>();
    if (renderers.Size() != m_listSnapshot.size() || renderers.Size() != m_sharedList.size()) {
        return false;
    }
    for (std::size_t i = 0; i < m_listSnapshot.size(); ++i) {
        const RenderListEntry& old = m_listSnapshot[i];
        const MeshRenderer& meshRenderer = renderers.ComponentAt(i);
        const Entity entity = renderers.EntityAt(i);
        if (entity != old.entity || meshRenderer.mesh != old.mesh || meshRenderer.pipeline != old.pipeline
            || meshRenderer.texture != old.texture) {
            return false;
        }
        const Transform* transform = transforms.TryGet(entity);
        if (transform == nullptr) {
            if (old.hasTransform) {
                return false;
            }
            continue;
        }
        // Bitwise on purpose: a NaN must compare equal to itself, and -0 must differ from +0.
        if (!old.hasTransform || transform->parent.IsValid()
            || !SameBytes(&transform->position, &old.position, sizeof(Vec3))
            || !SameBytes(&transform->rotation, &old.rotation, sizeof(Quat))
            || !SameBytes(&transform->scale, &old.scale, sizeof(Vec3))) {
            return false;
        }
    }
    return true;
}

bool RenderSystem::SnapshotSharedList(Registry& registry)
{
    ComponentStorage<MeshRenderer>& renderers = registry.Storage<MeshRenderer>();
    ComponentStorage<Transform>& transforms = registry.Storage<Transform>();
    m_listSnapshot.resize(renderers.Size());
    for (std::size_t i = 0; i < m_listSnapshot.size(); ++i) {
        const MeshRenderer& meshRenderer = renderers.ComponentAt(i);
        RenderListEntry& entry = m_listSnapshot[i];
        entry.entity = renderers.EntityAt(i);
        entry.mesh = meshRenderer.mesh;
        entry.pipeline = meshRenderer.pipeline;
        entry.texture = meshRenderer.texture;
        const Transform* transform = transforms.TryGet(entry.entity);
        entry.hasTransform = transform != nullptr;
        if (transform != nullptr) {
            if (transform->parent.IsValid()) {
                return false; // The world matrix would depend on ancestors; such lists are always rebuilt.
            }
            entry.position = transform->position;
            entry.rotation = transform->rotation;
            entry.scale = transform->scale;
        }
    }
    return true;
}

const std::vector<DrawCommand>& RenderSystem::BuildSharedList(Registry& registry)
{
    m_sharedListRegistry = nullptr;
    // A frame whose renderables and their transforms equal the previous frame's reuses the previous list.
    if (m_listCooldown == 0 && m_listSnapshotValid && m_listSnapshotRegistry == &registry
        && SharedListMatchesSnapshot(registry)) {
        m_listMissStreak = 0;
        m_sharedListRegistry = &registry;
        return m_sharedList;
    }

    CollectRenderables(registry, m_sharedList);
    ++m_sharedListEpoch;
    if (m_listCooldown > 0) {
        --m_listCooldown;
    } else {
        // A list that changed twice in a row is probably animated: stop paying for snapshots for a while.
        m_listMissStreak = m_listSnapshotValid ? m_listMissStreak + 1 : 0;
        m_listSnapshotValid = false;
        if (m_listMissStreak >= 2) {
            m_listMissStreak = 0;
            m_listCooldown = kChangingListCooldown;
        } else if (SnapshotSharedList(registry)) {
            m_listSnapshotValid = true;
            m_listSnapshotRegistry = &registry;
        } else {
            m_listCooldown = kParentedListCooldown;
        }
    }
    m_sharedListRegistry = &registry;
    return m_sharedList;
}

void RenderSystem::EndSharedList() noexcept
{
    m_sharedListRegistry = nullptr;
}

bool RenderSystem::HasActiveCamera(Registry& registry) noexcept
{
    ComponentStorage<Camera>& cameras = registry.Storage<Camera>();
    for (std::size_t i = 0; i < cameras.Size(); ++i) {
        if (cameras.ComponentAt(i).active) {
            return true;
        }
    }
    return false;
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
    const EntityMarkSet& batchedEntities, DescriptorSetHandle sceneServicesSet, rg::CommandBuffer* cmd,
    std::optional<SceneCullRequest> cull)
{
    Draw(registry, renderer, ResolveActiveCameraViewProjection(registry, aspectWidthOverHeight), capture, maxDrawCount,
        batchedEntities, std::nullopt, sceneServicesSet, cmd, cull);
}

bool RenderSystem::CanDrawWithOverrides(const MeshRenderer& renderer, const PipelineOverrideSet& overrides) const
{
    const Pipeline* own = m_pipelines.TryGet(renderer.pipeline);
    if (own == nullptr || m_meshes.TryGet(renderer.mesh) == nullptr) {
        return false;
    }
    const std::optional<PipelineHandle> perLayout = ResolveOverridePipeline(overrides, own->VertexLayoutKind());
    const Pipeline* replacement = perLayout.has_value() ? m_pipelines.TryGet(*perLayout) : nullptr;
    return replacement != nullptr
        && CanDrawWithMaterial(replacement->HasMaterialSet(), m_textures.TryGet(renderer.texture) != nullptr);
}

namespace {
const char* VertexLayoutDebugName(VertexLayout layout)
{
    switch (layout) {
    case VertexLayout::PositionColor: return "PositionColor";
    case VertexLayout::PositionNormal: return "PositionNormal";
    case VertexLayout::PositionNormalUv: return "PositionNormalUv";
    case VertexLayout::PositionNormalUvTangent: return "PositionNormalUvTangent";
    case VertexLayout::None: return "None";
    }
    return "Unknown";
}
} // namespace

void RenderSystem::Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection,
    IFrameDebuggerCaptureRecorder* capture, std::optional<std::size_t> maxDrawCount,
    const EntityMarkSet& batchedEntities, std::optional<PipelineOverrideSet> pipelineOverrideSet,
    DescriptorSetHandle sceneServicesSet, rg::CommandBuffer* cmd, std::optional<SceneCullRequest> cull,
    const EntityMarkSet* onlyEntities)
{
    GTE_PROFILE_SCOPE("RenderSystem::Draw");

    // A frame-scoped list is shared by every draw of the frame; otherwise this call collects its own.
    std::vector<DrawCommand> ownCommands;
    if (m_sharedListRegistry != &registry) {
        CollectRenderables(registry, ownCommands);
    }
    const std::vector<DrawCommand>& commands = m_sharedListRegistry == &registry ? m_sharedList : ownCommands;
    const bool hasOverrideSet = pipelineOverrideSet.has_value();

    // An unusable matrix means no culling: a fallback plane would reject real geometry.
    SceneCullCounts* const counts = cull.has_value() ? cull->counts : nullptr;
    if (counts != nullptr) {
        *counts = SceneCullCounts{};
    }
    std::optional<std::array<Plane, 6>> cullPlanes;
    if (cull.has_value()) {
        cullPlanes = TryExtractFrustumPlanes(cull->viewProjection);
    }

    std::size_t consideredCount = 0;

    for (const DrawCommand& command : commands) {
        if (maxDrawCount.has_value() && consideredCount >= *maxDrawCount) {
            break;
        }
        ++consideredCount;

        // A batched entity is drawn by its indirect pass, but still counts toward maxDrawCount.
        if (batchedEntities.Contains(command.entity)) {
            continue;
        }

        // Checked before any layout or pipeline lookup, so unselected entities never log a warning.
        if (onlyEntities != nullptr && !onlyEntities->Contains(command.entity)) {
            continue;
        }

        const Mesh* mesh = m_meshes.TryGet(command.mesh);
        if (cullPlanes.has_value() && mesh != nullptr) {
            const bool bounded = mesh->LocalBounds().has_value();
            const bool outside =
                IsBoundsOutsideFrustum(*cullPlanes, mesh->LocalBounds(), command.model, cull->paddingFraction);
            if (counts != nullptr) {
                ++(bounded ? counts->tested : counts->unbounded);
                counts->culled += outside ? 1u : 0u;
            }
            if (outside) {
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

} // namespace gte
