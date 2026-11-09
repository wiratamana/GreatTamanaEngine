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
        fx::Gate{ "VolumetricCloud.March.Dispatch", "VolumetricCloud.Resolve.Dispatch", "VolumetricCloud.Debug.Dispatch",
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
    if (!f.PassEnabled("VolumetricCloud.March.Dispatch") || !f.PassEnabled("VolumetricCloud.Resolve.Dispatch")) {
        snapshot.status = VolumetricCloudFrameStatus::PassDisabled;
        return false;
    }
    // Ask both before testing either, so a failed build of one does not hide the other.
    const bool marchReady = f.ShaderReady(m_marchShader);
    const bool resolveReady = f.ShaderReady(m_resolveShader);
    if (!marchReady || !resolveReady) {
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

    // Each target is acquired once per frame; a refused Acquire returns an invalid texture.
    const Extent2D full = f.Extent();
    const std::uint32_t divisor = CloudResolutionDivisor(m_settings);
    const Extent2D low = fx::ScaledExtent(full, divisor);
    const std::uint32_t slot = static_cast<std::uint32_t>(f.FrameIndex() & 1u);
    const fx::Texture lowColor = view.lowColor.Acquire(f, low, PixelFormat::RGBA16_Float);
    const fx::Texture lowAux = view.lowAux.Acquire(f, low, PixelFormat::RGBA16_Float);
    const fx::Texture currentColor = view.historyColor[slot].Acquire(f, full, PixelFormat::RGBA16_Float);
    const fx::Texture currentAux = view.historyAux[slot].Acquire(f, full, PixelFormat::RGBA16_Float);
    const fx::Texture previousColor = view.historyColor[slot ^ 1u].Acquire(f, full, PixelFormat::RGBA16_Float);
    const fx::Texture previousAux = view.historyAux[slot ^ 1u].Acquire(f, full, PixelFormat::RGBA16_Float);
    if (!lowColor.IsValid() || !lowAux.IsValid() || !currentColor.IsValid() || !currentAux.IsValid()
        || !previousColor.IsValid() || !previousAux.IsValid()) {
        snapshot.status = VolumetricCloudFrameStatus::TargetFailed;
        return false;
    }

    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
    const std::optional<SunLightModulation> modulation = m_sunLight.Fetch(f);
    const SunLightModulation light = modulation ? SanitizeSunLightModulation(*modulation) : SunLightModulation{};

    view.previousWindOffset[0] = view.windOffset[0];
    view.previousWindOffset[1] = view.windOffset[1];
    AdvanceCloudWind(view.windOffset, m_settings.windSpeed, m_settings.windDirection, f.DeltaSeconds());
    VolumetricCloudFrameFacts facts;
    // Rays and reprojection are formed relative to the eye, in double precision.
    const CloudRelativeCamera camera = MakeCloudRelativeCamera(f.View().viewProjection, f.View().eye);
    if (!camera.valid) {
        snapshot.status = VolumetricCloudFrameStatus::NoValidCamera;
        return false;
    }
    facts.invViewProjection = camera.inverse;
    facts.eye = f.View().eye;
    facts.sunDirection = sun.directionTowardSun;
    facts.sunIlluminance = sun.sunIlluminance;
    facts.sunTint = light.sunTint;
    facts.ambientScale = light.ambientScale;
    facts.windOffset[0] = view.windOffset[0];
    facts.windOffset[1] = view.windOffset[1];
    facts.frameIndex = f.FrameIndex();
    facts.viewWidth = full.width;
    facts.viewHeight = full.height;

    const bool marched = f.Compute(m_marchShader, "VolumetricCloud.March.Dispatch")
                             .Params(view.params, MakeVolumetricCloudParams(m_settings, facts))
                             .Bind("sourceDepth", fx::Depth(f.SceneDepth()))
                             .Bind("shapeVolume", shape)
                             .Bind("detailVolume", detail)
                             .Bind("weatherMap", weather)
                             .Bind("blueNoise", blueNoise)
                             .Bind("lowColorOut", lowColor)
                             .Bind("lowAuxOut", lowAux)
                             .Dispatch(low);
    if (!marched) {
        f.ReportOnce("VolumetricCloud.March.Dispatch was refused");
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
        return false;
    }

    // The history reprojects through the previous unjittered view-projection, which this feature keeps itself.
    // The basis comes from the eye-relative inverse: the engine's own is too coarse far from the origin.
    fx::CameraBasis basis = fx::DeriveCameraBasis(Vec3::Zero(), camera.inverse, full);
    basis.eye = f.View().eye;
    const bool accumulate = m_settings.temporal >= 0.5f;
    VolumetricCloudResolveFacts resolveFacts;
    resolveFacts.prevViewProjection = MakeCloudRelativeCamera(view.previous.viewProjection, f.View().eye).projection;
    resolveFacts.windDelta[0] = view.windOffset[0] - view.previousWindOffset[0];
    resolveFacts.windDelta[1] = view.windOffset[1] - view.previousWindOffset[1];
    resolveFacts.historyValid = accumulate && basis.valid && fx::HistoryUsable(view.previous, basis, f.FrameIndex(), full, divisor);
    resolveFacts.lowWidth = low.width;
    resolveFacts.lowHeight = low.height;
    snapshot.historyUsed = resolveFacts.historyValid;
    snapshot.divisor = divisor;

    const bool resolved = f.Compute(m_resolveShader, "VolumetricCloud.Resolve.Dispatch")
                              .Params(view.resolveParams, MakeVolumetricCloudResolveParams(m_settings, facts, resolveFacts))
                              .Bind("sourceColor", f.SceneColor())
                              .Bind("sourceDepth", fx::Depth(f.SceneDepth()))
                              .Bind("lowColor", lowColor)
                              .Bind("lowAux", lowAux)
                              .Bind("historyColor", previousColor)
                              .Bind("historyAux", previousAux)
                              .Bind("historyColorOut", currentColor)
                              .Bind("historyAuxOut", currentAux)
                              .Bind("destinationImage", output)
                              .Dispatch(full);
    if (!resolved) {
        f.ReportOnce("VolumetricCloud.Resolve.Dispatch was refused");
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
        return false;
    }
    // Only a frame whose resolve pass ran becomes history.
    view.previous = basis.valid ? fx::MakeHistory(basis, f.View().viewProjection, f.FrameIndex(), full, divisor, view.previous)
                                : fx::TemporalHistory{};
    return true;
}

void VolumetricCloudFeature::CompositePass(fx::CompositeFrame& f)
{
    // Every return below that is not Ok declares nothing, so the compositor skips this entry.
    VolumetricCloudViewState& view = m_views.For(f.Scope());
    VolumetricCloudViewSnapshot& snapshot = view.snapshot;
    snapshot.frameIndex = f.FrameIndex();

    // A frame that does not write the history must not leave the last one usable.
    const auto bail = [&](VolumetricCloudFrameStatus status) {
        view.previous = fx::TemporalHistory{};
        snapshot.status = status;
    };

    if (!m_settings.enabled) {
        bail(VolumetricCloudFrameStatus::Disabled);
        return;
    }
    if (!f.View().invViewProjectionValid) {
        bail(VolumetricCloudFrameStatus::NoValidCamera);
        return;
    }
    if (!f.PassEnabled("VolumetricCloud.CopyToPrivateTarget")) {
        bail(VolumetricCloudFrameStatus::PassDisabled);
        return;
    }
    const fx::Texture output = m_output.Acquire(f, f.Extent(), PixelFormat::RGBA16_Float);
    if (!output.IsValid()) {
        bail(VolumetricCloudFrameStatus::TargetFailed);
        return;
    }

    const bool sliceView = m_settings.debugView != VolumetricCloudDebugView::Off;
    if (!(sliceView ? RunSliceView(f, output, snapshot) : RunMarch(f, view, output, snapshot))) {
        view.previous = fx::TemporalHistory{};
        return;
    }
    if (sliceView) {
        view.previous = fx::TemporalHistory{};
    }
    snapshot.status = VolumetricCloudFrameStatus::Ok;
    f.ExposeAsViewOutput(m_output, output);
    f.Blit("VolumetricCloud.CopyToPrivateTarget", output, f.PrivateTarget(), fx::BlitFilter::Linear);
}

GTE_REGISTER_RENDER_FEATURE("VolumetricCloud", VolumetricCloudFeature);

} // namespace gte
