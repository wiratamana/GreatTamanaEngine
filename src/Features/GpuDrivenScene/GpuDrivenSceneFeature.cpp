#include "GpuDrivenSceneFeature.h"

#include <algorithm>
#include <string>
#include <utility>

namespace gte {

namespace {

constexpr std::int32_t kOpaquePriority = 0;
constexpr std::int32_t kHizBuildPriority = -100; // Unique in PostOpaque; runs first, right after the opaque depth is final.

// A report older than this many frames (against the newest frame of any view) counts as not rendered.
constexpr std::uint64_t kSnapshotStaleFrames = 2;

// A visible-cluster count older than this many frames is not reported.
constexpr std::uint64_t kVisibleCountFreshFrames = 8;

bool HasNoPixels(const fx::View& view) noexcept
{
    return view.extent.width == 0 || view.extent.height == 0;
}

ClusterCullFlags CullFlagsOf(const GpuDrivenSceneSettings& settings) noexcept
{
    ClusterCullFlags flags = ClusterCullFlags::None;
    if (settings.frustum) {
        flags = flags | ClusterCullFlags::Frustum;
    }
    if (settings.cone) {
        flags = flags | ClusterCullFlags::BackfaceCone;
    }
    return flags;
}

} // namespace

GpuDrivenSceneFeature::GpuDrivenSceneFeature(fx::FeatureHost&) { }

void GpuDrivenSceneFeature::Setup(fx::FeatureSetup& s)
{
    if (!m_state.Valid()) {
        s.Fail("the cluster draw state is invalid: " + m_state.Error());
        return;
    }
    if (!m_hiz.Valid()) {
        s.Fail("the occlusion pyramid is invalid: " + m_hiz.Error());
        return;
    }

    // Every pass name of the draw state is a toggle; the callback runs while any of them is enabled.
    fx::Gate gate;
    gate.passNames = m_state.AllPassNames();
    s.Opaque("GpuDrivenScene.Opaque", kOpaquePriority, [this](fx::OpaqueFrame& f) { OpaquePass(f); }, gate);
    for (const std::string& name : gate.passNames) {
        s.CountInViewStats(name);
    }

    // The pyramid is built from the depth the opaque stage left behind and read by the next frame's cull.
    fx::Gate hizGate;
    hizGate.passNames = m_hiz.AllPassNames();
    s.PostOpaque("GpuDrivenScene.HizBuild", kHizBuildPriority, [this](fx::PostOpaqueFrame& f) { HizPass(f); }, hizGate);
    for (const std::string& name : hizGate.passNames) {
        s.CountInViewStats(name);
    }
}

void GpuDrivenSceneFeature::ApplySettings(const GpuDrivenSceneSettings& candidate)
{
    m_settings = SanitizeGpuDrivenSceneSettings(candidate);
}

GpuDrivenSceneViewSnapshot GpuDrivenSceneFeature::Snapshot(rg::ViewScope scope) const
{
    GpuDrivenSceneViewSnapshot snapshot = m_views.For(scope).snapshot;
    const bool stale = snapshot.frameIndex > m_newestFrame || m_newestFrame - snapshot.frameIndex > kSnapshotStaleFrames;
    if (stale) {
        snapshot.rendered = false;
    }
    return snapshot;
}

void GpuDrivenSceneFeature::OpaquePass(fx::OpaqueFrame& f)
{
    GpuDrivenSceneViewState& view = m_views.For(f.Scope());
    m_newestFrame = std::max(m_newestFrame, f.FrameIndex());
    m_supported = f.ClusterPathSupported();

    const GpuDrivenSceneSettings cfg = m_settings;
    const fx::View& viewData = f.View();
    const fx::ClusterCamera live = fx::ClusterCameraOf(viewData);
    const fx::ClusterCamera& cullCamera =
        ResolveCullCamera(view.frozenCamera, cfg.enabled, cfg.freezeCulling, live, viewData.extent);

    GpuDrivenSceneViewSnapshot snap;
    snap.frameIndex = f.FrameIndex();
    snap.rendered = true;
    snap.frozen = view.frozenCamera.frozen;

    const auto finish = [&] {
        snap.ledgerBytes = m_state.HeldBytes(f.Scope());
        view.snapshot = std::move(snap);
    };

    if (!cfg.enabled || HasNoPixels(viewData) || !live.valid) {
        snap.refusal = !cfg.enabled ? "disabled" : (HasNoPixels(viewData) ? "view has no pixels" : "camera is not valid");
        // An idle scope still gives its buffers back.
        m_state.TickIdle(f, false);
        finish();
        return;
    }

    const std::span<const fx::ClusterInstance> candidates = f.ClusterCandidates();
    ClusterCullFlags flags = CullFlagsOf(cfg);
    fx::ClusterDrawChain chain = f.ClusterDraw(m_state, 0);
    if (cfg.occlusion) {
        // Last frame's pyramid, only while its guards hold (first frame, resize, cut, missed build).
        const fx::HizPrevious hiz = m_hiz.Previous(f);
        snap.occlusionUsable = hiz.usable;
        if (hiz.usable) {
            flags = flags | ClusterCullFlags::Occlusion;
            chain.Occlusion(hiz.atlas, hiz.viewProjection, hiz.baseWidth, hiz.baseHeight);
        }
    }
    chain.Instances(candidates)
        .Camera(live)
        .CullCamera(cullCamera)
        .Culling(flags)
        .Target(f.SceneTarget())
        .IdTint(cfg.debugView == GpuDrivenSceneDebugView::ClusterId)
        .Stats(cfg.collectStats);
    const fx::ClusterStats stats = chain.Record();

    snap.claimedEntities = stats.instances;
    snap.parts = stats.parts;
    snap.testedClusters = stats.workItems;
    snap.inertReason = stats.inertReason;
    snap.refusal = DescribeClusterOutcome(candidates.size(), stats);
    snap.statsCollected = cfg.collectStats;
    // A count from before stats were switched on, or from a stalled readback, is never reported.
    const fx::ClusterVisibleCount visible = m_state.VisibleClusters(f);
    snap.visibleValid = cfg.collectStats && visible.valid && visible.frame <= f.FrameIndex()
        && f.FrameIndex() - visible.frame <= kVisibleCountFreshFrames;
    if (snap.visibleValid) {
        snap.visibleClusters = visible.value;
        snap.visibleFrame = visible.frame;
    }
    finish();
}

void GpuDrivenSceneFeature::HizPass(fx::PostOpaqueFrame& f)
{
    const GpuDrivenSceneSettings cfg = m_settings;
    if (!cfg.enabled || !cfg.occlusion) {
        m_hiz.Reset(f);
        return;
    }
    m_hiz.Build(f);
}

GTE_REGISTER_RENDER_FEATURE("GpuDrivenScene", GpuDrivenSceneFeature);

} // namespace gte
