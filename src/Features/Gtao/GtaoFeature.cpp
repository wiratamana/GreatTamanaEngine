#include "GtaoFeature.h"

#include "GtaoMath.h"
#include "../../Core/PixelFormat.h"

#include <optional>

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 40;
constexpr std::int32_t kOcclusionPriority = 50;
constexpr std::int32_t kCompositePriority = -1500;

// A snapshot older than this many frames (against the newest frame of any view) counts as not rendered.
constexpr std::uint64_t kSnapshotStaleFrames = 2;

constexpr std::uint64_t kAoBytesPerTexel = 8; // RGBA16 float.

constexpr float kTuningDepthMeters = 5.0f; // View depth the snapshot reports the screen radius at.

} // namespace

GtaoFeature::GtaoFeature(fx::FeatureHost&) { }

void GtaoFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a toggle from freezing the frame clock the snapshots age against.
    s.PreOpaque("Gtao.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); }, fx::Gate::Always());

    s.PostOpaque("Gtao.Occlusion", kOcclusionPriority, [this](fx::PostOpaqueFrame& f) { OcclusionPass(f); },
        fx::Gate{ "Gtao.Main.Dispatch", "Gtao.Resolve.Dispatch" });
    s.PostComposite("Gtao.Composite", kCompositePriority, fx::Blend::ScreenSpaceMask,
        [this](fx::CompositeFrame& f) { CompositePass(f); }, fx::Gate{ "Gtao.Composite.Draw" });
}

void GtaoFeature::ApplySettings(const GtaoSettings& candidate)
{
    m_settings = SanitizeGtaoSettings(candidate);
}

GtaoViewSnapshot GtaoFeature::Snapshot(rg::ViewScope scope) const
{
    GtaoViewSnapshot snapshot = m_views.For(scope).snapshot;
    const bool stale = snapshot.frameIndex > m_newestFrame || m_newestFrame - snapshot.frameIndex > kSnapshotStaleFrames;
    if (stale) {
        snapshot.status = GtaoFrameStatus::NotRendered;
    }
    return snapshot;
}

void GtaoFeature::OcclusionPass(fx::PostOpaqueFrame& f)
{
    GtaoViewState& view = m_views.For(f.Scope());
    const fx::View& v = f.View();
    const GtaoSettings& cfg = m_settings;

    GtaoViewSnapshot snap;
    snap.frameIndex = f.FrameIndex();

    // Every early exit drops the history: it was not written this frame, so the next frame must not read it.
    const auto bail = [&](GtaoFrameStatus status) {
        view.previous = fx::TemporalHistory{};
        snap.status = status;
        view.snapshot = snap;
    };

    if (v.extent.width == 0 || v.extent.height == 0 || !v.invViewProjectionValid) {
        bail(GtaoFrameStatus::NoValidCamera);
        return;
    }
    const fx::CameraBasis basis = fx::DeriveCameraBasis(v.eye, v.invViewProjection, v.extent);
    if (!basis.valid) {
        f.ReportOnce("Gtao: camera is not a square-pixel perspective camera, AO is off");
        bail(GtaoFrameStatus::NoValidCamera);
        return;
    }
    if (!f.PassEnabled("Gtao.Main.Dispatch")) {
        bail(GtaoFrameStatus::PassDisabled);
        return;
    }
    if (!f.ShaderReady(m_mainShader)) {
        bail(GtaoFrameStatus::ShadersNotReady);
        return;
    }

    const Extent2D aoExtent = fx::ScaledExtent(v.extent, cfg.resolutionDivisor);
    const fx::Texture raw = view.raw.Acquire(f, aoExtent, PixelFormat::RGBA16_Float);
    if (!raw.IsValid()) {
        bail(GtaoFrameStatus::TargetFailed);
        return;
    }

    const std::uint32_t noiseFrame = cfg.temporal ? static_cast<std::uint32_t>(f.FrameIndex() & 63u) : 0u;
    const bool sampled = f.Compute(m_mainShader, "Gtao.Main.Dispatch")
                             .Params(view.mainParams, MakeGtaoMainParams(basis, cfg, v.invViewProjection, noiseFrame))
                             .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                             .Bind("aoOutput", raw)
                             .Dispatch(aoExtent);
    if (!sampled) {
        f.ReportOnce("Gtao.Main.Dispatch was refused");
        bail(GtaoFrameStatus::ShadersNotReady);
        return;
    }

    // Denoise and accumulate. Without it the composite reads the raw samples through the same shader path.
    fx::Texture shown = raw;
    bool resolved = false;
    bool historyUsed = false;
    if (f.PassEnabled("Gtao.Resolve.Dispatch") && f.ShaderReady(m_resolveShader)) {
        // Each target is acquired once per frame; a second request in the same frame is refused.
        const std::uint32_t slot = static_cast<std::uint32_t>(f.FrameIndex() & 1u);
        const fx::Texture current = view.history[slot].Acquire(f, aoExtent, PixelFormat::RGBA16_Float);
        const fx::Texture previous = view.history[slot ^ 1u].Acquire(f, aoExtent, PixelFormat::RGBA16_Float);
        if (current.IsValid() && previous.IsValid()) {
            const bool useHistory = cfg.temporal
                && fx::HistoryUsable(view.previous, basis, f.FrameIndex(), aoExtent, cfg.resolutionDivisor);
            const fx::TemporalResolveSettings filter{
                cfg.depthSigma, cfg.historyWeight, cfg.spatialDenoise, cfg.resolutionDivisor };
            const fx::TemporalResolveParamsGpu params =
                fx::MakeTemporalResolveParams(basis, view.previous, filter, v.extent, v.invViewProjection, useHistory);
            resolved = f.Compute(m_resolveShader, "Gtao.Resolve.Dispatch")
                           .Params(view.resolveParams, params)
                           .Bind("aoRaw", raw)
                           .Bind("aoHistory", previous)
                           .Bind("aoOutput", current)
                           .Dispatch(aoExtent);
            if (resolved) {
                shown = current;
                historyUsed = useHistory;
            } else {
                f.ReportOnce("Gtao.Resolve.Dispatch was refused");
            }
        }
    }

    // Only a frame whose resolve pass ran becomes history.
    view.previous = resolved ? fx::MakeHistory(basis, v.viewProjection, f.FrameIndex(), aoExtent,
                                   cfg.resolutionDivisor, view.previous)
                             : fx::TemporalHistory{};
    m_result.Publish(f, GtaoResult{ shown, cfg.resolutionDivisor, basis }, { shown });

    snap.aoExtent = aoExtent;
    snap.divisor = cfg.resolutionDivisor;
    snap.historyUsed = historyUsed;
    snap.screenRadiusAt5m = GtaoScreenRadiusPixels(cfg.radius, kTuningDepthMeters, basis.pixelScale, kGtaoMaxScreenRadiusPixels);
    snap.textureBytes = std::uint64_t{ aoExtent.width } * aoExtent.height * kAoBytesPerTexel * (resolved ? 3u : 1u);
    snap.status = GtaoFrameStatus::Ok;
    view.snapshot = snap;
}

void GtaoFeature::CompositePass(fx::CompositeFrame& f)
{
    const std::optional<GtaoResult> result = m_result.Fetch(f);
    if (!result || !result->basis.valid || !f.View().invViewProjectionValid) {
        return;
    }
    const GtaoCompositePush push =
        MakeGtaoCompositePush(result->basis, m_settings, f.View().invViewProjection, result->divisor);
    const bool drawn = f.Fullscreen(m_compositeShader, "Gtao.Composite.Draw")
                           .Bind("sceneColor", f.SceneColor())
                           .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                           .Bind("aoTexture", result->ao)
                           .Push(push)
                           .Target(f.PrivateTarget())
                           .Draw();
    if (!drawn) {
        f.ReportOnce("Gtao.Composite.Draw was refused");
    }
}

GTE_REGISTER_RENDER_FEATURE("Gtao", GtaoFeature);

} // namespace gte
