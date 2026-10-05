#include "AtmosphereFeature.h"

#include "AtmosphereParameters.h"
#include "AtmospherePassSequence.h"
#include "../../Core/Core.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderPassToggleGuard.h"
#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include <cstdint>
#include <utility>

namespace gte {

using rg::operator""_passId;

namespace {

constexpr rg::RenderPassId kAtmosphereSharedLutKey = "Atmosphere.SharedLuts"_passId;
constexpr rg::RenderPassId kAtmosphereViewLutGameKey = "Atmosphere.ViewLut.Game"_passId;
constexpr rg::RenderPassId kAtmosphereViewLutSceneKey = "Atmosphere.ViewLut.Scene"_passId;
constexpr rg::RenderPassId kGameSkyBackgroundCallbackKey = "Atmosphere.GameSkyBackgroundCallback"_passId;

// "AtmosphereSharedLut"'s own blackboard payload.
struct AtmosphereSharedLutBlackboardEntry {
    AtmosphereSharedLutHandles handles;
    AtmosphereParametersGpu parameters;
};

} // namespace

AtmosphereFeature::AtmosphereFeature(Core& core)
    : m_core(core)
    , m_toggleRegistry(&core.GetRenderPassToggleRegistryMutable())
    , m_settings()
    , m_renderer()
{
    RegisterPasses();
}

void AtmosphereFeature::RegisterPasses()
{
    // "AtmosphereSharedLut" - ProviderScope::Once, BeforeEverything.
    m_core.RegisterProjectRenderPassProvider("AtmosphereSharedLut", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            AtmosphereSharedLutBlackboardEntry entry;
            entry.parameters = MakeDefaultEarthAtmosphereParameters();
            entry.parameters.groundAlbedo = entry.parameters.groundAlbedo * m_settings.groundAlbedoTint;
            entry.handles = AddAtmosphereSharedLutPasses(
                frame.builder, m_core.GetRenderer(), m_renderer, entry.parameters, m_toggleRegistry);

            frame.finalTextureOutputs.push_back(entry.handles.transmittanceLutHandle);
            frame.finalTextureOutputs.push_back(entry.handles.multiScatteringLutHandle);

            frame.blackboard.Publish<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey, entry);
        });

    // "AtmosphereViewLut" - ProviderScope::PerActiveView, PreOpaques.
    m_core.RegisterProjectRenderPassProvider("AtmosphereViewLut", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(frame.currentView);
            if (!viewData.has_value()) {
                return;
            }

            const std::optional<AtmosphereSharedLutBlackboardEntry> sharedLuts =
                frame.blackboard.Fetch<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey);
            if (!sharedLuts.has_value()) {
                return;
            }

            const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
            const char* skyViewLutName = isGameView ? "AtmosphereSkyViewLut_GameView" : "AtmosphereSkyViewLut_SceneView";
            const char* aerialVolumeName =
                isGameView ? "AtmosphereAerialPerspectiveVolume_GameView" : "AtmosphereAerialPerspectiveVolume_SceneView";
            const rg::ViewScope legacyViewScope = isGameView ? rg::ViewScope::GameView : rg::ViewScope::SceneView;

            AtmosphereViewLutHandles viewLuts = AddAtmosphereViewLutPasses(frame.builder, m_core.GetRenderer(),
                m_renderer, m_core.GetGame().GetRegistry(), sharedLuts->parameters, m_settings,
                sharedLuts->handles, viewData->eyeWorldPosition, viewData->viewProjection, skyViewLutName,
                aerialVolumeName, legacyViewScope, m_toggleRegistry);

            frame.finalTextureOutputs.push_back(viewLuts.skyViewLutHandle);
            frame.builder.KeepVolumeTextureOutput(viewLuts.aerialPerspectiveVolumeHandle);

            // See editor-core-separation-20's own fix (RenderPassToggleGuard.h
            // precedent): the volume handle can be invalid this frame if an
            // upstream LUT pass is disabled, even with a stale view-state entry
            // from an earlier frame - guard before reading it.
            if (isGameView && viewLuts.aerialPerspectiveVolumeHandle.IsValid()) {
                const rg::TextureHandle debugSlice = m_renderer.AddAerialPerspectiveVolumeDebugSlicePass(
                    frame.builder, m_core.GetRenderer(), viewLuts.aerialPerspectiveVolumeHandle, aerialVolumeName,
                    static_cast<std::uint32_t>(m_settings.aerialPerspectiveDebugSliceIndex),
                    "AtmosphereAerialPerspectiveVolumeDebugSlice", rg::ViewScope::GameView, m_toggleRegistry);
                frame.finalTextureOutputs.push_back(debugSlice);
            }

            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            frame.blackboard.Publish<AtmosphereViewLutHandles>(viewLutKey, viewLuts);
        });

    // "DrawSkyBackground" - ProviderScope::PerActiveView, AfterOpaques.
    m_core.RegisterProjectRenderPassProvider("DrawSkyBackground", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            // Early toggle guard - see RenderPassToggleGuard.h's own doc
            // comment: must run before any side effect (the blackboard
            // publish below), not merely before out.push_back().
            if (!rg::ShouldDeclareBuiltInPassThisFrame(m_toggleRegistry, "DrawSkyBackground")) {
                return;
            }
            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(frame.currentView);
            if (!viewData.has_value()) {
                return;
            }

            const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            const std::optional<AtmosphereViewLutHandles> viewLuts =
                frame.blackboard.Fetch<AtmosphereViewLutHandles>(viewLutKey);
            const std::optional<AtmosphereSharedLutBlackboardEntry> sharedLuts =
                frame.blackboard.Fetch<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey);
            if (!viewLuts.has_value() || !sharedLuts.has_value() || !viewLuts->skyViewLutHandle.IsValid()) {
                return;
            }

            const char* skyViewLutName = isGameView ? "AtmosphereSkyViewLut_GameView" : "AtmosphereSkyViewLut_SceneView";
            const std::function<void(VkCommandBuffer)> recordSkyBackground =
                MakeRecordSkyBackgroundCallback(m_renderer, m_core.GetRenderer(), viewData->viewProjection,
                    sharedLuts->parameters, viewLuts->frameUniforms, skyViewLutName, m_settings.skyExposure);

            if (isGameView) {
                frame.blackboard.Publish<std::function<void(VkCommandBuffer)>>(
                    kGameSkyBackgroundCallbackKey, recordSkyBackground);
            }

            if (!recordSkyBackground) {
                return;
            }

            const rg::TextureHandle viewTarget = viewData->colorTarget;

            rg::RenderPassDesc desc;
            desc.debugName = "DrawSkyBackground";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::AfterOpaques;
            desc.view = frame.currentView;
            desc.legacyCategory = rg::RenderPassCategory::General;
            desc.drawKind = rg::RenderPassDrawKind::DrawQuad;
            desc.setup = [viewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(viewTarget);
                pass.WriteDepthStencilAttachment(viewTarget);
            };
            desc.execute = [this, recordSkyBackground](rg::PassContext& ctx) {
                m_core.GetRenderer().BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                recordSkyBackground(ctx.cmd);
                m_core.GetRenderer().EndGraphPassRecording();
            };
            out.push_back(std::move(desc));
        });

    // "AtmosphereComposite" - ProviderScope::PerActiveView, AfterTransparents,
    // ProviderTiming::AfterDeferredPasses.
    m_core.RegisterProjectRenderPassProvider("AtmosphereComposite", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(frame.currentView);
            if (!viewData.has_value() || viewData->renderTexture == nullptr) {
                return;
            }

            // This provider calls frame.builder.AddRenderPass() (via
            // AddAtmosphereCompositePass()) directly and never reaches
            // RenderPipeline::DeclareOnePhase()'s own flush loop at all, so it
            // must consult the toggle registry itself.
            const bool atmosphereCompositeEnabled =
                m_toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereComposite");
            if (!atmosphereCompositeEnabled) {
                return;
            }

            const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            const std::optional<AtmosphereViewLutHandles> viewLuts =
                frame.blackboard.Fetch<AtmosphereViewLutHandles>(viewLutKey);
            if (!viewLuts.has_value() || !viewLuts->aerialPerspectiveVolumeHandle.IsValid()) {
                return;
            }

            const char* aerialVolumeName =
                isGameView ? "AtmosphereAerialPerspectiveVolume_GameView" : "AtmosphereAerialPerspectiveVolume_SceneView";
            const char* outputTextureName = isGameView ? "GameViewComposited" : "SceneViewComposited";
            const rg::ViewScope legacyViewScope = isGameView ? rg::ViewScope::GameView : rg::ViewScope::SceneView;

            const rg::TextureHandle composited = AddAtmosphereCompositePass(frame.builder, m_core.GetRenderer(),
                m_renderer, *viewData->renderTexture, viewData->colorTarget, viewLuts->aerialPerspectiveVolumeHandle,
                aerialVolumeName, viewLuts->frameUniforms, viewData->eyeWorldPosition,
                m_settings.aerialPerspectiveStrength, m_settings.aerialPerspectiveMaxDistanceKm,
                m_settings.aerialPerspectiveDepthExponent, viewData->renderTexture->Extent(), outputTextureName,
                legacyViewScope, m_toggleRegistry);

            if (!composited.IsValid()) {
                // This view's upstream volume is valid but the composite pass
                // itself was individually toggled off this frame - publish
                // nothing, so FindPluginRenderFeatureTarget()/consumers fall
                // back to the raw, pre-composite view target instead of an
                // invalid handle.
                return;
            }

            frame.finalTextureOutputs.push_back(composited);

            Core::ViewCompositedOutputEntry entry;
            entry.handle = composited;
            RenderTexture* compositedTexture = m_renderer.CompositedOutput(outputTextureName);
            entry.sampler = compositedTexture != nullptr ? compositedTexture->Sampler() : VK_NULL_HANDLE;
            frame.blackboard.Publish<Core::ViewCompositedOutputEntry>(
                Core::ViewCompositedOutputKey(isGameView), entry);
        },
        rg::ProviderTiming::AfterDeferredPasses);

    // AtmosphereComposite's own finalize-for-sampling step, one hook per view
    // output name.
    m_core.RegisterFinalizeForSamplingHook("GameViewComposited", [this](VkCommandBuffer cmd) -> RenderTexture* {
        m_renderer.FinalizeAerialPerspectiveCompositeForSampling(cmd, "GameViewComposited");
        return m_renderer.CompositedOutput("GameViewComposited");
    });
    m_core.RegisterFinalizeForSamplingHook("SceneViewComposited", [this](VkCommandBuffer cmd) -> RenderTexture* {
        m_renderer.FinalizeAerialPerspectiveCompositeForSampling(cmd, "SceneViewComposited");
        return m_renderer.CompositedOutput("SceneViewComposited");
    });
}

void RegisterAtmosphereFeature(Core& core)
{
    // Not currently called anywhere - EditorHost owns AtmosphereFeature
    // directly via std::make_unique<AtmosphereFeature>(m_core) instead (see
    // EditorHost.cpp). Kept so a future bootstrap path (e.g. a real external
    // .dll's own exported entry point) can wire this feature up with a single
    // call, without needing to manage the instance's own lifetime itself.
    // Intentionally leaks for the life of the process - the same guarantee
    // every other "always on, no teardown" registration in this engine
    // already has.
    static AtmosphereFeature* const instance = new AtmosphereFeature(core);
    (void)instance;
}

} // namespace gte
