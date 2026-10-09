#include "ShadowFeature.h"

#include "ShadowMath.h"
#include "../../Core/Core.h"
#include "../../Core/Logging.h"
#include "../../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../Core/Plugins/RenderFeatureCameraData.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Game/SceneQuery.h"
#include "../../Renderer/RenderGraph/RenderFeatureScope.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <vector>

namespace gte {

using rg::operator""_passId;

namespace {
constexpr rg::RenderPassId kShadowMapGameKey = "Shadow.Map.Game"_passId;
constexpr rg::RenderPassId kShadowMapSceneKey = "Shadow.Map.Scene"_passId;

// Distinct from AtmosphereViewLut's priority 0 in the same PreOpaque stage.
constexpr std::int32_t kShadowDepthPassPriority = 10;

// Never 0 (collides with AddScreenPostProcessPass()'s auto-priority
// counter), never -1000 (AtmosphereComposite's slot). Uniqueness against
// every other PostComposite feature is enforced by review only - verify
// manually via the Render Graph panel before shipping.
constexpr std::int32_t kShadowCompositePriority = -500;

struct ShadowMapBlackboardEntry {
    rg::TextureHandle shadowMapHandle;
    Mat4 lightSpaceViewProjection;
};

// Shadow.Mask's own output, keyed per view.
struct ShadowMaskBlackboardEntry {
    rg::TextureHandle maskHandle;
};
constexpr rg::RenderPassId kShadowMaskGameKey = "Shadow.Mask.Game"_passId;
constexpr rg::RenderPassId kShadowMaskSceneKey = "Shadow.Mask.Scene"_passId;
} // namespace

ShadowFeature::ShadowFeature(Core& core)
    : m_core(core)
    , m_settings()
{
    RegisterPasses();
}

// Builds one depth-only Pipeline per supported vertex layout
// (PositionColor/PositionNormal/PositionNormalUv). Not transactional across
// repeated calls: on a build failure, rolls back only what THIS attempt
// inserted (ResourcePool slots from earlier, already-succeeded frames are
// untouched), then backs off for kRetryCooldownFrames before trying again,
// giving up permanently after kMaxBuildAttempts total failures.
void ShadowFeature::EnsureDepthPipelinesBuilt(Renderer& renderer)
{
    constexpr std::uint32_t kRetryCooldownFrames = 120; // ~2s at 60 FPS.
    constexpr std::uint32_t kMaxBuildAttempts = 5;

    if (m_depthPipelinesBuilt || m_depthPipelineGaveUp) {
        return;
    }

    if (m_depthPipelineRetryCooldownFramesRemaining > 0) {
        --m_depthPipelineRetryCooldownFramesRemaining;
        return; // Still cooling down from the last failed attempt.
    }

    RenderSystem& renderSystem = m_core.GetGame().GetRenderSystem();
    std::vector<PipelineHandle> insertedThisAttempt;

    auto build = [&](VertexLayout layout, const char* vert, const char* debugName) {
        const PipelineHandle handle = renderSystem.RegisterPipeline(
            renderer.CreatePipeline(std::span<const VkFormat>{}, vert, "shaders/ShadowDepth.frag.spv", layout,
                /*useMaterialTexture=*/false, debugName));
        insertedThisAttempt.push_back(handle);
        m_depthPipelines.byLayout[static_cast<std::size_t>(layout)] = handle;
    };

    try {
        build(VertexLayout::PositionColor, "shaders/ShadowDepthPositionColor.vert.spv",
            "ShadowDepthPositionColor.vert/ShadowDepth.frag (PositionColor, depth-only)");
        build(VertexLayout::PositionNormal, "shaders/ShadowDepthPositionNormal.vert.spv",
            "ShadowDepthPositionNormal.vert/ShadowDepth.frag (PositionNormal, depth-only)");
        build(VertexLayout::PositionNormalUv, "shaders/ShadowDepthPositionNormalUv.vert.spv",
            "ShadowDepthPositionNormalUv.vert/ShadowDepth.frag (PositionNormalUv, depth-only)");
        // PositionNormalInstanced intentionally left unset - GPU-driven
        // batched entities cast through the brute-force PositionNormal path.
    } catch (const std::exception& e) {
        for (const PipelineHandle handle : insertedThisAttempt) {
            renderSystem.UnregisterPipeline(handle);
        }
        m_depthPipelines = PipelineOverrideSet{}; // Drop any half-built entries too.
        ++m_depthPipelineFailedAttempts;

        if (m_depthPipelineFailedAttempts >= kMaxBuildAttempts) {
            m_depthPipelineGaveUp = true;
            GTE_LOG_WARNING("Shadow", std::string("EnsureDepthPipelinesBuilt(): pipeline build failed ('") +
                e.what() + "') after " + std::to_string(m_depthPipelineFailedAttempts) +
                " attempts - giving up. Depth casting stays disabled for the rest of this process.");
            return;
        }

        m_depthPipelineRetryCooldownFramesRemaining = kRetryCooldownFrames;
        GTE_LOG_WARNING("Shadow", std::string("EnsureDepthPipelinesBuilt(): pipeline build failed ('") + e.what() +
            "') - rolled back this attempt's pipelines. Retrying in " + std::to_string(kRetryCooldownFrames) +
            " frames (attempt " + std::to_string(m_depthPipelineFailedAttempts) + "/" +
            std::to_string(kMaxBuildAttempts) + ").");
        return; // m_depthPipelinesBuilt stays false - retried after cooldown, no leak accumulates meanwhile.
    }

    m_depthPipelinesBuilt = true;
}

void ShadowFeature::RegisterPasses()
{
    // "Shadow.DepthPass" - PreOpaque, built-in engine feature, via
    // Core::AddBuiltInPreOpaquePass() (reports isProjectFeature == false in
    // DebugSnapshot()/GET /render_graph - this is the engine's own pass).
    const bool depthRegistered = m_core.AddBuiltInPreOpaquePass(
        "Shadow.DepthPass",
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView) {
            // Shares one literal "Shadow" heading across all 3 of this
            // feature's own stages (depth pass, mask, composite) - each
            // registered under its own distinct feature name, so the
            // generic per-registration-name scope alone would otherwise
            // split them into 3 separate sidebar headings.
            const rg::RenderFeatureScope scope(builder, "Shadow");
            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(currentView);
            if (!viewData.has_value()) {
                return; // No per-view data yet this frame - safe no-op.
            }

            Renderer& renderer = m_core.GetRenderer();
            EnsureDepthPipelinesBuilt(renderer);

            // Lock mapResolution on first use - see ShadowTypes.h. Never 0.
            if (!m_mapResolutionLocked) {
                m_mapResolutionInUse = std::max<std::uint32_t>(1u, m_settings.mapResolution);
                m_mapResolutionLocked = true;
            } else if (m_settings.mapResolution != m_mapResolutionInUse && !m_mapResolutionMismatchWarned) {
                GTE_LOG_WARNING("Shadow",
                    "ShadowSettings::mapResolution changed after the shadow map's first frame - "
                    "the view is fixed-size for its whole process lifetime. Still using the original "
                    "resolution this session; restart to apply a new one. This warning is logged once.");
                m_mapResolutionMismatchWarned = true;
            }

            const ShadowSettings safeSettings = SanitizeShadowSettings(m_settings);

            const bool isGameView = (currentView == rg::RenderViewId::Named("Game"));
            const char* viewName = isGameView ? "Shadow.Map.GameView" : "Shadow.Map.SceneView";

            const rg::RenderViewId shadowView = m_core.CreateRenderView(viewName, m_mapResolutionInUse,
                m_mapResolutionInUse, /*depthOnly=*/true, /*allowDepthSampledAccess=*/true);
            RenderTexture* shadowTarget = m_core.FindRenderViewTarget(shadowView);
            if (shadowTarget == nullptr) {
                return;
            }

            const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(m_core.GetRegistry());

            // A fixed-size box centered on THIS VIEW's own camera eye
            // position - not a hardcoded world point. A box fixed at the
            // origin would produce zero shadows for any scene not authored
            // near (0,0,0); this still does not frustum-fit (sharp shadows
            // may alias for casters far from the camera).
            const Mat4 lightViewProj = BuildDirectionalShadowViewProjection(sun.directionTowardSun,
                viewData->eyeWorldPosition, safeSettings.orthoHalfExtentWorld, safeSettings.nearZ, safeSettings.farZ);

            const rg::TextureHandle shadowHandle = builder.ImportTexture(
                viewName, shadowTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED,
                /*colorSampler=*/VK_NULL_HANDLE, /*depthSampler=*/shadowTarget->DepthSampler());

            builder.AddRenderPass(
                "Shadow.DepthPass.Draw", rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::General,
                [shadowHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                    pass.WriteDepthStencilAttachment(shadowHandle, /*clearDepth=*/1.0f);
                },
                [this, lightViewProj](rg::PassContext& ctx) {
                    Renderer& renderer = m_core.GetRenderer();
                    renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                    SceneDrawRequest request;
                    request.viewProjection = lightViewProj; // Real light-space matrix, never Identity.
                    request.pipelineOverrideSet = m_depthPipelines;
                    gte::DrawScene(m_core.GetGame().GetRenderSystem(), m_core.GetRegistry(), renderer, request);
                    renderer.EndGraphPassRecording();
                },
                rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);

            builder.KeepTextureOutput(shadowHandle);

            const rg::RenderPassId key = isGameView ? kShadowMapGameKey : kShadowMapSceneKey;
            blackboard.Publish<ShadowMapBlackboardEntry>(key, ShadowMapBlackboardEntry{ shadowHandle, lightViewProj });
        },
        kShadowDepthPassPriority);
    assert(depthRegistered && "Shadow.DepthPass registration failed - see the GTE_LOG_WARNING above.");

    // "Shadow.Mask" - PostOpaque, AfterOpaques, built-in engine feature, via
    // Core::AddBuiltInPostOpaquePass() (reports isProjectFeature == false in
    // DebugSnapshot()/GET /render_graph - this is the engine's own pass).
    const bool maskRegistered = m_core.AddBuiltInPostOpaquePass(
        "Shadow.Mask",
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            const ScenePassReadHandles& currentViewHandles) {
            const bool isGameView = (currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId mapKey = isGameView ? kShadowMapGameKey : kShadowMapSceneKey;
            const std::optional<ShadowMapBlackboardEntry> shadowMap =
                blackboard.Fetch<ShadowMapBlackboardEntry>(mapKey);
            if (!shadowMap.has_value() || !shadowMap->shadowMapHandle.IsValid()) {
                return; // Shadow.DepthPass didn't run this frame - nothing to mask.
            }

            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(currentView);
            if (!viewData.has_value()) {
                return;
            }
            const RenderFeatureCameraData cameraData = ResolveRenderFeatureCameraData(currentView, &(*viewData));
            if (!cameraData.invViewProjectionValid) {
                return; // Degenerate camera matrix this frame - never divide by garbage.
            }

            RenderTexture* shadowTarget = m_core.FindRenderViewTarget(
                rg::RenderViewId::Named(isGameView ? "Shadow.Map.GameView" : "Shadow.Map.SceneView"));
            if (shadowTarget == nullptr) {
                return;
            }

            const char* maskName = isGameView ? "Shadow.Mask.GameView" : "Shadow.Mask.SceneView";
            const ShadowSettings safeSettings = SanitizeShadowSettings(m_settings);
            const rg::TextureHandle maskHandle = m_maskRenderer.AddMaskPass(builder, m_core.GetRenderer(), maskName,
                viewData->renderTexture->Extent(), cameraData.invViewProjection, shadowMap->lightSpaceViewProjection,
                safeSettings.depthBias, m_mapResolutionInUse, shadowMap->shadowMapHandle, shadowTarget->DepthSampler(),
                currentViewHandles.depthHandle, currentViewHandles.depthImageView, currentViewHandles.depthSampler);
            if (!maskHandle.IsValid()) {
                return;
            }

            builder.KeepTextureOutput(maskHandle);

            const rg::RenderPassId maskKey = isGameView ? kShadowMaskGameKey : kShadowMaskSceneKey;
            blackboard.Publish<ShadowMaskBlackboardEntry>(maskKey, ShadowMaskBlackboardEntry{ maskHandle });
        });
    assert(maskRegistered && "Shadow.Mask registration failed - see the GTE_LOG_WARNING above.");

    // "Shadow.Composite" - built-in PostComposite feature, RenderFeatureBlendMode::ScreenSpaceMask.
    const bool compositeRegistered = m_core.RegisterBuiltInRenderFeature("Shadow.Composite",
        RenderFeatureStage::PostComposite, RenderFeatureBlendMode::ScreenSpaceMask, kShadowCompositePriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle privateTarget, VkExtent2D extent, const ScenePassReadHandles& currentViewHandles,
            const RenderFeatureCameraData&) {
            const bool isGameView = (currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId maskKey = isGameView ? kShadowMaskGameKey : kShadowMaskSceneKey;
            const std::optional<ShadowMaskBlackboardEntry> mask =
                blackboard.Fetch<ShadowMaskBlackboardEntry>(maskKey);
            if (!mask.has_value() || !mask->maskHandle.IsValid()) {
                return; // Shadow.Mask didn't run this frame - nothing to darken with.
            }

            const ShadowSettings safeSettings = SanitizeShadowSettings(m_settings);
            m_compositeRenderer.AddCompositePass(builder, m_core.GetRenderer(), isGameView ? "Game" : "Scene",
                privateTarget, extent, currentViewHandles.colorHandle, currentViewHandles.colorSampler,
                mask->maskHandle, currentViewHandles.depthHandle, safeSettings.strength);
        });
    assert(compositeRegistered && "Shadow.Composite registration failed - see the GTE_LOG_WARNING above.");
}

namespace {
std::unique_ptr<IEngineFeatureModule> CreateShadowFeatureModule(Core& core)
{
    return std::make_unique<ShadowFeature>(core);
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_MODULE("Shadow", &CreateShadowFeatureModule);

} // namespace gte
