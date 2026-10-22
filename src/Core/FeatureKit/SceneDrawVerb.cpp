#include "SceneDrawChain.h"

#include "SceneShaderSet.h"
#include "VerbSupport.h"
#include "../Core.h"
#include "../../Game/Game.h"
#include "../../Game/SceneDrawPass.h"

#include <cassert>
#include <optional>
#include <utility>

namespace gte::fx {

// `active` is false for an inert chain: pass toggled off, or the frame has no engine context.
struct SceneDrawChain::Impl {
    Impl(Frame& owner, rg::StaticName passName, bool isActive) noexcept
        : frame(&owner)
        , name(passName)
        , active(isActive)
    {
    }

    Frame* frame;
    rg::StaticName name;
    bool active;
    bool finished = false;
    bool refused = false;
    SceneShaderSet* shaders = nullptr;
    Mat4 camera = Mat4::Identity();
    Texture depth;
    std::optional<float> depthClear;
    Texture color;
    std::optional<float> cullPadding;
    const std::unordered_set<Entity>* excluded = nullptr;
};

SceneDrawChain Frame::SceneDraw(rg::StaticName passName)
{
    bool active = false;
    if (m_context == nullptr || m_context->builder == nullptr || m_context->core == nullptr) {
        ReportOnce("SceneDraw() needs a frame that belongs to a render callback");
    } else {
        active = detail::PassEnabled(*m_context, passName);
    }
    return SceneDrawChain(std::make_unique<SceneDrawChain::Impl>(*this, passName, active));
}

SceneDrawChain::SceneDrawChain(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

SceneDrawChain::~SceneDrawChain()
{
    assert((!m_impl || !m_impl->active || m_impl->finished) && "scene draw chain destroyed without Record()");
}

SceneDrawChain::SceneDrawChain(SceneDrawChain&&) noexcept = default;
SceneDrawChain& SceneDrawChain::operator=(SceneDrawChain&&) noexcept = default;

SceneDrawChain& SceneDrawChain::Shaders(SceneShaderSet& shaders)
{
    if (m_impl) {
        m_impl->shaders = &shaders;
    }
    return *this;
}

SceneDrawChain& SceneDrawChain::Camera(const Mat4& viewProjection)
{
    if (m_impl) {
        m_impl->camera = viewProjection;
    }
    return *this;
}

SceneDrawChain& SceneDrawChain::ClearDepthTarget(Texture depth, float clearValue)
{
    if (m_impl && m_impl->active) {
        if (detail::CheckOwned(*m_impl->frame, depth, m_impl->name, "the depth target")) {
            m_impl->depth = depth;
            m_impl->depthClear = clearValue;
        } else {
            m_impl->refused = true;
        }
    }
    return *this;
}

SceneDrawChain& SceneDrawChain::ColorTarget(Texture color)
{
    if (m_impl && m_impl->active) {
        if (detail::CheckOwned(*m_impl->frame, color, m_impl->name, "the color target")) {
            m_impl->color = color;
        } else {
            m_impl->refused = true;
        }
    }
    return *this;
}

SceneDrawChain& SceneDrawChain::CullToFrustum(float paddingFraction)
{
    if (m_impl) {
        m_impl->cullPadding = paddingFraction;
    }
    return *this;
}

SceneDrawChain& SceneDrawChain::Exclude(const EntityExclusion& exclusion)
{
    if (m_impl && exclusion.handle != nullptr) {
        m_impl->excluded = static_cast<const std::unordered_set<Entity>*>(exclusion.handle);
    }
    return *this;
}

bool SceneDrawChain::Record()
{
    if (!m_impl || !m_impl->active || m_impl->finished) {
        return false;
    }
    Impl& impl = *m_impl;
    impl.finished = true;
    detail::FrameContext& context = *impl.frame->Context();
    if (impl.refused) {
        return false;
    }
    if (impl.shaders == nullptr) {
        impl.frame->ReportOnce(std::string("Pass '") + impl.name.value + "': no shader set was given");
        return false;
    }
    if (!detail::PrepareSceneShaders(*impl.shaders, context)) {
        impl.frame->ReportOnce(std::string("Pass '") + impl.name.value + "': the shader set is not ready, nothing was declared");
        return false;
    }

    Core& core = *context.core;
    SceneDrawPass pass(*context.builder, core.GetGame().GetRenderSystem(), core.GetRegistry(), core.GetRenderer(),
        impl.name, context.scope, context.event);
    pass.Pipelines(detail::OverridesOf(*impl.shaders)).Camera(impl.camera);
    if (impl.color.IsValid()) {
        pass.Color(impl.color.handle);
    }
    if (impl.depth.IsValid()) {
        pass.Depth(impl.depth.handle, impl.depthClear);
    }
    if (impl.cullPadding.has_value()) {
        pass.CullToFrustum(*impl.cullPadding);
    }
    if (impl.excluded != nullptr) {
        pass.ExcludeEntities(impl.excluded);
    }
    return pass.Draw();
}

} // namespace gte::fx
