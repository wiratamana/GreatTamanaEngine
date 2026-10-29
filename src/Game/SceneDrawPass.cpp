#include "SceneDrawPass.h"

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

SceneDrawPass& SceneDrawPass::ExcludeEntities(const EntityMarkSet* entities, const std::uint64_t* liveEpoch)
{
    m_excluded = entities;
    m_liveEpoch = liveEpoch;
    m_epochAtDeclare = liveEpoch != nullptr ? *liveEpoch : 0;
    return *this;
}

bool SceneDrawPass::Draw()
{
    SceneDrawRequest request = MakeSceneDrawRequest(m_viewProjection, m_overrides);
    request.batchedEntities = m_excluded;
    if (m_cullPadding.has_value()) {
        request.cull = SceneCullRequest{ m_viewProjection, *m_cullPadding };
    }
    return m_mesh.Draw([system = m_system, registry = m_registry, renderer = m_renderer, liveEpoch = m_liveEpoch,
                           epochAtDeclare = m_epochAtDeclare, request = std::move(request)]() {
        // A set from an older frame may be gone: draw everything on the CPU path instead.
        if (liveEpoch != nullptr && *liveEpoch != epochAtDeclare && request.batchedEntities != nullptr) {
            assert(false && "exclusion set read after its frame ended");
            SceneDrawRequest safe = request;
            safe.batchedEntities = nullptr;
            DrawScene(*system, *registry, *renderer, safe);
            return;
        }
        DrawScene(*system, *registry, *renderer, request);
    });
}

} // namespace gte
