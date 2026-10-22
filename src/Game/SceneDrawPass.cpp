#include "SceneDrawPass.h"

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

SceneDrawPass& SceneDrawPass::ExcludeEntities(const std::unordered_set<Entity>* entities)
{
    m_excluded = entities;
    return *this;
}

bool SceneDrawPass::Draw()
{
    SceneDrawRequest request = MakeSceneDrawRequest(m_viewProjection, m_overrides);
    request.batchedEntities = m_excluded;
    if (m_cullPadding.has_value()) {
        request.cull = SceneCullRequest{ m_viewProjection, *m_cullPadding };
    }
    return m_mesh.Draw([system = m_system, registry = m_registry, renderer = m_renderer,
                           request = std::move(request)]() { DrawScene(*system, *registry, *renderer, request); });
}

} // namespace gte
