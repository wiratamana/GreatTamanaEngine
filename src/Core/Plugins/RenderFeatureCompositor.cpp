#include "RenderFeatureCompositor.h"

#include "RenderFeatureCompositorImpl.h"

#include <utility>

namespace gte {

RenderFeatureCompositor::RenderFeatureCompositor(Core& core, Renderer& /*renderer*/)
    : m_impl(std::make_unique<Impl>(core))
{
}

RenderFeatureCompositor::~RenderFeatureCompositor() = default;

void RenderFeatureCompositor::OnPluginsLoaded(const std::vector<IPluginModule*>& modules)
{
    m_impl->OnPluginsLoaded(modules);
}

void RenderFeatureCompositor::ContributeRenderGraphPasses(
    const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out)
{
    m_impl->ContributeRenderGraphPasses(frame, out);
}

std::vector<RenderFeatureDebugEntry> RenderFeatureCompositor::DebugSnapshot() const
{
    return m_impl->DebugSnapshot();
}

bool RenderFeatureCompositor::SetFeatureEnabled(const std::string& name, bool enabled)
{
    return m_impl->SetFeatureEnabled(name, enabled);
}

bool RenderFeatureCompositor::SetFeaturePriority(const std::string& name, std::int32_t priority)
{
    return m_impl->SetFeaturePriority(name, priority);
}

bool RenderFeatureCompositor::RegisterFeature(
    const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback, RenderFeatureOwner owner)
{
    return m_impl->RegisterFeature(descriptor, std::move(callback), owner);
}

bool RenderFeatureCompositor::UnregisterProjectFeature(const char* name)
{
    return m_impl->UnregisterProjectFeature(name);
}

bool RenderFeatureCompositor::RegisterStageFeature(CompositorStage stage, const std::string& name,
    std::int32_t priority, ProjectPreOpaqueCallback callback, RenderFeatureOwner owner)
{
    return m_impl->RegisterStageFeature(stage, name, priority, std::move(callback), owner);
}

bool RenderFeatureCompositor::RegisterStageFeature(CompositorStage stage, const std::string& name,
    std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner)
{
    return m_impl->RegisterStageFeature(stage, name, priority, std::move(callback), owner);
}

bool RenderFeatureCompositor::UnregisterStageFeature(CompositorStage stage, const char* name)
{
    return m_impl->UnregisterStageFeature(stage, name);
}

template <CompositorStage S>
const std::vector<StageEntry<StageCallback<S>>>& RenderFeatureCompositor::StageFeaturesInPriorityOrder() const noexcept
{
    if constexpr (S == CompositorStage::PreOpaque) {
        return m_impl->preOpaque.entries;
    } else if constexpr (S == CompositorStage::Opaque) {
        return m_impl->opaque.entries;
    } else if constexpr (S == CompositorStage::PostOpaque) {
        return m_impl->postOpaque.entries;
    } else {
        return m_impl->postTransparent.entries;
    }
}

template const std::vector<PreOpaqueEntry>&
RenderFeatureCompositor::StageFeaturesInPriorityOrder<CompositorStage::PreOpaque>() const noexcept;
template const std::vector<SceneStageEntry>&
RenderFeatureCompositor::StageFeaturesInPriorityOrder<CompositorStage::Opaque>() const noexcept;
template const std::vector<SceneStageEntry>&
RenderFeatureCompositor::StageFeaturesInPriorityOrder<CompositorStage::PostOpaque>() const noexcept;
template const std::vector<SceneStageEntry>&
RenderFeatureCompositor::StageFeaturesInPriorityOrder<CompositorStage::PostTransparent>() const noexcept;

} // namespace gte
