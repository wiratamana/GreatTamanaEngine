#include "TemporalAaFeature.h"

#include "TemporalAaMath.h"
#include "../../Core/PixelFormat.h"

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 47;
constexpr std::int32_t kJitterPriority = 48;
constexpr std::int32_t kCompositePriority = -150; // After VolumetricLight, before Bloom.

// Bytes per texel of the two color and two depth history targets.
constexpr std::uint64_t kTexelBytes = 8 + 8 + 4 + 4;

// Camera of `v`; invalid when it has no usable matrix.
fx::CameraBasis UsableBasis(const fx::View& v) noexcept
{
    if (!v.invViewProjectionValid || ApproximatelyEqual(v.viewProjection, Mat4::Identity())) {
        return fx::CameraBasis{};
    }
    return fx::DeriveCameraBasis(v.eye, v.invViewProjection, v.extent);
}

bool ResolvePassesEnabled(const fx::Frame& f)
{
    return f.PassEnabled("TemporalAa.Resolve.Dispatch") && f.PassEnabled("TemporalAa.CopyToPrivateTarget");
}

} // namespace

TemporalAaFeature::TemporalAaFeature(fx::FeatureHost&) { }

void TemporalAaFeature::Setup(fx::FeatureSetup& s)
{
    // Not render passes: Always() keeps a pass toggle from freezing the frame clock or the jitter record.
    s.PreOpaque("TemporalAa.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); }, fx::Gate::Always());
    s.PreOpaque("TemporalAa.Jitter", kJitterPriority, [this](fx::PreOpaqueFrame& f) { JitterPass(f); },
        fx::Gate::Always());

    // The secondary view is not supported: its registration starts off.
    s.DefaultSecondaryViewEnabled(false);
    s.PostComposite("TemporalAa", kCompositePriority, fx::Blend::Replace,
        [this](fx::CompositeFrame& f) { ResolvePass(f); },
        fx::Gate{ "TemporalAa.Resolve.Dispatch", "TemporalAa.CopyToPrivateTarget" });
}

void TemporalAaFeature::ApplySettings(const TemporalAaSettings& candidate)
{
    m_settings = SanitizeTemporalAaSettings(candidate);
}

TemporalAaViewSnapshot TemporalAaFeature::Snapshot(rg::ViewScope scope) const
{
    TemporalAaViewSnapshot s = m_views.For(scope).snapshot;
    if (fx::SnapshotIsStale(s.frameIndex, m_newestFrame)) {
        s.status = TemporalAaFrameStatus::NotRendered;
        s.historyUsed = false;
        s.stableFrames = 0;
        s.textureBytes = 0;
    }
    if (fx::SnapshotIsStale(s.jitterFrameIndex, m_newestFrame)) {
        s.jitterDecision = TemporalAaJitterDecision::NotRun;
        s.jitterApplied = false;
        s.jitterPixelX = 0.0f;
        s.jitterPixelY = 0.0f;
    }
    return s;
}

void TemporalAaFeature::JitterPass(fx::PreOpaqueFrame& f)
{
    TemporalAaViewState& view = m_views.For(f.Scope());
    const fx::View& v = f.View();
    const TemporalAaSettings cfg = m_settings;

    TemporalAaJitterInputs inputs;
    inputs.primaryView = f.Scope() == rg::ViewScope::Primary;
    inputs.enabled = cfg.enabled;
    inputs.captureArmed = f.ViewCaptureArmed();
    inputs.passesEnabled = ResolvePassesEnabled(f);
    inputs.cameraValid = UsableBasis(v).valid;
    inputs.extent = v.extent;
    // Placeholders: a disabled feature must never trigger a shader build.
    inputs.shaderReady = true;
    inputs.resolvedLastFrame = true;

    TemporalAaJitterDecision decision = DecideTemporalAaJitter(inputs);
    if (decision == TemporalAaJitterDecision::Apply) {
        inputs.shaderReady = f.ShaderReady(m_resolveShader);
        inputs.resolvedLastFrame = view.resolvedValid && view.resolvedFrame + 1 == f.FrameIndex();
        decision = DecideTemporalAaJitter(inputs);
    }

    Vec2 pixels{};
    bool applied = false;
    if (decision == TemporalAaJitterDecision::Apply) {
        pixels = TemporalAaJitterPixels(f.FrameIndex(), cfg);
        const Vec2 ndc = TemporalAaJitterNdc(pixels, v.extent);
        if (!fx::IsValidViewJitter(ndc)) {
            pixels = Vec2{}; // Too big for this view size: no jitter, not an error.
        } else if (f.SetViewJitter(ndc)) {
            applied = ndc != Vec2::Zero();
        } else {
            pixels = Vec2{};
            f.ReportOnce("TemporalAa: the engine refused the jitter");
        }
    }

    TemporalAaViewSnapshot& snapshot = view.snapshot;
    snapshot.jitterDecision = decision;
    snapshot.jitterFrameIndex = f.FrameIndex();
    snapshot.jitterApplied = applied;
    snapshot.jitterPixelX = pixels.x;
    snapshot.jitterPixelY = pixels.y;
}

void TemporalAaFeature::ResolvePass(fx::CompositeFrame& f)
{
    TemporalAaViewState& view = m_views.For(f.Scope());
    const fx::View& v = f.View();
    const TemporalAaSettings cfg = m_settings;

    // Every early exit declares nothing, drops the history and clears the heartbeat.
    const auto bail = [&](TemporalAaFrameStatus status) {
        view.previous = fx::TemporalHistory{};
        view.resolvedValid = false;
        TemporalAaViewSnapshot& snapshot = view.snapshot;
        snapshot.status = status;
        snapshot.frameIndex = f.FrameIndex();
        snapshot.historyUsed = false;
        snapshot.stableFrames = 0;
        snapshot.textureBytes = 0;
        if (v.jitterNdc != Vec2::Zero()) {
            f.ReportWarningOnce("TemporalAa: a jittered frame was not resolved");
        }
    };

    if (f.Scope() != rg::ViewScope::Primary) {
        bail(TemporalAaFrameStatus::ViewNotSupported);
        return;
    }
    if (!cfg.enabled) {
        bail(TemporalAaFrameStatus::Disabled);
        return;
    }
    if (f.ViewCaptureArmed()) {
        bail(TemporalAaFrameStatus::CaptureArmed);
        return;
    }
    // Before any Acquire, params upload or history write.
    if (!ResolvePassesEnabled(f)) {
        bail(TemporalAaFrameStatus::PassDisabled);
        return;
    }
    const fx::CameraBasis basis = UsableBasis(v);
    if (!basis.valid) {
        bail(TemporalAaFrameStatus::NoValidCamera);
        return;
    }
    const Extent2D extent = f.Extent();
    if (extent.width < kTemporalAaMinViewSide || extent.height < kTemporalAaMinViewSide) {
        bail(TemporalAaFrameStatus::ExtentTooSmall);
        return;
    }
    if (!f.ShaderReady(m_resolveShader)) {
        bail(TemporalAaFrameStatus::ShadersNotReady);
        return;
    }

    // Each target is acquired exactly once per frame; a second request is refused.
    const std::uint32_t slot = static_cast<std::uint32_t>(f.FrameIndex() & 1u);
    const fx::Texture colorNow = view.color[slot].Acquire(f, extent, PixelFormat::RGBA16_Float);
    const fx::Texture colorPrev = view.color[slot ^ 1u].Acquire(f, extent, PixelFormat::RGBA16_Float);
    const fx::Texture depthNow = view.depth[slot].Acquire(f, extent, PixelFormat::R32_Float);
    const fx::Texture depthPrev = view.depth[slot ^ 1u].Acquire(f, extent, PixelFormat::R32_Float);
    if (!colorNow.IsValid() || !colorPrev.IsValid() || !depthNow.IsValid() || !depthPrev.IsValid()) {
        bail(TemporalAaFrameStatus::TargetFailed);
        return;
    }

    const bool useHistory = fx::HistoryUsable(view.previous, basis, f.FrameIndex(), extent, 1u);
    const TemporalAaParamsGpu params =
        MakeTemporalAaParams(basis, view.previous, useHistory, cfg, v.invViewProjection, extent);
    const bool dispatched = f.Compute(m_resolveShader, "TemporalAa.Resolve.Dispatch")
                                .Params(view.params, params)
                                .Bind("sceneColor", f.SceneColor())
                                .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                                .Bind("colorHistory", colorPrev)
                                .Bind("depthHistory", depthPrev)
                                .Bind("colorOutput", colorNow)
                                .Bind("depthOutput", depthNow)
                                .Dispatch(extent);
    if (!dispatched) {
        f.ReportOnce("TemporalAa.Resolve.Dispatch was refused");
        bail(TemporalAaFrameStatus::ShadersNotReady);
        return;
    }
    // Nothing reads the depth in this frame; the next frame does.
    f.Keep(depthNow);

    if (!f.Blit("TemporalAa.CopyToPrivateTarget", colorNow, f.PrivateTarget(), fx::BlitFilter::Linear)) {
        f.ReportOnce("TemporalAa.CopyToPrivateTarget was refused");
        bail(TemporalAaFrameStatus::ShadersNotReady);
        return;
    }

    // Only a frame whose passes were both declared becomes history. The matrix is the unjittered one.
    view.previous = fx::MakeHistory(basis, v.viewProjection, f.FrameIndex(), extent, 1u, view.previous);
    view.resolvedValid = true;
    view.resolvedFrame = f.FrameIndex();

    TemporalAaViewSnapshot& snapshot = view.snapshot;
    snapshot.status = TemporalAaFrameStatus::Ok;
    snapshot.frameIndex = f.FrameIndex();
    snapshot.historyUsed = useHistory && cfg.debugView == TemporalAaDebugView::Off;
    snapshot.stableFrames = view.previous.stableFrames;
    snapshot.extent = extent;
    snapshot.textureBytes = std::uint64_t{ extent.width } * extent.height * kTexelBytes;
}

GTE_REGISTER_RENDER_FEATURE("TemporalAa", TemporalAaFeature);

} // namespace gte
