#include "AtmosphereFeature.h"

#include "AtmosphereMath.h"
#include "AtmosphereParameters.h"
#include "AtmospherePassSequence.h"
#include "../../Core/Core.h"
#include "../../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../Core/ViewBackgroundReplayKey.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../../Renderer/RenderGraph/CommandBuffer.h"

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
    // "AtmosphereSharedLut" - a PreOpaque BUILT-IN FEATURE, registered
    // through the universal Core::RegisterRenderPass() front door
    // (migration scaffold - see chat history). FrameEvent::PreOpaque
    // routes straight into RenderFeatureCompositor::RegisterBuiltInPreOpaqueFeature(),
    // UNCHANGED - same real engine machinery "AtmosphereViewLut" below and
    // Shadow's "Shadow.DepthPass" also use. Still invoked
    // ProviderScope::PerActiveView under the hood - Core's own
    // "PreOpaqueFeatures" provider calls EVERY registered PreOpaque feature
    // once per active view, in the SAME frame, against the SAME
    // RenderPassBlackboard (see ProjectPreOpaqueCallback.h's own doc
    // comment) - but the shared transmittance/multi-scattering LUTs this
    // pass builds are view-independent and must only be computed ONCE per
    // frame. Guarded below: whichever view happens to run first this frame
    // does the real work and Publish()es; every later view this same frame
    // just Fetches the already-published entry and returns immediately,
    // declaring no pass of its own - safe because `blackboard` really is the
    // same instance across every per-view invocation this frame.
    //
    // Priority -10, strictly BEFORE "AtmosphereViewLut" (priority 0, same
    // PreOpaque stage, same per-view priority-ordered loop) - ViewLut's own
    // callback Fetches this entry out of the blackboard and needs it
    // already published AND already declared into `builder`
    // (RenderGraphCompiler resolves a reader's dependency only against a
    // writer already SEEN earlier in declaration order - see
    // RenderPassFrameContext's own doc comment, RenderPipeline.h).
    //
    // privateTarget/extent/currentViewHandles/cameraData below are
    // PostOpaque-or-later-only fields - always safe, invalid placeholders
    // here for FrameEvent::PreOpaque (see UniversalRenderPassCallback.h).
    // This callback never reads any of the 4, by design.
    static constexpr std::int32_t kAtmosphereSharedLutPriority = -10;
    const bool sharedLutRegistered = m_core.RegisterRenderPass(FrameEvent::PreOpaque, "AtmosphereSharedLut",
        kAtmosphereSharedLutPriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle /*privateTarget*/, Extent2D /*extent*/,
            const ScenePassReadHandles& /*currentViewHandles*/, const RenderFeatureCameraData& /*cameraData*/) {
            (void)currentView; // View-independent - see this registration's own doc comment above.
            if (blackboard.Fetch<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey).has_value()) {
                return; // Already built by an earlier view THIS SAME FRAME - nothing to redo.
            }

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
                builder, m_core.GetRenderer(), m_renderer, entry.parameters, m_toggleRegistry);

            // A PreOpaque callback has no RenderPassFrameContext::finalTextureOutputs
            // of its own - KeepTextureOutput() is the builder-level equivalent (see
            // "AtmosphereViewLut"'s own identical comment just below).
            builder.KeepTextureOutput(entry.handles.transmittanceLutHandle);
            builder.KeepTextureOutput(entry.handles.multiScatteringLutHandle);

            blackboard.Publish<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey, entry);
        });
    assert(sharedLutRegistered && "AtmosphereSharedLut registration failed - see the GTE_LOG_WARNING above.");

    // "AtmosphereViewLut" - PreOpaque, built-in engine feature, now via the
    // universal Core::RegisterRenderPass() front door (migration scaffold -
    // see chat history). FrameEvent::PreOpaque routes straight into
    // RenderFeatureCompositor::RegisterBuiltInPreOpaqueFeature() (reports
    // isProjectFeature == false in DebugSnapshot()/GET /render_graph - this
    // is the engine's own pass, not a Project Assembly's). Priority 0 sits
    // strictly AFTER "AtmosphereSharedLut" (priority -10, registered
    // immediately above - this pass's own
    // Fetch<AtmosphereSharedLutBlackboardEntry>() call below depends on it
    // having already published THIS frame) and strictly BEFORE Shadow's
    // "Shadow.DepthPass" (priority 10, ShadowFeature.cpp, same PreOpaque
    // stage) - confirmed by inspection, not assumed.
    //
    // privateTarget/extent/cameraData below are PostComposite/PreUI-only
    // fields - always safe, invalid placeholders here (see
    // UniversalRenderPassCallback.h). currentViewHandles is ALSO a
    // placeholder for PreOpaque specifically (nothing has been drawn yet) -
    // this callback never reads any of the 4, by design.
    static constexpr std::int32_t kAtmosphereViewLutPriority = 0;
    const bool viewLutRegistered = m_core.RegisterRenderPass(FrameEvent::PreOpaque, "AtmosphereViewLut",
        kAtmosphereViewLutPriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle /*privateTarget*/, Extent2D /*extent*/,
            const ScenePassReadHandles& /*currentViewHandles*/, const RenderFeatureCameraData& /*cameraData*/) {
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

            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            blackboard.Publish<AtmosphereViewLutHandles>(viewLutKey, viewLuts);
        });
    assert(viewLutRegistered && "AtmosphereViewLut registration failed - see the GTE_LOG_WARNING above.");

    // "DrawSkyBackground" - REHAULED onto the universal
    // Core::RegisterRenderPass() front door (FrameEvent::PostOpaque), per
    // explicit instruction. This pass still writes directly into the view's
    // own shared color+depth target (never a private target) - that part
    // of its contract is unchanged and is exactly what FrameEvent::PostOpaque
    // allows (unlike PostComposite/PreUI, which only ever hands out a
    // private target). The thing that actually blocked this conversion
    // earlier was a real gap in Core's own safety-net check
    // (FindScenePassCallbackMissingReadDeclaration, RenderPassTagValidation.h)
    // - it only recognized an explicit ReadTexture() as a "declared
    // dependency," so a depth-tested in-place write (this pass's real
    // technique: fill in only the pixels the depth-EQUAL test leaves
    // untouched) tripped it as a false positive. FIXED properly, not
    // bypassed: RenderGraphBuilder::PassWritesTextureAsDepthReadWrite() (new)
    // + FindScenePassCallbackMissingReadDeclaration() (updated) now also
    // recognize WriteDepthStencilAttachment() itself as a real, honest
    // dependency declaration - which it always was
    // (ResourceAccess::DepthStencilAttachmentReadWrite already encodes a
    // combined read+write, confirmed in RenderGraphBarrierPlanner.cpp). This
    // benefits every future PostOpaque feature using the same technique, not
    // just this one - the safety net is now MORE accurate, not weaker.
    //
    // The old manual `rg::ShouldDeclareBuiltInPassThisFrame` toggle guard is
    // GONE - redundant now, not removed carelessly: Core's own
    // "PostOpaqueFeatures" provider already skips this entire callback
    // (blackboard Publish() side-channel included) whenever "DrawSkyBackground"
    // is disabled, confirmed by reading Core.cpp directly - the exact same
    // protection FrameDebuggerSideChannelChecker.cpp's documented,
    // previously-fixed bug
    // (PHASE1_FIX_DRAWSKYBACKGROUND_TOGGLE_SIDE_CHANNEL_LEAK.md) needed,
    // just provided automatically instead of by hand.
    static constexpr std::int32_t kDrawSkyBackgroundPriority = 100; // Distinct from Shadow.Mask's default (0) in the same PostOpaque stage - no real ordering dependency between them, just avoids a harmless log-only collision warning.
    const bool skyBackgroundRegistered = m_core.RegisterRenderPass(FrameEvent::PostOpaque, "DrawSkyBackground",
        kDrawSkyBackgroundPriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle /*privateTarget*/, Extent2D /*extent*/, const ScenePassReadHandles& /*currentViewHandles*/,
            const RenderFeatureCameraData& /*cameraData*/) {
            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(currentView);
            if (!viewData.has_value()) {
                return;
            }

            const bool isGameView = (currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            const std::optional<AtmosphereViewLutHandles> viewLuts =
                blackboard.Fetch<AtmosphereViewLutHandles>(viewLutKey);
            const std::optional<AtmosphereSharedLutBlackboardEntry> sharedLuts =
                blackboard.Fetch<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey);
            if (!viewLuts.has_value() || !sharedLuts.has_value() || !viewLuts->skyViewLutHandle.IsValid()) {
                return;
            }

            const char* skyViewLutName = isGameView ? "AtmosphereSkyViewLut_GameView" : "AtmosphereSkyViewLut_SceneView";
            const std::function<void(rg::CommandBuffer&)> recordSkyBackground =
                MakeRecordSkyBackgroundCallback(m_renderer, m_core.GetRenderer(), m_core.GetRenderGraph(),
                    viewData->viewProjection, sharedLuts->parameters, viewLuts->frameUniforms, skyViewLutName,
                    m_settings.skyExposure);

            if (isGameView) {
                // Core.cpp/FrameDebuggerCapture.cpp's replay/debug side
                // channel travels as the SAME engine-owned
                // std::function<void(rg::CommandBuffer&)> shape
                // `recordSkyBackground` already is - published directly, no
                // extra per-publish wrapping needed (Core::
                // DispatchFinalizeForSamplingHook()/FrameDebuggerCaptureContext::
                // AddReplayPasses() each wrap their own raw VkCommandBuffer
                // into an rg::CommandBuffer themselves, right at the one spot
                // that actually owns it). Safe to publish unconditionally
                // here - see this registration's own doc comment above for
                // why Core's own toggle gating already covers this side
                // channel.
                blackboard.Publish<std::function<void(rg::CommandBuffer&)>>(
                    kGameSkyBackgroundCallbackKey, recordSkyBackground);
                blackboard.Publish<std::function<void(rg::CommandBuffer&)>>(
                    kViewBackgroundReplayCallbackKey, recordSkyBackground);
            }

            if (!recordSkyBackground) {
                return;
            }

            const rg::TextureHandle viewTarget = viewData->colorTarget;

            builder.AddRenderPass(
                "DrawSkyBackground", rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::General,
                [viewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
                    pass.WriteColorAttachment(viewTarget);
                    pass.WriteDepthStencilAttachment(viewTarget);
                },
                [this, recordSkyBackground](rg::PassContext& ctx) {
                    m_core.GetRenderer().BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                    rg::CommandBuffer cmd = ctx.Cmd();
                    recordSkyBackground(cmd);
                    m_core.GetRenderer().EndGraphPassRecording();
                },
                rg::RenderPassDrawKind::DrawQuad, rg::RenderPassEvent::AfterOpaques);
        });
    assert(skyBackgroundRegistered && "DrawSkyBackground registration failed - see the GTE_LOG_WARNING above.");

    // Folds "DrawSkyBackground" into Core's generic view-content GPU stats.
    m_core.RegisterViewContentPassName("DrawSkyBackground");

    // "AtmosphereComposite" - built-in PostComposite feature, now registered
    // through the universal Core::RegisterRenderPass() front door
    // (migration scaffold - see chat history). FrameEvent::PostComposite
    // routes straight into RenderFeatureCompositor::RegisterBuiltInFeature() -
    // same real engine machinery, same safety asserts, this is
    // purely a different call shape. Reserved priority, deliberately far
    // below any Project Assembly's own auto-assigned screen post-process
    // priority - always runs first among PostComposite features.
    static constexpr std::int32_t kAtmosphereCompositePriority = -1000;
    const bool compositeRegistered = m_core.RegisterRenderPass(FrameEvent::PostComposite, "AtmosphereComposite",
        kAtmosphereCompositePriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle privateTarget, Extent2D extent, const ScenePassReadHandles& currentViewHandles,
            const RenderFeatureCameraData& cameraData) {
            if (!cameraData.invViewProjectionValid) {
                return; // Nothing safe to compute this frame for this view.
            }

            // Gating already happens in RenderFeatureCompositor::ContributeRenderGraphPasses()
            // via IsEffectivelyEnabled() - no local registry write needed here.

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
                currentViewHandles.depthHandle, currentViewHandles.depthImageView,
                currentViewHandles.depthSampler, viewLuts->aerialPerspectiveVolumeHandle, aerialVolumeName,
                viewLuts->frameUniforms, cameraData.eyeWorldPosition, m_settings.aerialPerspectiveStrength,
                m_settings.aerialPerspectiveMaxDistanceKm, m_settings.aerialPerspectiveDepthExponent,
                extent, outputTextureName, legacyViewScope, m_toggleRegistry);

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
        },
        RenderFeatureBlendMode::AlphaOver);
    assert(compositeRegistered && "AtmosphereComposite registration failed - see the GTE_LOG_WARNING above.");

    // AtmosphereComposite's own finalize-for-sampling step, one hook per view
    // output name. FreshCompositedOutput() reports nullptr on a stale/
    // disabled frame instead of replaying a leftover cached texture. `cmd`
    // arrives already wrapped as an engine-owned rg::CommandBuffer - Core::
    // DispatchFinalizeForSamplingHook() wraps its own raw VkCommandBuffer
    // once, right there, before calling this hook.
    m_core.RegisterFinalizeForSamplingHook("GameViewComposited", [this](rg::CommandBuffer& cmd) -> RenderTexture* {
        m_renderer.FinalizeAerialPerspectiveCompositeForSampling(cmd, "GameViewComposited");
        return FreshCompositedOutput("GameViewComposited");
    });
    m_core.RegisterFinalizeForSamplingHook("SceneViewComposited", [this](rg::CommandBuffer& cmd) -> RenderTexture* {
        m_renderer.FinalizeAerialPerspectiveCompositeForSampling(cmd, "SceneViewComposited");
        return FreshCompositedOutput("SceneViewComposited");
    });
}

// Returns outputTextureName's output, or nullptr if the render graph did
// not write it this frame - reuses RenderGraph's own debug-texture frame
// counter, so a disabled/stale pass reports honestly instead of replaying
// a leftover cached texture forever.
RenderTexture* AtmosphereFeature::FreshCompositedOutput(const char* outputTextureName)
{
    RenderTexture* output = m_renderer.CompositedOutput(outputTextureName);
    if (output == nullptr) {
        return nullptr;
    }
    const rg::RenderGraph& graph = m_core.GetRenderGraph();
    const std::optional<rg::DebugTextureSnapshot> snapshot = graph.DebugTextureSnapshotFor(outputTextureName);
    const bool freshThisFrame =
        snapshot.has_value() && snapshot->lastUpdatedFrameCounter == graph.CurrentDebugTextureFrameCounter();
    return freshThisFrame ? output : nullptr;
}

namespace {
std::unique_ptr<IEngineFeatureModule> CreateAtmosphereFeatureModule(Core& core)
{
    return std::make_unique<AtmosphereFeature>(core);
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_MODULE("Atmosphere", &CreateAtmosphereFeatureModule);

} // namespace gte
