#include "ShadowFeature.h"

#include "../../Core/Logging.h"
#include "../../Core/PixelFormat.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

#include <algorithm>

namespace gte {

namespace {
constexpr std::int32_t kDepthPassPriority = 10;
constexpr std::int32_t kMaskPriority = 0;
constexpr std::int32_t kCompositePriority = -500;
} // namespace

ShadowFeature::ShadowFeature(fx::FeatureHost&) { }

void ShadowFeature::Setup(fx::FeatureSetup& s)
{
    // The gate names the pass shown in the Render Graph panel; a disabled gate skips the whole callback.
    s.PreOpaque("Shadow.DepthPass", kDepthPassPriority, [this](fx::PreOpaqueFrame& f) { DepthPass(f); },
        fx::Gate{ "Shadow.DepthPass.Draw" });
    s.PostOpaque("Shadow.Mask", kMaskPriority, [this](fx::PostOpaqueFrame& f) { MaskPass(f); },
        fx::Gate{ "Shadow.Mask.Dispatch" });
    s.PostComposite("Shadow.Composite", kCompositePriority, fx::Blend::ScreenSpaceMask,
        [this](fx::CompositeFrame& f) { CompositePass(f); }, fx::Gate{ "Shadow.Composite.Draw" });
}

std::uint32_t ShadowFeature::LockedMapResolution(const ShadowSettings& settings)
{
    if (m_mapResolution == 0) {
        m_mapResolution = std::max<std::uint32_t>(1u, settings.mapResolution);
    } else if (settings.mapResolution != m_mapResolution && !m_resolutionWarned) {
        m_resolutionWarned = true;
        GTE_LOG_WARNING(kShadowFeatureName, "mapResolution changed after the first frame; the map keeps its size until restart.");
    }
    return m_mapResolution;
}

void ShadowFeature::DepthPass(fx::PreOpaqueFrame& f)
{
    const ShadowSettings cfg = SanitizeShadowSettings(m_settings);
    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
    const Mat4 lightViewProjection = BuildDirectionalShadowViewProjection(
        sun.directionTowardSun, f.View().eye, cfg.orthoHalfExtentWorld, cfg.nearZ, cfg.farZ);

    ShadowViewState& view = m_views.For(f.Scope());
    const fx::Texture map = view.map.Acquire(f, fx::DepthOnly{ LockedMapResolution(m_settings) });
    if (!map.IsValid()) {
        return;
    }

    const bool drawn = f.SceneDraw("Shadow.DepthPass.Draw")
                           .Shaders(m_depthShaders)
                           .Camera(lightViewProjection)
                           .ClearDepthTarget(map, 1.0f)
                           .Record();
    if (!drawn) {
        return; // Shaders not ready: no map this frame, so the mask and composite stay out.
    }
    m_mapData.Publish(f, ShadowMapData{ map, lightViewProjection });
}

void ShadowFeature::MaskPass(fx::PostOpaqueFrame& f)
{
    const std::optional<ShadowMapData> map = m_mapData.Fetch(f);
    if (!map || !f.View().invViewProjectionValid) {
        return;
    }
    ShadowViewState& view = m_views.For(f.Scope());
    const fx::Texture mask = view.mask.Acquire(f, f.View().extent, PixelFormat::R8_UNorm);
    if (!mask.IsValid()) {
        return;
    }

    const ShadowSettings cfg = SanitizeShadowSettings(m_settings);
    const ShadowMaskParamsGpu params =
        MakeShadowMaskParams(f.View().invViewProjection, map->lightViewProjection, cfg.depthBias, m_mapResolution);
    const bool dispatched = f.Compute(m_maskShader, "Shadow.Mask.Dispatch")
                                .Params(view.params, params)
                                .Bind("shadowDepth", fx::Depth(map->texture))
                                .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                                .Bind("maskOutput", mask)
                                .Dispatch(f.View().extent);
    if (dispatched) {
        m_maskData.Publish(f, mask);
    }
}

void ShadowFeature::CompositePass(fx::CompositeFrame& f)
{
    const std::optional<fx::Texture> mask = m_maskData.Fetch(f);
    if (!mask) {
        return;
    }
    f.Fullscreen(m_compositeShader, "Shadow.Composite.Draw")
        .Bind("sceneColor", f.SceneColor())
        .Bind("shadowMask", *mask)
        .Push(CompositeParams{ SanitizeShadowSettings(m_settings).strength })
        .Acknowledge(f.SceneDepth())
        .Target(f.PrivateTarget())
        .Draw();
}

GTE_REGISTER_RENDER_FEATURE("Shadow", ShadowFeature);

} // namespace gte
