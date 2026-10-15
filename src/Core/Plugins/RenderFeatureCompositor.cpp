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

bool RenderFeatureCompositor::RegisterPreOpaqueFeatureInternal(
    const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback, RenderFeatureOwner owner)
{
    return m_impl->RegisterPreOpaqueFeatureInternal(name, priority, std::move(callback), owner);
}

bool RenderFeatureCompositor::UnregisterPreOpaqueFeature(const char* name)
{
    return m_impl->UnregisterPreOpaqueFeature(name);
}

const std::vector<RenderFeatureCompositor::PreOpaqueEntry>& RenderFeatureCompositor::PreOpaqueFeaturesInPriorityOrder()
    const noexcept
{
    return m_impl->preOpaque;
}

bool RenderFeatureCompositor::RegisterPostOpaqueFeatureInternal(
    const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner)
{
    return m_impl->RegisterPostOpaqueFeatureInternal(name, priority, std::move(callback), owner);
}

bool RenderFeatureCompositor::UnregisterPostOpaqueFeature(const char* name)
{
    return m_impl->UnregisterPostOpaqueFeature(name);
}

const std::vector<RenderFeatureCompositor::PostOpaqueEntry>& RenderFeatureCompositor::PostOpaqueFeaturesInPriorityOrder()
    const noexcept
{
    return m_impl->postOpaque;
}

bool RenderFeatureCompositor::RegisterPostTransparentFeatureInternal(
    const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner)
{
    return m_impl->RegisterPostTransparentFeatureInternal(name, priority, std::move(callback), owner);
}

bool RenderFeatureCompositor::UnregisterPostTransparentFeature(const char* name)
{
    return m_impl->UnregisterPostTransparentFeature(name);
}

const std::vector<RenderFeatureCompositor::PostTransparentEntry>&
RenderFeatureCompositor::PostTransparentFeaturesInPriorityOrder() const noexcept
{
    return m_impl->postTransparent;
}

} // namespace gte
