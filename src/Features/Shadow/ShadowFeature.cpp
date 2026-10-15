#include "ShadowFeature.h"

#include "ShadowMath.h"
#include "../../Core/Core.h"
#include "../../Core/Logging.h"
#include "../../Core/PixelFormat.h"
#include "../../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../Core/Plugins/RenderFeatureCameraData.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Game/SceneQuery.h"
#include "../../Application/RenderPassViewData.h"
#include "../../Renderer/RenderGraph/FullscreenPass.h"
#include "../../Renderer/RenderGraph/MeshPass.h"
#include "../../Renderer/PipelineHandle.h"
#include "../../Renderer/RenderGraph/RenderFeatureScope.h"
#include "../../Renderer/RenderGraph/RenderPassToggleGuard.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gte {

using rg::operator""_passId;

namespace {
// Indexed by rg::ViewScope (Shared, GameView, SceneView); the Shared entry is unused.
constexpr rg::ViewPair<rg::RenderPassId> kShadowMapKey{
    { rg::RenderPassId{}, "Shadow.Map.Game"_passId, "Shadow.Map.Scene"_passId } };
constexpr rg::ViewPair<rg::RenderPassId> kShadowMaskKey{
    { rg::RenderPassId{}, "Shadow.Mask.Game"_passId, "Shadow.Mask.Scene"_passId } };
constexpr rg::ViewPair<const char*> kShadowMapViewName{ { nullptr, "Shadow.Map.GameView", "Shadow.Map.SceneView" } };
constexpr rg::ViewPair<const char*> kShadowMaskName{ { nullptr, "Shadow.Mask.GameView", "Shadow.Mask.SceneView" } };

constexpr std::int32_t kShadowDepthPassPriority = 10;
constexpr std::int32_t kShadowCompositePriority = -500;

// Push block of ShadowComposite.frag.
struct CompositePush {
    float strength;
};
static_assert(sizeof(CompositePush) == 4);

struct ShadowMapBlackboardEntry {
    rg::TextureHandle shadowMapHandle;
    Mat4 lightSpaceViewProjection;
};

// Output of the shadow mask pass, stored per view.
struct ShadowMaskBlackboardEntry {
    rg::TextureHandle maskHandle;
};
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

// Builds the depth-only pipelines once. A failed build is rolled back and retried with back-off, never abandoned.
void ShadowFeature::EnsureDepthPipelinesBuilt(Renderer& renderer, std::uint64_t frame)
{
    if (m_depthPipelinesBuilt || !m_depthPipelineGate.ShouldAttempt(frame)) {
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
        if (m_depthPipelineGate.OnFailure(frame) == rg::BuildRetryGate::FailureKind::First) {
            GTE_LOG_WARNING(kShadowFeatureName,
                std::string("Depth pipeline build failed ('") + e.what() + "'). Rolled back; retrying with back-off.");
        }
        return;
    }

    if (m_depthPipelineGate.OnSuccess()) {
        GTE_LOG_INFO(kShadowFeatureName, "Depth pipelines built after an earlier failure.");
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
            if (!rg::ShouldDeclareBuiltInPassThisFrame(
                    &m_core.GetRenderPassToggleRegistryMutable(), "Shadow.DepthPass.Draw")) {
                return;
            }
            // Shadow supports the Game and Scene views only.
            const rg::ViewScope scope = TranslateLegacyViewScope(currentView);
            if (scope == rg::ViewScope::Shared) {
                return;
            }
            // Group all shadow passes under the feature name in debug views.
            const rg::RenderFeatureScope featureScope(builder, kShadowFeatureName);
            const std::optional<RenderPassViewData> viewData = m_core.FindRenderPassViewData(currentView);
            if (!viewData.has_value()) {
                return;
            }

            Renderer& renderer = m_core.GetRenderer();
            EnsureDepthPipelinesBuilt(renderer, builder.FrameIndex());

            // Lock shadow map resolution on first use.
            if (!m_mapResolutionLocked) {
                m_mapResolutionInUse = std::max<std::uint32_t>(1u, m_settings.mapResolution);
                m_mapResolutionLocked = true;
            } else if (m_settings.mapResolution != m_mapResolutionInUse && !m_mapResolutionMismatchWarned) {
                GTE_LOG_WARNING(kShadowFeatureName,
                    "ShadowSettings::mapResolution changed after the shadow map's first frame - "
                    "the view is fixed-size for its whole process lifetime. Still using the original "
                    "resolution this session; restart to apply a new one. This warning is logged once.");
                m_mapResolutionMismatchWarned = true;
            }

            const ShadowSettings safeSettings = SanitizeShadowSettings(m_settings);

            const char* viewName = kShadowMapViewName.For(scope);

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
                    const rg::SceneRecording recording(ctx);
                    SceneDrawRequest request;
                    request.viewProjection = lightViewProj;
                    request.pipelineOverrideSet = m_depthPipelines;
                    gte::DrawScene(m_core.GetGame().GetRenderSystem(), m_core.GetRegistry(), m_core.GetRenderer(), request);
                },
                rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);

            builder.KeepTextureOutput(shadowHandle);

            blackboard.Publish<ShadowMapBlackboardEntry>(
                kShadowMapKey.For(scope), ShadowMapBlackboardEntry{ shadowHandle, lightViewProj });

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
            if (!rg::ShouldDeclareBuiltInPassThisFrame(
                    &m_core.GetRenderPassToggleRegistryMutable(), "Shadow.Mask.Dispatch")) {
                return;
            }
            const rg::ViewScope scope = TranslateLegacyViewScope(currentView);
            if (scope == rg::ViewScope::Shared) {
                return;
            }
            const std::optional<ShadowMapBlackboardEntry> shadowMap =
                blackboard.Fetch<ShadowMapBlackboardEntry>(kShadowMapKey.For(scope));
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

            const ShadowSettings safeSettings = SanitizeShadowSettings(m_settings);
            ShadowMaskInputs maskInputs;
            maskInputs.outputName = kShadowMaskName.For(scope);
            maskInputs.viewScope = scope;
            maskInputs.viewExtent = viewData->renderTexture->Extent();
            maskInputs.invViewProjection = realCameraData.invViewProjection;
            maskInputs.lightViewProjection = shadowMap->lightSpaceViewProjection;
            maskInputs.depthBias = safeSettings.depthBias;
            maskInputs.shadowMapResolution = m_mapResolutionInUse;
            const rg::TextureHandle maskHandle = m_maskRenderer.AddMaskPass(
                builder, maskInputs, shadowMap->shadowMapHandle, currentViewHandles.depthHandle);
            if (!maskHandle.IsValid()) {
                return;
            }

            builder.KeepTextureOutput(maskHandle);
            blackboard.Publish<ShadowMaskBlackboardEntry>(
                kShadowMaskKey.For(scope), ShadowMaskBlackboardEntry{ maskHandle });
        });
    assert(maskRegistered && "Shadow.Mask registration failed");

    // PostComposite pass: composites the shadow mask over the color buffer.
    const bool compositeRegistered = m_core.RegisterRenderPass(FrameEvent::PostComposite, "Shadow.Composite",
        kShadowCompositePriority,
        [this](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
            rg::TextureHandle privateTarget, Extent2D /*extent*/, const ScenePassReadHandles& currentViewHandles,
            const RenderFeatureCameraData&) {
            if (!rg::ShouldDeclareBuiltInPassThisFrame(
                    &m_core.GetRenderPassToggleRegistryMutable(), "Shadow.Composite.Draw")) {
                return;
            }
            const rg::ViewScope scope = TranslateLegacyViewScope(currentView);
            if (scope == rg::ViewScope::Shared) {
                return;
            }
            const std::optional<ShadowMaskBlackboardEntry> mask =
                blackboard.Fetch<ShadowMaskBlackboardEntry>(kShadowMaskKey.For(scope));
            if (!mask.has_value() || !mask->maskHandle.IsValid()) {
                return;
            }

            const CompositePush push{ SanitizeShadowSettings(m_settings).strength };
            const rg::RenderFeatureScope featureScope(builder, kShadowFeatureName);
            rg::FullscreenPass(builder, m_compositeShader, "Shadow.Composite.Draw", scope)
                .Bind("sceneColor", currentViewHandles.colorHandle)
                .Bind("shadowMask", mask->maskHandle)
                .Push(push)
                .Acknowledge(currentViewHandles.depthHandle)
                .Target(privateTarget)
                .Draw();
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

GTE_REGISTER_BUILTIN_FEATURE_MODULE(kShadowFeatureName, &CreateShadowFeatureModule);

} // namespace gte
