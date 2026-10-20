#include "ToneMappingFeature.h"

#include "ToneMappingMath.h"
#include "../../Core/PixelFormat.h"

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 40;
constexpr std::int32_t kCompositePriority = 1000; // After every other PostComposite feature.

// A snapshot older than this many frames (against the newest frame of any view) counts as not rendered.
constexpr std::uint64_t kSnapshotStaleFrames = 2;

} // namespace

ToneMappingFeature::ToneMappingFeature(fx::FeatureHost&) { }

void ToneMappingFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a toggle from freezing the frame clock the snapshots age against.
    s.PreOpaque("ToneMapping.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); }, fx::Gate::Always());

    s.PostComposite("ToneMapping", kCompositePriority, fx::Blend::Replace,
        [this](fx::CompositeFrame& f) { CompositePass(f); },
        fx::Gate{ "ToneMapping.Dispatch", "ToneMapping.CopyToPrivateTarget" });
}

void ToneMappingFeature::ApplySettings(const ToneMappingSettings& candidate)
{
    m_settings = SanitizeToneMappingSettings(candidate);
}

ToneMappingViewSnapshot ToneMappingFeature::Snapshot(rg::ViewScope scope) const
{
    ToneMappingViewSnapshot snapshot = m_views.For(scope).snapshot;
    const bool stale = snapshot.frameIndex > m_newestFrame || m_newestFrame - snapshot.frameIndex > kSnapshotStaleFrames;
    if (stale) {
        snapshot.status = ToneMappingFrameStatus::NotRendered;
    }
    return snapshot;
}

void ToneMappingFeature::CompositePass(fx::CompositeFrame& f)
{
    ToneMappingViewState& view = m_views.For(f.Scope());
    const auto finish = [&](ToneMappingFrameStatus status) {
        view.snapshot = ToneMappingViewSnapshot{ status, f.FrameIndex() };
    };

    // Every early return declares nothing, so the compositor skips this entry.
    if (!f.PassEnabled("ToneMapping.Dispatch") || !f.PassEnabled("ToneMapping.CopyToPrivateTarget")) {
        finish(ToneMappingFrameStatus::PassDisabled);
        return;
    }
    if (!f.ShaderReady(m_shader)) {
        finish(ToneMappingFrameStatus::ShadersNotReady);
        return;
    }
    const fx::Texture output = m_output.Acquire(f, f.Extent(), PixelFormat::RGBA8_UNorm);
    if (!output.IsValid()) {
        finish(ToneMappingFrameStatus::TargetFailed);
        return;
    }

    const bool dispatched = f.Compute(m_shader, "ToneMapping.Dispatch")
                                .Push(MakeToneMappingPush(m_settings, f.FrameIndex()))
                                .Bind("sourceColor", f.SceneColor())
                                .Bind("destinationImage", output)
                                .Dispatch(f.Extent());
    if (!dispatched) {
        f.ReportOnce("ToneMapping.Dispatch was refused");
        finish(ToneMappingFrameStatus::ShadersNotReady);
        return;
    }
    f.ExposeAsViewOutput(m_output, output);
    f.Blit("ToneMapping.CopyToPrivateTarget", output, f.PrivateTarget(), fx::BlitFilter::Linear);
    finish(ToneMappingFrameStatus::Ok);
}

GTE_REGISTER_RENDER_FEATURE("ToneMapping", ToneMappingFeature);

} // namespace gte
