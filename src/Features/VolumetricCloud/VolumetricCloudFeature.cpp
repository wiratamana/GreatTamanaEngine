#include "VolumetricCloudFeature.h"

#include "VolumetricCloudMath.h"
#include "../../Core/PixelFormat.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 49;
constexpr std::int32_t kCompositePriority = -900; // After the aerial composite, so sky pixels are final.

} // namespace

VolumetricCloudFeature::VolumetricCloudFeature(fx::FeatureHost&) { }

void VolumetricCloudFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a toggle from freezing the frame clock the snapshots age against.
    s.PreOpaque("VolumetricCloud.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); }, fx::Gate::Always());

    // The secondary view skips the clouds by default.
    s.DefaultSecondaryViewEnabled(false);

    s.PostComposite("VolumetricCloud", kCompositePriority, fx::Blend::Replace,
        [this](fx::CompositeFrame& f) { CompositePass(f); },
        fx::Gate{ "VolumetricCloud.March.Dispatch", "VolumetricCloud.Debug.Dispatch",
            "VolumetricCloud.CopyToPrivateTarget" });
}

void VolumetricCloudFeature::ApplySettings(const VolumetricCloudSettings& candidate)
{
    m_settings = SanitizeVolumetricCloudSettings(candidate);
}

VolumetricCloudViewSnapshot VolumetricCloudFeature::Snapshot(rg::ViewScope scope) const
{
    VolumetricCloudViewSnapshot snapshot = m_views.For(scope).snapshot;
    if (fx::SnapshotIsStale(snapshot.frameIndex, m_newestFrame)) {
        snapshot.status = VolumetricCloudFrameStatus::NotRendered;
    }
    return snapshot;
}

fx::Texture VolumetricCloudFeature::AcquireDebugSource(fx::CompositeFrame& f)
{
    switch (m_settings.debugView) {
    case VolumetricCloudDebugView::ShapeSlice: return m_shape.Acquire(f);
    case VolumetricCloudDebugView::DetailSlice: return m_detail.Acquire(f);
    case VolumetricCloudDebugView::WeatherMap: return m_weather.Acquire(f);
    case VolumetricCloudDebugView::BlueNoise: return m_blueNoise.Acquire(f);
    case VolumetricCloudDebugView::Off: break;
    }
    return fx::Texture{};
}

bool VolumetricCloudFeature::RunSliceView(
    fx::CompositeFrame& f, fx::Texture output, VolumetricCloudViewSnapshot& snapshot)
{
    if (!f.PassEnabled("VolumetricCloud.Debug.Dispatch")) {
        snapshot.status = VolumetricCloudFrameStatus::PassDisabled;
        return false;
    }
    const bool volumeView = m_settings.debugView == VolumetricCloudDebugView::ShapeSlice
        || m_settings.debugView == VolumetricCloudDebugView::DetailSlice;
    if (!f.ShaderReady(volumeView ? m_slice3dShader : m_slice2dShader)) {
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
        return false;
    }
    const fx::Texture source = AcquireDebugSource(f);
    if (!source.IsValid()) {
        snapshot.status = VolumetricCloudFrameStatus::AssetMissing;
        return false;
    }
    const VolumetricCloudSlicePush push = MakeVolumetricCloudSlicePush(m_settings);
    const bool dispatched = volumeView ? f.Compute(m_slice3dShader, "VolumetricCloud.Debug.Dispatch")
                                             .Push(push)
                                             .Bind("noiseVolume", source)
                                             .Bind("destinationImage", output)
                                             .Dispatch(f.Extent())
                                       : f.Compute(m_slice2dShader, "VolumetricCloud.Debug.Dispatch")
                                             .Push(push)
                                             .Bind("noiseTexture", source)
                                             .Bind("destinationImage", output)
                                             .Dispatch(f.Extent());
    if (!dispatched) {
        f.ReportOnce("VolumetricCloud.Debug.Dispatch was refused");
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
    }
    return dispatched;
}

bool VolumetricCloudFeature::RunMarch(fx::CompositeFrame& f, VolumetricCloudViewState& view, fx::Texture output,
    VolumetricCloudViewSnapshot& snapshot)
{
    if (!f.PassEnabled("VolumetricCloud.March.Dispatch")) {
        snapshot.status = VolumetricCloudFrameStatus::PassDisabled;
        return false;
    }
    if (!f.ShaderReady(m_marchShader)) {
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
        return false;
    }
    const fx::Texture shape = m_shape.Acquire(f);
    const fx::Texture detail = m_detail.Acquire(f);
    const fx::Texture weather = m_weather.Acquire(f);
    const fx::Texture blueNoise = m_blueNoise.Acquire(f);
    if (!shape.IsValid() || !detail.IsValid() || !weather.IsValid() || !blueNoise.IsValid()) {
        snapshot.status = VolumetricCloudFrameStatus::AssetMissing;
        return false;
    }

    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
    const std::optional<SunLightModulation> modulation = m_sunLight.Fetch(f);
    const SunLightModulation light = modulation ? SanitizeSunLightModulation(*modulation) : SunLightModulation{};

    AdvanceCloudWind(view.windOffset, m_settings.windSpeed, m_settings.windDirection, f.DeltaSeconds());
    VolumetricCloudFrameFacts facts;
    facts.invViewProjection = f.View().invViewProjection;
    facts.eye = f.View().eye;
    facts.sunDirection = sun.directionTowardSun;
    facts.sunIlluminance = sun.sunIlluminance;
    facts.sunTint = light.sunTint;
    facts.ambientScale = light.ambientScale;
    facts.windOffset[0] = view.windOffset[0];
    facts.windOffset[1] = view.windOffset[1];
    facts.frameIndex = f.FrameIndex();

    const bool dispatched = f.Compute(m_marchShader, "VolumetricCloud.March.Dispatch")
                                .Params(view.params, MakeVolumetricCloudParams(m_settings, facts))
                                .Bind("sourceColor", f.SceneColor())
                                .Bind("sourceDepth", fx::Depth(f.SceneDepth()))
                                .Bind("shapeVolume", shape)
                                .Bind("detailVolume", detail)
                                .Bind("weatherMap", weather)
                                .Bind("blueNoise", blueNoise)
                                .Bind("destinationImage", output)
                                .Dispatch(f.Extent());
    if (!dispatched) {
        f.ReportOnce("VolumetricCloud.March.Dispatch was refused");
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
    }
    return dispatched;
}

void VolumetricCloudFeature::CompositePass(fx::CompositeFrame& f)
{
    // Every return below that is not Ok declares nothing, so the compositor skips this entry.
    VolumetricCloudViewState& view = m_views.For(f.Scope());
    VolumetricCloudViewSnapshot& snapshot = view.snapshot;
    snapshot.frameIndex = f.FrameIndex();

    if (!m_settings.enabled) {
        snapshot.status = VolumetricCloudFrameStatus::Disabled;
        return;
    }
    if (!f.View().invViewProjectionValid) {
        snapshot.status = VolumetricCloudFrameStatus::NoValidCamera;
        return;
    }
    if (!f.PassEnabled("VolumetricCloud.CopyToPrivateTarget")) {
        snapshot.status = VolumetricCloudFrameStatus::PassDisabled;
        return;
    }
    const fx::Texture output = m_output.Acquire(f, f.Extent(), PixelFormat::RGBA16_Float);
    if (!output.IsValid()) {
        snapshot.status = VolumetricCloudFrameStatus::TargetFailed;
        return;
    }

    const bool sliceView = m_settings.debugView != VolumetricCloudDebugView::Off;
    if (!(sliceView ? RunSliceView(f, output, snapshot) : RunMarch(f, view, output, snapshot))) {
        return;
    }
    snapshot.status = VolumetricCloudFrameStatus::Ok;
    f.ExposeAsViewOutput(m_output, output);
    f.Blit("VolumetricCloud.CopyToPrivateTarget", output, f.PrivateTarget(), fx::BlitFilter::Linear);
}

GTE_REGISTER_RENDER_FEATURE("VolumetricCloud", VolumetricCloudFeature);

} // namespace gte
