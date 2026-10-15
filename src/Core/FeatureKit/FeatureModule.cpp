#include "FeatureRegistration.h"
#include "FeatureLife.h"
#include "FrameContext.h"
#include "SceneSlotRegistrar.h"
#include "SharedOncePerFrame.h"
#include "../../Application/RenderPassViewData.h"
#include "../../Renderer/RenderGraph/BoundedOnceSet.h"
#include "../../Renderer/RenderGraph/RenderFeatureScope.h"
#include "../../Renderer/RenderGraph/RenderPassToggleGuard.h"
#include "../../Renderer/RenderTexture.h"
#include "../Core.h"
#include "../Logging.h"
#include "../Plugins/RenderFeatureCameraData.h"

#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace gte::fx {

Registry& FeatureHost::Scene() const noexcept
{
    return m_core.GetRegistry();
}

namespace {

constexpr const char* kLogCategory = "FeatureKit";

FrameEvent ToFrameEvent(Stage stage) noexcept
{
    switch (stage) {
    case Stage::PreOpaque:
    case Stage::PreOpaqueShared:
        return FrameEvent::PreOpaque;
    case Stage::PostOpaque:
        return FrameEvent::PostOpaque;
    case Stage::PostTransparent:
        return FrameEvent::PostTransparent;
    case Stage::PostComposite:
        return FrameEvent::PostComposite;
    case Stage::PreUI:
        return FrameEvent::PreUI;
    }
    return FrameEvent::PreOpaque;
}

// The graph ordering tag every pass a callback declares carries.
rg::RenderPassEvent ToPassEvent(Stage stage) noexcept
{
    switch (stage) {
    case Stage::PreOpaque:
    case Stage::PreOpaqueShared:
        return rg::RenderPassEvent::PreOpaques;
    case Stage::PostOpaque:
        return rg::RenderPassEvent::AfterOpaques;
    case Stage::PostTransparent:
        return rg::RenderPassEvent::AfterTransparents;
    case Stage::PostComposite:
    case Stage::PreUI:
        return rg::RenderPassEvent::AfterEverything;
    }
    return rg::RenderPassEvent::PreOpaques;
}

RenderFeatureBlendMode ToCoreBlend(Blend blend) noexcept
{
    switch (blend) {
    case Blend::Replace:
        return RenderFeatureBlendMode::Replace;
    case Blend::AlphaOver:
        return RenderFeatureBlendMode::AlphaOver;
    case Blend::Additive:
        return RenderFeatureBlendMode::Additive;
    case Blend::Multiply:
        return RenderFeatureBlendMode::Multiply;
    case Blend::ScreenSpaceMask:
        return RenderFeatureBlendMode::ScreenSpaceMask;
    }
    return RenderFeatureBlendMode::AlphaOver;
}

// The view a callback runs for, or nothing when the engine has no data for it.
std::optional<View> BuildView(Core& core, rg::RenderViewId currentView)
{
    const RenderPassViewData* data = core.FindRenderPassViewDataNoCopy(currentView);
    if (data == nullptr) {
        return std::nullopt;
    }
    const RenderFeatureCameraData camera = ResolveRenderFeatureCameraData(currentView, data);
    View view;
    view.scope = TranslateLegacyViewScope(currentView);
    if (data->renderTexture != nullptr) {
        view.extent = data->renderTexture->Extent();
    }
    view.eye = camera.eyeWorldPosition;
    view.viewProjection = data->viewProjection;
    view.invViewProjection = camera.invViewProjection;
    view.invViewProjectionValid = camera.invViewProjectionValid;
    return view;
}

// The engine-side owner of one feature: runs Setup(), then commits what it collected.
class FeatureModule final : public IEngineFeatureModule, private IRegistrationSink {
public:
    FeatureModule(Core& core, const char* name, const FeatureFactory& factory)
        : m_core(core)
        , m_name(name != nullptr ? name : "")
        , m_host(core)
        , m_life(std::make_shared<FeatureLife>())
    {
        m_feature = factory(m_host);
        if (!m_feature) {
            throw std::runtime_error("feature '" + m_name + "': the factory returned nothing");
        }
        // If this fails after some passes were registered, the destroyed life token silences them.
        if (const std::optional<std::string> failure = SetupAndCommit(*m_feature, *this)) {
            throw std::runtime_error("feature '" + m_name + "': " + *failure);
        }
    }

    const char* ModuleName() const override { return m_name.c_str(); }

    Feature* FeatureOrNull() noexcept { return m_feature.get(); }

private:
    ISceneSlotRegistrar* SlotRegistrar() noexcept override { return &m_slotRegistrar; }

    void Commit(const Registration& registration) override
    {
        SharedOncePerFrame* latch = nullptr;
        if (registration.stage == Stage::PreOpaqueShared) {
            latch = &m_sharedLatches.emplace_back();
        }
        const auto run = [this, registration, latch](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard,
                             rg::RenderViewId currentView, rg::TextureHandle privateTarget, Extent2D extent,
                             const ScenePassReadHandles& handles, const RenderFeatureCameraData&) {
            Run(registration, latch, builder, blackboard, currentView, privateTarget, extent, handles);
        };
        if (!m_core.RegisterRenderPass(ToFrameEvent(registration.stage), registration.name.c_str(),
                registration.priority, WhileAlive(std::weak_ptr<FeatureLife>(m_life), run), ToCoreBlend(registration.blend))) {
            throw std::runtime_error("Core refused the registration '" + registration.name + "'");
        }
    }

    void Run(const Registration& registration, SharedOncePerFrame* sharedLatch, rg::RenderGraphBuilder& builder,
        rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView, rg::TextureHandle privateTarget, Extent2D extent,
        const ScenePassReadHandles& handles)
    {
        // Gate first: a disabled pass must not run any side effect.
        const rg::RenderPassToggleRegistry* toggles = &m_core.GetRenderPassToggleRegistryMutable();
        const auto isEnabled = [toggles](std::string_view pass) {
            return rg::ShouldDeclareBuiltInPassThisFrame(toggles, pass);
        };
        if (!GateAllows(registration.gate, registration.name, isEnabled)) {
            return;
        }

        const bool shared = sharedLatch != nullptr;
        const std::optional<View> view = BuildView(m_core, currentView);
        if (!ShouldInvokeFeatureCallback(view, shared ? CallbackScope::Shared : CallbackScope::PerView)) {
            return;
        }
        const std::uint64_t frameIndex = builder.FrameIndex();
        if (shared && !sharedLatch->ShouldRun(frameIndex)) {
            return;
        }

        const rg::RenderFeatureScope scope(builder, m_name);
        Registry& scene = m_core.GetRegistry();
        const std::uint64_t serial = ++m_serial;

        detail::FrameContext context;
        context.builder = &builder;
        context.blackboard = &blackboard;
        context.core = &m_core;
        context.view = currentView;
        context.featureName = m_name;
        context.event = ToPassEvent(registration.stage);
        context.toggles = toggles;
        context.scope = shared ? rg::ViewScope::Shared : view->scope;
        context.serial = serial;
        context.channels = &m_channelNames;
        try {
            switch (registration.stage) {
            case Stage::PreOpaque: {
                PreOpaqueFrame frame(*view, scene, serial, &context);
                std::get<PreOpaqueCallback>(registration.callback)(frame);
                break;
            }
            case Stage::PostOpaque:
            case Stage::PostTransparent: {
                PostOpaqueFrame frame(*view, scene, serial, handles.colorHandle, handles.depthHandle, &context);
                std::get<PostOpaqueCallback>(registration.callback)(frame);
                break;
            }
            case Stage::PostComposite:
            case Stage::PreUI: {
                CompositeFrame frame(
                    *view, scene, serial, handles.colorHandle, handles.depthHandle, privateTarget, extent, &context);
                std::get<CompositeCallback>(registration.callback)(frame);
                break;
            }
            case Stage::PreOpaqueShared: {
                SharedFrame frame(scene, serial, &context);
                std::get<SharedCallback>(registration.callback)(frame);
                break;
            }
            }
            if (shared) {
                sharedLatch->MarkDone(frameIndex);
            }
        } catch (const std::exception& e) {
            if (shared) {
                sharedLatch->MarkFailed(frameIndex);
            }
            // A throw would end the whole offscreen frame; log it once and drop this callback's work.
            const std::string message = "feature '" + m_name + "', callback '" + registration.name + "': " + e.what();
            if (rg::BoundedOnceSet<32>::ShouldLog(m_failuresLogged.Note(message))) {
                GTE_LOG_ERROR(kLogCategory, message);
            }
        }
    }

    Core& m_core;
    std::string m_name;
    FeatureHost m_host;
    std::shared_ptr<FeatureLife> m_life;
    std::unique_ptr<Feature> m_feature;
    std::uint64_t m_serial = 0;
    std::deque<SharedOncePerFrame> m_sharedLatches; // One per shared registration; addresses stay stable.
    rg::BoundedOnceSet<32> m_failuresLogged;
    detail::ChannelNames m_channelNames;
    detail::EngineSceneSlotRegistrar m_slotRegistrar;
};

} // namespace

namespace detail {

std::unique_ptr<IEngineFeatureModule> MakeModule(Core& core, const char* name, FeatureFactory factory)
{
    return std::make_unique<FeatureModule>(core, name, factory);
}

Feature* FeatureOfModule(IEngineFeatureModule& module) noexcept
{
    FeatureModule* adapter = dynamic_cast<FeatureModule*>(&module);
    return adapter != nullptr ? adapter->FeatureOrNull() : nullptr;
}

} // namespace detail

} // namespace gte::fx
