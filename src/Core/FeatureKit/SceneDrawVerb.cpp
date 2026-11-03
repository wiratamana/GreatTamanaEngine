#include "SceneDrawChain.h"

#include "SceneShaderSet.h"
#include "VerbSupport.h"
#include "../ClusterClaim.h"
#include "../Core.h"
#include "../GpuSkinningKeys.h"
#include "../../ECS/Components/MeshRenderer.h"
#include "../../Game/Game.h"
#include "../../Game/RenderSystem.h"
#include "../../Game/SceneDrawPass.h"
#include "../../Renderer/SceneServicesDescriptorSet.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
    const EntityMarkSet* excluded = nullptr;
    EntitySelection only;
    bool sceneServices = false;
    bool claim = false;
    std::uint32_t claimedCount = 0;
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

EntitySelection Frame::SelectEntities(std::span<const Entity> entities)
{
    if (m_context == nullptr || m_context->core == nullptr) {
        ReportOnce("SelectEntities() needs a frame that belongs to a render callback");
        return {};
    }
    if (entities.empty()) {
        return {};
    }
    Core& core = *m_context->core;
    const EntityExclusion reserved = core.ReserveExclusion();
    if (reserved.handle == nullptr) {
        ReportOnce("SelectEntities(): no entity set is left this frame");
        return {};
    }
    core.FillExclusion(reserved, entities);
    return EntitySelection{ reserved.handle, entities };
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

SceneDrawChain& SceneDrawChain::Target(Texture target)
{
    if (m_impl && m_impl->active) {
        if (detail::CheckOwned(*m_impl->frame, target, m_impl->name, "the scene target")) {
            m_impl->color = target;
            m_impl->depth = target;
            m_impl->depthClear.reset();
        } else {
            m_impl->refused = true;
        }
    }
    return *this;
}

SceneDrawChain& SceneDrawChain::SceneServices()
{
    if (m_impl) {
        m_impl->sceneServices = true;
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
        m_impl->excluded = static_cast<const EntityMarkSet*>(exclusion.handle);
    }
    return *this;
}

SceneDrawChain& SceneDrawChain::Only(const EntitySelection& selection)
{
    if (m_impl && m_impl->active) {
        // An invalid selection means "nothing to draw", never "everything".
        if (selection.IsValid()) {
            m_impl->only = selection;
        } else {
            m_impl->refused = true;
        }
    }
    return *this;
}

SceneDrawChain& SceneDrawChain::Claim()
{
    if (m_impl) {
        m_impl->claim = true;
    }
    return *this;
}

namespace {

// Scene-service slots published for this view, as the reads the pass must declare.
std::size_t PublishedSceneSlots(const detail::FrameContext& context,
    std::array<rg::MeshPassTextureRead, kSceneServiceSlotCount>& slots)
{
    std::size_t count = 0;
    if (context.blackboard == nullptr) {
        return count;
    }
    for (std::uint32_t slot = 0; slot < kSceneServiceSlotCount; ++slot) {
        const std::optional<rg::TextureHandle> published =
            context.blackboard->Fetch<rg::TextureHandle>(SceneServiceBlackboardKey(slot, context.view));
        if (published.has_value()) {
            slots[count++] = rg::MeshPassTextureRead{ *published, SceneServiceSlotIsDepthResource(slot) };
        }
    }
    return count;
}

} // namespace

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
    Core& core = *context.core;

    const bool needsServices = detail::BindsSceneServices(*impl.shaders);
    if (needsServices && !impl.sceneServices) {
        impl.frame->ReportOnce(std::string("Pass '") + impl.name.value + "': a shaded set needs SceneServices()");
        return false;
    }
    if (impl.sceneServices && context.scope == rg::ViewScope::Shared) {
        impl.frame->ReportOnce(std::string("Pass '") + impl.name.value + "': SceneServices() needs a view frame");
        return false;
    }
    if (impl.claim) {
        const ClusterClaimContext claimContext{ context.scope, core.CaptureArmedThisFrame() };
        switch (SelectionClaimRefusalOf(impl.only.IsValid(), claimContext)) {
        case SelectionClaimRefusal::NoSelection:
            impl.frame->ReportOnce(std::string("Pass '") + impl.name.value + "': Claim() needs Only()");
            return false;
        case SelectionClaimRefusal::NotAllowed:
            return false; // The main draw keeps the entities, so none is drawn twice.
        case SelectionClaimRefusal::None:
            break;
        }
    }
    if (!detail::PrepareSceneShaders(*impl.shaders, context)) {
        impl.frame->ReportOnce(std::string("Pass '") + impl.name.value + "': the shader set is not ready, nothing was declared");
        return false;
    }

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
        pass.ExcludeEntities(impl.excluded, core.ExclusionEpoch());
    }
    if (impl.only.IsValid()) {
        pass.OnlyEntities(static_cast<const EntityMarkSet*>(impl.only.handle), core.ExclusionEpoch());
    }
    if (impl.sceneServices) {
        std::array<rg::MeshPassTextureRead, kSceneServiceSlotCount> slots{};
        const std::size_t slotCount = PublishedSceneSlots(context, slots);
        pass.SceneServices(core.GetSceneServicesDescriptorSet(), context.view,
            std::span<const rg::MeshPassTextureRead>(slots.data(), slotCount));
    }
    if (context.blackboard != nullptr) {
        // Skinned vertices are written by compute passes of this frame.
        const std::optional<std::vector<rg::BufferHandle>> skinned =
            context.blackboard->Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey);
        if (skinned.has_value()) {
            pass.ReadVertexBuffers(*skinned);
        }
    }

    const bool declared = pass.Draw();
    if (declared && impl.claim) {
        // Claim only what this pass can draw; the rest stays on the main draw.
        const RenderSystem& renderSystem = core.GetGame().GetRenderSystem();
        const PipelineOverrideSet& overrides = detail::OverridesOf(*impl.shaders);
        std::uint32_t claimed = 0;
        for (const Entity entity : impl.only.entities) {
            const MeshRenderer* renderer = core.GetRegistry().TryGetComponent<MeshRenderer>(entity);
            if (renderer != nullptr && renderSystem.CanDrawWithOverrides(*renderer, overrides)) {
                core.ClaimEntity(context.scope, entity);
                ++claimed;
            }
        }
        impl.claimedCount = claimed;
    }
    return declared;
}

std::uint32_t SceneDrawChain::ClaimedCount() const noexcept
{
    return m_impl ? m_impl->claimedCount : 0;
}

} // namespace gte::fx
