#include "FeatureRegistration.h"
#include "FeatureLife.h"
#include "FrameContext.h"
#include "RegistrationRunner.h"
#include "SceneSlotRegistrar.h"
#include "SharedOncePerFrame.h"
#include "../../Application/RenderPassViewData.h"
#include "../../Renderer/RenderGraph/BoundedOnceSet.h"
#include "../../Renderer/RenderTexture.h"
#include "../Core.h"
#include "../Logging.h"
#include "../Plugins/RenderFeatureCameraData.h"
#include "../Plugins/RenderFeatureCompositor.h"

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

FrameEvent ToFrameEvent(Stage stage) noexcept
{
    switch (stage) {
    case Stage::PreOpaque:
    case Stage::PreOpaqueShared:
        return FrameEvent::PreOpaque;
    case Stage::Opaque:
        return FrameEvent::Opaque;
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
    case Stage::Opaque:
        return rg::RenderPassEvent::Opaques;
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
        // Once-per-frame and always-on work has no per-view switch.
        const bool gated = !registration.secondaryViewDefault && registration.stage != Stage::PreOpaqueShared
            && !registration.gate.always;
        if (gated) {
            RenderFeatureCompositor* compositor = m_core.GetRenderFeatureCompositor();
            if (compositor == nullptr || !compositor->SetFeatureViewEnabled(registration.name, rg::ViewScope::Secondary, false)) {
                throw std::runtime_error("Core lost the registration '" + registration.name + "'");
            }
        }
        if (!registration.consumedChannel.empty()) {
            RenderFeatureCompositor* compositor = m_core.GetRenderFeatureCompositor();
            if (compositor == nullptr || !compositor->AddChannelConsumer(registration.name, registration.consumedChannel)) {
                throw std::runtime_error("Core lost the registration '" + registration.name + "'");
            }
        }
    }

    void Run(const Registration& registration, SharedOncePerFrame* sharedLatch, rg::RenderGraphBuilder& builder,
        rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView, rg::TextureHandle privateTarget, Extent2D extent,
        const ScenePassReadHandles& handles)
    {
        const std::optional<View> view = BuildView(m_core, currentView);
        Registry& scene = m_core.GetRegistry();

        detail::FrameContext context;
        context.builder = &builder;
        context.blackboard = &blackboard;
        context.core = &m_core;
        context.view = sharedLatch != nullptr ? rg::RenderViewId::Shared() : currentView;
        context.featureName = m_name;
        context.event = ToPassEvent(registration.stage);
        context.toggles = &m_core.GetRenderPassToggleRegistryMutable();
        context.deltaSeconds = ClampFrameDeltaSeconds(m_core.GetTime().UnscaledDeltaTime());
        context.channels = &m_channelNames;

        detail::RunInput input;
        input.registration = &registration;
        input.sharedLatch = sharedLatch;
        input.view = &view;
        input.scene = &scene;
        input.context = &context;
        input.sceneColor = handles.colorHandle;
        input.sceneDepth = handles.depthHandle;
        input.privateTarget = privateTarget;
        input.extent = extent;
        input.frameIndex = builder.FrameIndex();
        const bool perView = sharedLatch == nullptr && view.has_value();
        input.serial = FrameSerial(input.frameIndex, perView ? view->scope : rg::ViewScope::Shared);
        detail::RunRegistration(input, m_failuresLogged);
    }

    Core& m_core;
    std::string m_name;
    FeatureHost m_host;
    std::shared_ptr<FeatureLife> m_life;
    std::unique_ptr<Feature> m_feature;
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
