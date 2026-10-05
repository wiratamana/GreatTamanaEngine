#include "AtmosphereFeature.h"

#include "AtmosphereMath.h"
#include "AtmosphereParameters.h"
#include "AtmospherePassSequence.h"
#include "../../Core/Core.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderPassToggleGuard.h"
#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include <cassert>
#include <cstdint>
#include <utility>

namespace gte {

using rg::operator""_passId;

namespace {

constexpr rg::RenderPassId kAtmosphereSharedLutKey = "Atmosphere.SharedLuts"_passId;
constexpr rg::RenderPassId kAtmosphereViewLutGameKey = "Atmosphere.ViewLut.Game"_passId;
constexpr rg::RenderPassId kAtmosphereViewLutSceneKey = "Atmosphere.ViewLut.Scene"_passId;
constexpr rg::RenderPassId kGameSkyBackgroundCallbackKey = "Atmosphere.GameSkyBackgroundCallback"_passId;

// Mirrors Core.cpp's own identical, independently-declared copy of this
// same literal - its Frame Debugger replay dispatch fetches this exact key.
constexpr rg::RenderPassId kGameSkyBackgroundReplayCallbackKey = "Core.GameSkyBackgroundReplayCallback"_passId;

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
            AtmosphereParametersGpu params;
            params.rayleighScattering = m_settings.rayleighScattering;
            params.rayleighDensityExpScale = ReciprocalScaleHeightFromKm(m_settings.rayleighScaleHeightKm);
            params.mieScattering = m_settings.mieScattering;
            params.miePhaseG = m_settings.miePhaseG;
            params.mieAbsorption = m_settings.mieAbsorption;
            params.mieDensityExpScale = ReciprocalScaleHeightFromKm(m_settings.mieScaleHeightKm);
            params.ozoneAbsorption = m_settings.ozoneAbsorption;
            params.ozoneTentCenterKm = m_settings.ozoneTentCenterKm;
            params.groundAlbedo = m_settings.groundAlbedo;
            params.ozoneTentHalfWidthKm = m_settings.ozoneTentHalfWidthKm;
            params.planetRadiusKm = m_settings.planetRadiusKm;
            params.atmosphereThicknessKm = m_settings.atmosphereThicknessKm;
            params.multiScatteringStrength = m_settings.multiScatteringStrength;
            entry.parameters = params;
            entry.handles = AddAtmosphereSharedLutPasses(
                frame.builder, m_core.GetRenderer(), m_renderer, entry.parameters, m_toggleRegistry);

            frame.finalTextureOutputs.push_back(entry.handles.transmittanceLutHandle);
            frame.finalTextureOutputs.push_back(entry.handles.multiScatteringLutHandle);

            frame.blackboard.Publish<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey, entry);
        });

    // "AtmosphereViewLut" - PreOpaque, via Core::AddPreOpaquePass(). No other
    // PreOpaque feature exists in a real session today, so priority 0 has
    // nothing to collide with - confirmed by inspection, not assumed.
    static constexpr std::int32_t kAtmosphereViewLutPriority = 0;
    const bool viewLutRegistered = m_core.AddPreOpaquePass("AtmosphereViewLut",
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView) {
            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(currentView);
            if (!viewData.has_value()) {
                return;
            }

            const std::optional<AtmosphereSharedLutBlackboardEntry> sharedLuts =
                blackboard.Fetch<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey);
            if (!sharedLuts.has_value()) {
                return;
            }

            const bool isGameView = (currentView == rg::RenderViewId::Named("Game"));
            const char* skyViewLutName = isGameView ? "AtmosphereSkyViewLut_GameView" : "AtmosphereSkyViewLut_SceneView";
            const char* aerialVolumeName =
                isGameView ? "AtmosphereAerialPerspectiveVolume_GameView" : "AtmosphereAerialPerspectiveVolume_SceneView";
            const rg::ViewScope legacyViewScope = isGameView ? rg::ViewScope::GameView : rg::ViewScope::SceneView;

            AtmosphereViewLutHandles viewLuts = AddAtmosphereViewLutPasses(builder, m_core.GetRenderer(),
                m_renderer, m_core.GetGame().GetRegistry(), sharedLuts->parameters, m_settings,
                sharedLuts->handles, viewData->eyeWorldPosition, viewData->viewProjection, skyViewLutName,
                aerialVolumeName, legacyViewScope, m_toggleRegistry);

            // A PreOpaque callback has no RenderPassFrameContext::finalTextureOutputs
            // of its own - KeepTextureOutput() is the builder-level equivalent.
            builder.KeepTextureOutput(viewLuts.skyViewLutHandle);
            builder.KeepVolumeTextureOutput(viewLuts.aerialPerspectiveVolumeHandle);

            // The volume handle can be invalid this frame if an upstream LUT
            // pass is disabled, even with a stale view-state entry from an
            // earlier frame - guard before reading it.
            if (isGameView && viewLuts.aerialPerspectiveVolumeHandle.IsValid()) {
                const rg::TextureHandle debugSlice = m_renderer.AddAerialPerspectiveVolumeDebugSlicePass(
                    builder, m_core.GetRenderer(), viewLuts.aerialPerspectiveVolumeHandle, aerialVolumeName,
                    static_cast<std::uint32_t>(m_settings.aerialPerspectiveDebugSliceIndex),
                    "AtmosphereAerialPerspectiveVolumeDebugSlice", rg::ViewScope::GameView, m_toggleRegistry);
                builder.KeepTextureOutput(debugSlice);
            }

            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            blackboard.Publish<AtmosphereViewLutHandles>(viewLutKey, viewLuts);
        },
        kAtmosphereViewLutPriority);
    assert(viewLutRegistered && "AtmosphereViewLut registration failed - see the GTE_LOG_WARNING above.");

    // "DrawSkyBackground" - ProviderScope::PerActiveView, AfterOpaques.
    // PERMANENT EXCEPTION: writes directly into the view's own shared
    // color+depth target, which no RenderFeatureStage contract allows.
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
                frame.blackboard.Publish<std::function<void(VkCommandBuffer)>>(
                    kGameSkyBackgroundReplayCallbackKey, recordSkyBackground);
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

    // "AtmosphereComposite" - PostComposite, via
    // Core::RegisterProjectRenderFeature(). Reserved priority, deliberately
    // far below any Project Assembly's auto-assigned screen post-process
    // priority (ScreenPostProcessPassPriorityAssignment.h) - always runs
    // first among PostComposite features, mirroring its own effective
    // ordering today.
    static constexpr std::int32_t kAtmosphereCompositePriority = -1000;
    const bool compositeRegistered = m_core.RegisterProjectRenderFeature("AtmosphereComposite",
        RenderFeatureStage::PostComposite, RenderFeatureBlendMode::AlphaOver, kAtmosphereCompositePriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle privateTarget, VkExtent2D extent, const ScenePassReadHandles& currentViewHandles,
            const RenderFeatureCameraData& cameraData) {
            if (!cameraData.invViewProjectionValid) {
                return; // Nothing safe to compute this frame for this view.
            }

            // This callback reaches `builder` directly, bypassing
            // RenderPipeline::DeclareOnePhase()'s own toggle choke point -
            // must consult the registry itself.
            if (!m_toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereComposite")) {
                return;
            }

            const bool isGameView = (currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            const std::optional<AtmosphereViewLutHandles> viewLuts =
                blackboard.Fetch<AtmosphereViewLutHandles>(viewLutKey);
            if (!viewLuts.has_value() || !viewLuts->aerialPerspectiveVolumeHandle.IsValid()) {
                return;
            }

            const char* aerialVolumeName =
                isGameView ? "AtmosphereAerialPerspectiveVolume_GameView" : "AtmosphereAerialPerspectiveVolume_SceneView";
            const char* outputTextureName = isGameView ? "GameViewComposited" : "SceneViewComposited";
            const rg::ViewScope legacyViewScope = isGameView ? rg::ViewScope::GameView : rg::ViewScope::SceneView;

            const rg::TextureHandle composited = AddAtmosphereCompositePass(builder, m_core.GetRenderer(),
                m_renderer, currentViewHandles.colorHandle, currentViewHandles.colorSampler,
                currentViewHandles.depthHandle, currentViewHandles.depthImageView, currentViewHandles.depthSampler,
                viewLuts->aerialPerspectiveVolumeHandle, aerialVolumeName, viewLuts->frameUniforms,
                cameraData.eyeWorldPosition, m_settings.aerialPerspectiveStrength,
                m_settings.aerialPerspectiveMaxDistanceKm, m_settings.aerialPerspectiveDepthExponent, extent,
                outputTextureName, legacyViewScope, m_toggleRegistry);

            if (!composited.IsValid()) {
                return; // Individually toggled off this frame - nothing to copy.
            }

            // "GameViewComposited"/"SceneViewComposited" stay this feature's
            // own persistent, separately-named output - the screenshot
            // bridge/Editor panels/purity validator keep reading it exactly
            // as before. The generic blend chain gets its own copy via one
            // small additional blit into privateTarget.
            builder.KeepTextureOutput(composited);

            rg::BlitSpec spec;
            spec.src = composited;
            spec.dst = privateTarget;
            builder.AddBlitPass(
                "AtmosphereComposite.CopyToPrivateTarget", spec, rg::RenderPassEvent::AfterEverything, legacyViewScope);
        });
    assert(compositeRegistered && "AtmosphereComposite registration failed - see the GTE_LOG_WARNING above.");

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
