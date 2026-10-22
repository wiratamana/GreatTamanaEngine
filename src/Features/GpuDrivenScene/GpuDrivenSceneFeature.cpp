#include "GpuDrivenSceneFeature.h"

#include <algorithm>
#include <string>
#include <utility>

namespace gte {

namespace {

constexpr std::int32_t kOpaquePriority = 0;

// A report older than this many frames (against the newest frame of any view) counts as not rendered.
constexpr std::uint64_t kSnapshotStaleFrames = 2;

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

    // Every pass name of the draw state is a toggle; the callback runs while any of them is enabled.
    fx::Gate gate;
    gate.passNames = m_state.AllPassNames();
    s.Opaque("GpuDrivenScene.Opaque", kOpaquePriority, [this](fx::OpaqueFrame& f) { OpaquePass(f); }, gate);
    for (const std::string& name : gate.passNames) {
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
        m_state.NoteFrame(f, false);
        m_state.ReleaseIdleScope(f);
        finish();
        return;
    }

    const std::span<const fx::ClusterInstance> candidates = f.ClusterCandidates();
    fx::ClusterDrawChain chain = f.ClusterDraw(m_state, 0);
    chain.Instances(candidates)
        .Camera(live)
        .CullCamera(cullCamera)
        .Culling(CullFlagsOf(cfg))
        .Target(f.SceneTarget())
        .IdTint(cfg.debugView == GpuDrivenSceneDebugView::ClusterId)
        .Stats(true);
    const fx::ClusterStats stats = chain.Record();

    snap.claimedEntities = stats.instances;
    snap.parts = stats.parts;
    snap.testedClusters = stats.workItems;
    snap.inertReason = stats.inertReason;
    snap.refusal = DescribeClusterOutcome(candidates.size(), stats);
    const fx::ClusterVisibleCount visible = m_state.VisibleClusters(f);
    snap.visibleValid = visible.valid;
    snap.visibleClusters = visible.value;
    snap.visibleFrame = visible.frame;
    finish();
}

GTE_REGISTER_RENDER_FEATURE("GpuDrivenScene", GpuDrivenSceneFeature);

} // namespace gte
