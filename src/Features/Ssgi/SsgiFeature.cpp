#include "SsgiFeature.h"

#include "SsgiMath.h"
#include "../../Core/PixelFormat.h"

#include <optional>

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 35;
constexpr std::int32_t kTracePriority = 55;
constexpr std::int32_t kCompositePriority = -1600;

// A snapshot older than this many frames (against the newest frame of any view) counts as not rendered.
constexpr std::uint64_t kSnapshotStaleFrames = 2;

constexpr std::uint64_t kBytesPerTexel = 8; // RGBA16 float.

} // namespace

SsgiFeature::SsgiFeature(fx::FeatureHost&) { }

void SsgiFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a toggle from freezing the frame clock the snapshots age against.
    s.PreOpaque("Ssgi.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); }, fx::Gate::Always());

    s.PostOpaque("Ssgi.Trace", kTracePriority, [this](fx::PostOpaqueFrame& f) { TracePass(f); },
        fx::Gate{ "Ssgi.Prepare.Dispatch", "Ssgi.Trace.Dispatch", "Ssgi.Resolve.Dispatch" });
    s.PostComposite("Ssgi.Composite", kCompositePriority, fx::Blend::ScreenSpaceMask,
        [this](fx::CompositeFrame& f) { CompositePass(f); }, fx::Gate{ "Ssgi.Composite.Draw" });
}

void SsgiFeature::ApplySettings(const SsgiSettings& candidate)
{
    m_settings = SanitizeSsgiSettings(candidate);
}

SsgiViewSnapshot SsgiFeature::Snapshot(rg::ViewScope scope) const
{
    SsgiViewSnapshot snapshot = m_views.For(scope).snapshot;
    const bool stale = snapshot.frameIndex > m_newestFrame || m_newestFrame - snapshot.frameIndex > kSnapshotStaleFrames;
    if (stale) {
        snapshot.status = SsgiFrameStatus::NotRendered;
    }
    return snapshot;
}

void SsgiFeature::TracePass(fx::PostOpaqueFrame& f)
{
    SsgiViewState& view = m_views.For(f.Scope());
    const fx::View& v = f.View();
    const SsgiSettings& cfg = m_settings;

    SsgiViewSnapshot snap;
    snap.frameIndex = f.FrameIndex();

    // Every early exit drops the history: it was not written this frame, so the next frame must not read it.
    const auto bail = [&](SsgiFrameStatus status) {
        view.previous = fx::TemporalHistory{};
        snap.status = status;
        view.snapshot = snap;
    };

    const bool sceneViewOff = f.Scope() == rg::ViewScope::Secondary && !cfg.applyInSceneView;
    if (sceneViewOff || cfg.intensity <= 0.0f) {
        bail(SsgiFrameStatus::PassDisabled);
        return;
    }
    // No active camera leaves the view-projection as identity; that is "no camera yet", not a bad camera.
    if (v.extent.width == 0 || v.extent.height == 0 || !v.invViewProjectionValid
        || ApproximatelyEqual(v.viewProjection, Mat4::Identity())) {
        bail(SsgiFrameStatus::NoValidCamera);
        return;
    }
    const fx::CameraBasis basis = fx::DeriveCameraBasis(v.eye, v.invViewProjection, v.extent);
    if (!basis.valid) {
        f.ReportOnce("Ssgi: camera is not a square-pixel perspective camera, GI is off");
        bail(SsgiFrameStatus::NoValidCamera);
        return;
    }
    if (!f.PassEnabled("Ssgi.Prepare.Dispatch") || !f.PassEnabled("Ssgi.Trace.Dispatch")) {
        bail(SsgiFrameStatus::PassDisabled);
        return;
    }
    // Ask both before testing either, so a failed build of one does not hide the other.
    const bool prepareReady = f.ShaderReady(m_prepareShader);
    const bool traceReady = f.ShaderReady(m_traceShader);
    if (!prepareReady || !traceReady) {
        bail(SsgiFrameStatus::ShadersNotReady);
        return;
    }

    // Each target is acquired once per frame; a refused Acquire returns an invalid texture.
    const Extent2D gi = fx::ScaledExtent(v.extent, cfg.resolutionDivisor);
    const fx::Texture color = view.color.Acquire(f, gi, PixelFormat::RGBA16_Float);
    const fx::Texture normal = view.normal.Acquire(f, gi, PixelFormat::RGBA16_Float);
    const fx::Texture raw = view.raw.Acquire(f, gi, PixelFormat::RGBA16_Float);
    if (!color.IsValid() || !normal.IsValid() || !raw.IsValid()) {
        bail(SsgiFrameStatus::TargetFailed);
        return;
    }

    // A refused dispatch publishes nothing, so the composite declares nothing.
    const bool prepared = f.Compute(m_prepareShader, "Ssgi.Prepare.Dispatch")
                              .Params(view.prepareParams, MakeSsgiPrepareParams(basis, cfg, v.invViewProjection))
                              .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                              .Bind("sceneColor", f.SceneColor())
                              .Bind("ssgiColorOut", color)
                              .Bind("ssgiNormalOut", normal)
                              .Dispatch(gi);
    if (!prepared) {
        f.ReportOnce("Ssgi.Prepare.Dispatch was refused");
        bail(SsgiFrameStatus::ShadersNotReady);
        return;
    }

    const std::uint32_t noiseFrame = cfg.temporal ? static_cast<std::uint32_t>(f.FrameIndex() & 63u) : 0u;
    const bool traced = f.Compute(m_traceShader, "Ssgi.Trace.Dispatch")
                            .Params(view.traceParams, MakeSsgiTraceParams(basis, cfg, v.extent, noiseFrame))
                            .Bind("ssgiColor", color)
                            .Bind("ssgiNormal", normal)
                            .Bind("ssgiRaw", raw)
                            .Dispatch(gi);
    if (!traced) {
        f.ReportOnce("Ssgi.Trace.Dispatch was refused");
        bail(SsgiFrameStatus::ShadersNotReady);
        return;
    }

    // Denoise and accumulate. Without it the composite reads the raw samples.
    fx::Texture shown = raw;
    bool resolved = false;
    bool historyUsed = false;
    if (f.PassEnabled("Ssgi.Resolve.Dispatch") && f.ShaderReady(m_resolveShader)) {
        const std::uint32_t slot = static_cast<std::uint32_t>(f.FrameIndex() & 1u);
        const fx::Texture current = view.history[slot].Acquire(f, gi, PixelFormat::RGBA16_Float);
        const fx::Texture previous = view.history[slot ^ 1u].Acquire(f, gi, PixelFormat::RGBA16_Float);
        if (current.IsValid() && previous.IsValid()) {
            const bool useHistory = cfg.temporal
                && fx::HistoryUsable(view.previous, basis, f.FrameIndex(), gi, cfg.resolutionDivisor);
            const fx::TemporalResolveSettings filter{
                cfg.depthSigma, cfg.historyWeight, cfg.spatialDenoise, cfg.resolutionDivisor };
            resolved = f.Compute(m_resolveShader, "Ssgi.Resolve.Dispatch")
                           .Params(view.resolveParams, fx::MakeTemporalResolveParams(basis, view.previous, filter,
                                                           v.extent, v.invViewProjection, useHistory))
                           .Bind("ssgiRaw", raw)
                           .Bind("ssgiHistory", previous)
                           .Bind("ssgiOutput", current)
                           .Dispatch(gi);
            if (resolved) {
                shown = current;
                historyUsed = useHistory;
            } else {
                f.ReportOnce("Ssgi.Resolve.Dispatch was refused");
            }
        }
    }

    // Only a frame whose resolve pass ran becomes history.
    view.previous = resolved ? fx::MakeHistory(basis, v.viewProjection, f.FrameIndex(), gi, cfg.resolutionDivisor,
                                   view.previous)
                             : fx::TemporalHistory{};
    m_result.Publish(f, SsgiResult{ shown, normal, cfg.resolutionDivisor, basis }, { shown, normal });

    snap.giExtent = gi;
    snap.divisor = cfg.resolutionDivisor;
    snap.historyUsed = historyUsed;
    snap.textureBytes = std::uint64_t{ gi.width } * gi.height * kBytesPerTexel * (resolved ? 5u : 3u);
    snap.status = SsgiFrameStatus::Ok;
    view.snapshot = snap;
}

void SsgiFeature::CompositePass(fx::CompositeFrame& f)
{
    const std::optional<SsgiResult> result = m_result.Fetch(f);
    if (!result || !result->basis.valid || !f.View().invViewProjectionValid) {
        return;
    }
    const SsgiCompositePush push =
        MakeSsgiCompositePush(result->basis, m_settings, f.View().invViewProjection, result->divisor);
    const bool drawn = f.Fullscreen(m_compositeShader, "Ssgi.Composite.Draw")
                           .Bind("sceneColor", f.SceneColor())
                           .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                           .Bind("giTexture", result->gi)
                           .Bind("ssgiNormal", result->normal)
                           .Push(push)
                           .Target(f.PrivateTarget())
                           .Draw();
    if (!drawn) {
        f.ReportOnce("Ssgi.Composite.Draw was refused");
    }
}

GTE_REGISTER_RENDER_FEATURE("Ssgi", SsgiFeature);

} // namespace gte
