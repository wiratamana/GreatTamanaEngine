#include "ShadowFeature.h"

#include "ShadowMath.h"
#include "../../Core/Core.h"
#include "../../Core/Logging.h"
#include "../../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../Core/Plugins/RenderFeatureCameraData.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Game/SceneQuery.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"

#include <algorithm>
#include <cassert>
#include <memory>

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

void ShadowFeature::EnsureDepthPipelineBuilt(Renderer& renderer)
{
    if (m_depthPipeline.IsValid()) {
        return;
    }
    // Empty colorFormats span == depth-only Pipeline.
    m_depthPipeline = m_core.GetGame().GetRenderSystem().RegisterPipeline(
        renderer.CreatePipeline(std::span<const VkFormat>{}, "shaders/ShadowDepth.vert.spv",
            "shaders/ShadowDepth.frag.spv", VertexLayout::PositionNormal, /*useMaterialTexture=*/false,
            "ShadowDepth.vert/.frag (PositionNormal, depth-only)"));
}

void ShadowFeature::RegisterPasses()
{
    // "Shadow.DepthPass" - PreOpaque.
    const bool depthRegistered = m_core.AddPreOpaquePass(
        "Shadow.DepthPass",
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView) {
            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(currentView);
            if (!viewData.has_value()) {
                return; // No per-view data yet this frame - safe no-op.
            }

            Renderer& renderer = m_core.GetRenderer();
            EnsureDepthPipelineBuilt(renderer);

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
                    // Only reaches VertexLayout::PositionNormal meshes -
                    // RenderSystem::Draw()'s guard skips the rest.
                    Renderer& renderer = m_core.GetRenderer();
                    renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                    SceneDrawRequest request;
                    request.viewProjection = lightViewProj; // Real light-space matrix, never Identity.
                    request.pipelineOverride = m_depthPipeline;
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

    // "Shadow.Mask" - PostOpaque, AfterOpaques.
    const bool maskRegistered = m_core.AddPostOpaquePass(
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

    // "Shadow.Composite" - PostComposite, RenderFeatureBlendMode::ScreenSpaceMask.
    const bool compositeRegistered = m_core.RegisterProjectRenderFeature("Shadow.Composite",
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
