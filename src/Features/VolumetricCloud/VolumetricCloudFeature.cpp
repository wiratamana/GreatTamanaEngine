#include "VolumetricCloudFeature.h"

#include "../../Core/PixelFormat.h"

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 48;
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

    s.PostComposite("VolumetricCloud", kCompositePriority, fx::Blend::AlphaOver,
        [this](fx::CompositeFrame& f) { CompositePass(f); },
        fx::Gate{ "VolumetricCloud.Debug.Dispatch", "VolumetricCloud.Debug.CopyToPrivateTarget" });
}

void VolumetricCloudFeature::ApplySettings(const VolumetricCloudSettings& candidate)
{
    m_settings = SanitizeVolumetricCloudSettings(candidate);
}

VolumetricCloudViewSnapshot VolumetricCloudFeature::Snapshot(rg::ViewScope scope) const
{
    VolumetricCloudViewSnapshot snapshot = m_views.For(scope);
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

void VolumetricCloudFeature::CompositePass(fx::CompositeFrame& f)
{
    // Every return below that is not Ok declares nothing, so the compositor skips this entry.
    VolumetricCloudViewSnapshot& snapshot = m_views.For(f.Scope());
    snapshot.frameIndex = f.FrameIndex();

    if (!m_settings.enabled) {
        snapshot.status = VolumetricCloudFrameStatus::Disabled;
        return;
    }
    if (m_settings.debugView == VolumetricCloudDebugView::Off) {
        snapshot.status = VolumetricCloudFrameStatus::Off;
        return;
    }
    if (!f.PassEnabled("VolumetricCloud.Debug.Dispatch") || !f.PassEnabled("VolumetricCloud.Debug.CopyToPrivateTarget")) {
        snapshot.status = VolumetricCloudFrameStatus::PassDisabled;
        return;
    }
    const bool volumeView = m_settings.debugView == VolumetricCloudDebugView::ShapeSlice
        || m_settings.debugView == VolumetricCloudDebugView::DetailSlice;
    if (!f.ShaderReady(volumeView ? m_slice3dShader : m_slice2dShader)) {
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
        return;
    }
    const fx::Texture source = AcquireDebugSource(f);
    if (!source.IsValid()) {
        snapshot.status = VolumetricCloudFrameStatus::AssetMissing;
        return;
    }
    const fx::Texture output = m_output.Acquire(f, f.Extent(), PixelFormat::RGBA16_Float);
    if (!output.IsValid()) {
        snapshot.status = VolumetricCloudFrameStatus::TargetFailed;
        return;
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
        return;
    }
    snapshot.status = VolumetricCloudFrameStatus::Ok;
    f.ExposeAsViewOutput(m_output, output);
    f.Blit("VolumetricCloud.Debug.CopyToPrivateTarget", output, f.PrivateTarget(), fx::BlitFilter::Linear);
}

GTE_REGISTER_RENDER_FEATURE("VolumetricCloud", VolumetricCloudFeature);

} // namespace gte
