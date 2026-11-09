#include "SceneDrawPass.h"

#include "../Renderer/SceneServicesDescriptorSet.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace gte {

SceneDrawRequest MakeSceneDrawRequest(const Mat4& viewProjection, const PipelineOverrideSet& overrides)
{
    SceneDrawRequest request;
    request.viewProjection = viewProjection;
    request.pipelineOverrideSet = overrides;
    return request;
}

SceneDrawPass::SceneDrawPass(rg::RenderGraphBuilder& builder, RenderSystem& system, Registry& registry,
    Renderer& renderer, rg::StaticName name, rg::ViewScope scope, rg::RenderPassEvent event)
    : m_mesh(builder, name, scope, event)
    , m_system(&system)
    , m_registry(&registry)
    , m_renderer(&renderer)
{
}

SceneDrawPass& SceneDrawPass::Pipelines(const PipelineOverrideSet& overrides)
{
    m_overrides = overrides;
    return *this;
}

SceneDrawPass& SceneDrawPass::Camera(const Mat4& viewProjection)
{
    m_viewProjection = viewProjection;
    return *this;
}

SceneDrawPass& SceneDrawPass::Color(rg::TextureHandle target, std::optional<std::array<float, 4>> clear)
{
    m_mesh.Color(target, clear);
    return *this;
}

SceneDrawPass& SceneDrawPass::Depth(rg::TextureHandle target, std::optional<float> clear)
{
    m_mesh.Depth(target, clear);
    return *this;
}

SceneDrawPass& SceneDrawPass::CullToFrustum(float paddingFraction)
{
    m_cullPadding = paddingFraction;
    return *this;
}

SceneDrawPass& SceneDrawPass::CullPlanes(std::span<const Plane> planes)
{
    m_extraPlaneCount = static_cast<std::uint32_t>(std::min<std::size_t>(planes.size(), m_extraPlanes.size()));
    std::copy_n(planes.begin(), m_extraPlaneCount, m_extraPlanes.begin());
    return *this;
}

SceneDrawPass& SceneDrawPass::ExcludeEntities(const EntityMarkSet* entities, const std::uint64_t* liveEpoch)
{
    m_excluded = entities;
    m_liveEpoch = liveEpoch;
    m_epochAtDeclare = liveEpoch != nullptr ? *liveEpoch : 0;
    return *this;
}

SceneDrawPass& SceneDrawPass::OnlyEntities(const EntityMarkSet* entities, const std::uint64_t* liveEpoch)
{
    m_only = entities;
    m_liveEpoch = liveEpoch;
    m_epochAtDeclare = liveEpoch != nullptr ? *liveEpoch : 0;
    return *this;
}

SceneDrawPass& SceneDrawPass::SceneServices(const SceneServicesDescriptorSet& services, rg::RenderViewId view,
    std::span<const rg::MeshPassTextureRead> publishedSlots)
{
    m_services = &services;
    m_servicesView = view;
    for (const rg::MeshPassTextureRead& slot : publishedSlots) {
        m_mesh.ReadTexture(slot.texture, slot.depth);
    }
    return *this;
}

SceneDrawPass& SceneDrawPass::ReadVertexBuffers(std::span<const rg::BufferHandle> buffers)
{
    for (const rg::BufferHandle& buffer : buffers) {
        m_mesh.ReadVertexBuffer(buffer);
    }
    return *this;
}

bool SceneDrawPass::Draw()
{
    SceneDrawRequest request = MakeSceneDrawRequest(m_viewProjection, m_overrides);
    request.batchedEntities = m_excluded;
    request.onlyEntities = m_only;
    if (m_cullPadding.has_value()) {
        request.cull = SceneCullRequest{ m_viewProjection, *m_cullPadding };
        request.cull->extraPlanes = m_extraPlanes;
        request.cull->extraPlaneCount = m_extraPlaneCount;
    }
    return m_mesh.Draw([system = m_system, registry = m_registry, renderer = m_renderer, liveEpoch = m_liveEpoch,
                           epochAtDeclare = m_epochAtDeclare, services = m_services, servicesView = m_servicesView,
                           request = std::move(request)]() {
        const bool stale = liveEpoch != nullptr && *liveEpoch != epochAtDeclare;
        // A selection from an older frame may be gone: draw nothing rather than everything.
        if (stale && request.onlyEntities != nullptr) {
            assert(false && "selection set read after its frame ended");
            return;
        }
        // An exclusion set from an older frame may be gone: draw everything on the CPU path instead.
        if (stale && request.batchedEntities != nullptr) {
            assert(false && "exclusion set read after its frame ended");
            SceneDrawRequest safe = request;
            safe.batchedEntities = nullptr;
            DrawScene(*system, *registry, *renderer, safe);
            return;
        }
        if (services != nullptr) {
            SceneDrawRequest bound = request;
            bound.sceneServicesSet = services->DescriptorSetFor(servicesView);
            DrawScene(*system, *registry, *renderer, bound);
            return;
        }
        DrawScene(*system, *registry, *renderer, request);
    });
}

} // namespace gte
