#include "ShadowFeature.h"

#include "ShadowMath.h"
#include "../../Core/Core.h"
#include "../../Core/Logging.h"
#include "../../Core/PixelFormat.h"
#include "../../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../Core/Plugins/RenderFeatureCameraData.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Game/SceneQuery.h"
#include "../../Renderer/RenderGraph/RenderFeatureScope.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace gte {

using rg::operator""_passId;

namespace {
constexpr rg::RenderPassId kShadowMapGameKey = "Shadow.Map.Game"_passId;
constexpr rg::RenderPassId kShadowMapSceneKey = "Shadow.Map.Scene"_passId;

constexpr std::int32_t kShadowDepthPassPriority = 10;
constexpr std::int32_t kShadowCompositePriority = -500;

struct ShadowMapBlackboardEntry {
    rg::TextureHandle shadowMapHandle;
    Mat4 lightSpaceViewProjection;
};

// Output of the shadow mask pass, stored per view.
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
    // Register the depth shadow map in Scene Services slot 0 for material shaders.
    m_shadowMapSlot = RegisterSceneServiceSlot(
        "ShadowMap", SceneServiceResourceKind::Image2D, /*preferredIndex=*/0, /*isDepthResource=*/true);
    RegisterPasses();
}

// Compiles depth-only pipelines for supported vertex layouts with retry on failure.
void ShadowFeature::EnsureDepthPipelinesBuilt(Renderer& renderer)
{
    constexpr std::uint32_t kRetryCooldownFrames = 120;
    constexpr std::uint32_t kMaxBuildAttempts = 5;

    if (m_depthPipelinesBuilt || m_depthPipelineGaveUp) {
        return;
    }

    if (m_depthPipelineRetryCooldownFramesRemaining > 0) {
        --m_depthPipelineRetryCooldownFramesRemaining;
        return;
    }

    RenderSystem& renderSystem = m_core.GetGame().GetRenderSystem();
    std::vector<PipelineHandle> insertedThisAttempt;

    auto build = [&](VertexLayout layout, const char* vert, const char* debugName) {
        const PipelineHandle handle = renderSystem.RegisterPipeline(
            renderer.CreatePipeline(std::span<const PixelFormat>{}, vert, "shaders/ShadowDepth.frag.spv", layout,
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
    } catch (const std::exception& e) {
        for (const PipelineHandle handle : insertedThisAttempt) {
            renderSystem.UnregisterPipeline(handle);
        }
        m_depthPipelines = PipelineOverrideSet{};
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
        return;
    }

    m_depthPipelinesBuilt = true;
}

void ShadowFeature::RegisterPasses()
{
    // PreOpaque pass: renders the scene depth from the directional light's perspective.
    const bool depthRegistered = m_core.RegisterRenderPass(FrameEvent::PreOpaque, "Shadow.DepthPass",
        kShadowDepthPassPriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle /*privateTarget*/, Extent2D /*extent*/,
            const ScenePassReadHandles& /*currentViewHandles*/, const RenderFeatureCameraData& /*cameraData*/) {
            // Group all shadow passes under "Shadow" in debug views.
            const rg::RenderFeatureScope scope(builder, "Shadow");
            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(currentView);
            if (!viewData.has_value()) {
                return;
            }

            Renderer& renderer = m_core.GetRenderer();
            EnsureDepthPipelinesBuilt(renderer);

            // Lock shadow map resolution on first use.
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

            // Build directional light view-projection matrix centered on the camera position.
            const Mat4 lightViewProj = BuildDirectionalShadowViewProjection(sun.directionTowardSun,
                viewData->eyeWorldPosition, safeSettings.orthoHalfExtentWorld, safeSettings.nearZ, safeSettings.farZ);

            const rg::TextureHandle shadowHandle = builder.ImportTexture(
                viewName, shadowTarget->Target(), ImageLayout::Undefined,
                /*colorSampler=*/SamplerHandle::Invalid, /*depthSampler=*/shadowTarget->DepthSamplerObjectHandle());

            builder.AddRenderPass(
                "Shadow.DepthPass.Draw", rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::General,
                [shadowHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                    pass.WriteDepthStencilAttachment(shadowHandle, /*clearDepth=*/1.0f);
                },
                [this, lightViewProj](rg::PassContext& ctx) {
                    Renderer& renderer = m_core.GetRenderer();
                    renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                    SceneDrawRequest request;
                    request.viewProjection = lightViewProj;
                    request.pipelineOverrideSet = m_depthPipelines;
                    gte::DrawScene(m_core.GetGame().GetRenderSystem(), m_core.GetRegistry(), renderer, request);
                    renderer.EndGraphPassRecording();
                },
                rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);

            builder.KeepTextureOutput(shadowHandle);

            const rg::RenderPassId key = isGameView ? kShadowMapGameKey : kShadowMapSceneKey;
            blackboard.Publish<ShadowMapBlackboardEntry>(key, ShadowMapBlackboardEntry{ shadowHandle, lightViewProj });

            // Publish shadow map and lighting uniforms for scene material shaders.
            blackboard.Publish<rg::TextureHandle>(SceneServiceBlackboardKey(m_shadowMapSlot, currentView), shadowHandle);

            const SceneLightingUniformData packed =
                PackSceneLightingUniformData(sun, lightViewProj, safeSettings, m_mapResolutionInUse);
            m_core.GetSceneServicesDescriptorSet().UpdateGlobalUniformBlock(
                currentView, std::as_bytes(std::span{ &packed, 1 }));
        });
    assert(depthRegistered && "Shadow.DepthPass registration failed");

    // PostOpaque pass: generates screen-space shadow mask using scene depth and the shadow map.
    static constexpr std::int32_t kShadowMaskPriority = 0;
    const bool maskRegistered = m_core.RegisterRenderPass(FrameEvent::PostOpaque, "Shadow.Mask", kShadowMaskPriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle /*privateTarget*/, Extent2D /*extent*/, const ScenePassReadHandles& currentViewHandles,
            const RenderFeatureCameraData& /*cameraData*/) {
            const bool isGameView = (currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId mapKey = isGameView ? kShadowMapGameKey : kShadowMapSceneKey;
            const std::optional<ShadowMapBlackboardEntry> shadowMap =
                blackboard.Fetch<ShadowMapBlackboardEntry>(mapKey);
            if (!shadowMap.has_value() || !shadowMap->shadowMapHandle.IsValid()) {
                return;
            }

            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(currentView);
            if (!viewData.has_value()) {
                return;
            }
            const RenderFeatureCameraData realCameraData = ResolveRenderFeatureCameraData(currentView, &(*viewData));
            if (!realCameraData.invViewProjectionValid) {
                return;
            }

            RenderTexture* shadowTarget = m_core.FindRenderViewTarget(
                rg::RenderViewId::Named(isGameView ? "Shadow.Map.GameView" : "Shadow.Map.SceneView"));
            if (shadowTarget == nullptr) {
                return;
            }

            const char* maskName = isGameView ? "Shadow.Mask.GameView" : "Shadow.Mask.SceneView";
            const ShadowSettings safeSettings = SanitizeShadowSettings(m_settings);
            const rg::TextureHandle maskHandle = m_maskRenderer.AddMaskPass(builder, m_core.GetRenderer(), maskName,
                viewData->renderTexture->Extent(), realCameraData.invViewProjection,
                shadowMap->lightSpaceViewProjection,
                safeSettings.depthBias, m_mapResolutionInUse, shadowMap->shadowMapHandle,
                shadowTarget->DepthSamplerObjectHandle(), currentViewHandles.depthHandle,
                currentViewHandles.depthImageView, currentViewHandles.depthSampler);
            if (!maskHandle.IsValid()) {
                return;
            }

            builder.KeepTextureOutput(maskHandle);

            const rg::RenderPassId maskKey = isGameView ? kShadowMaskGameKey : kShadowMaskSceneKey;
            blackboard.Publish<ShadowMaskBlackboardEntry>(maskKey, ShadowMaskBlackboardEntry{ maskHandle });
        });
    assert(maskRegistered && "Shadow.Mask registration failed");

    // PostComposite pass: composites the shadow mask over the color buffer.
    const bool compositeRegistered = m_core.RegisterRenderPass(FrameEvent::PostComposite, "Shadow.Composite",
        kShadowCompositePriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle privateTarget, Extent2D extent, const ScenePassReadHandles& currentViewHandles,
            const RenderFeatureCameraData&) {
            const bool isGameView = (currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId maskKey = isGameView ? kShadowMaskGameKey : kShadowMaskSceneKey;
            const std::optional<ShadowMaskBlackboardEntry> mask =
                blackboard.Fetch<ShadowMaskBlackboardEntry>(maskKey);
            if (!mask.has_value() || !mask->maskHandle.IsValid()) {
                return;
            }

            const ShadowSettings safeSettings = SanitizeShadowSettings(m_settings);
            m_compositeRenderer.AddCompositePass(builder, m_core.GetRenderer(), isGameView ? "Game" : "Scene",
                privateTarget, extent, currentViewHandles.colorHandle,
                currentViewHandles.colorSampler, mask->maskHandle, currentViewHandles.depthHandle,
                safeSettings.strength);
        },
        RenderFeatureBlendMode::ScreenSpaceMask);
    assert(compositeRegistered && "Shadow.Composite registration failed");
}

namespace {
std::unique_ptr<IEngineFeatureModule> CreateShadowFeatureModule(Core& core)
{
    return std::make_unique<ShadowFeature>(core);
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_MODULE("Shadow", &CreateShadowFeatureModule);

} // namespace gte
