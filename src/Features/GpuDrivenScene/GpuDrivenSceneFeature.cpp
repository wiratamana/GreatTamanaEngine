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

bool HasNoPixels(const fx::View& view) noexcept
{
    return view.extent.width == 0 || view.extent.height == 0;
}

ClusterCullFlags CullFlagsOf(const ViewCullTests& tests) noexcept
{
    ClusterCullFlags flags = ClusterCullFlags::None;
    if (tests.frustum) {
        flags = flags | ClusterCullFlags::Frustum;
    }
    if (tests.cone) {
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

GpuDrivenSceneSettings GpuDrivenSceneFeature::EditableSettings()
{
    FlushRequest();
    return m_settings;
}

void GpuDrivenSceneFeature::ApplySettings(const GpuDrivenSceneSettings& candidate)
{
    m_settings = SanitizeGpuDrivenSceneSettings(candidate);
}

void GpuDrivenSceneFeature::FlushRequest()
{
    fx::ViewCullRequest& request = m_link->request;
    if (!request.pending) {
        return;
    }
    m_settings = ApplyViewCullRequest(m_settings, request);
    request.pending = false;
    request.freezeHere = false;
    request.resetAll = false;
}

GpuDrivenSceneViewSnapshot GpuDrivenSceneFeature::Snapshot(rg::ViewScope scope) const
{
    const GpuDrivenSceneViewState& view = m_views.For(scope);
    GpuDrivenSceneViewSnapshot snapshot = view.snapshot;
    const auto stale = [this](std::uint64_t frame) {
        return frame > m_newestFrame || m_newestFrame - frame > kSnapshotStaleFrames;
    };
    if (stale(snapshot.frameIndex)) {
        snapshot.rendered = false;
    }
    snapshot.hizTrackingRequested = view.hizBuildAsked && !stale(view.hizBuildFrame);
    return snapshot;
}

void GpuDrivenSceneFeature::BeginFrame(std::uint64_t frameIndex)
{
    if (m_frameBegun && frameIndex == m_beginFrame) {
        return;
    }
    m_frameBegun = true;
    m_beginFrame = frameIndex;
    m_newestFrame = std::max(m_newestFrame, frameIndex);

    FlushRequest();
    fx::ViewCullRequest& request = m_link->request;
    m_hudStatsActive = m_hudStatsHold.Step(request.hudShown);
    request.hudShown = false;

    // A Scene view that stopped rendering never rewrites its report.
    fx::ViewCullReport& report = m_link->report;
    if (report.rendered && frameIndex > report.frame + kSnapshotStaleFrames) {
        report.rendered = false;
    }
}

void GpuDrivenSceneFeature::OpaquePass(fx::OpaqueFrame& f)
{
    const rg::ViewScope scope = f.Scope();
    const bool sceneView = scope == rg::ViewScope::SceneView;
    GpuDrivenSceneViewState& view = m_views.For(scope);
    const std::uint64_t frameIndex = f.FrameIndex();
    BeginFrame(frameIndex);
    m_supported = f.ClusterPathSupported();

    const GpuDrivenSceneSettings cfg = m_settings;
    const ViewCullTests tests = ResolveViewCullTests(cfg, sceneView);
    const fx::View& viewData = f.View();
    const fx::ClusterCamera live = fx::ClusterCameraOf(viewData);
    const fx::ClusterCamera& cullCamera = ResolveCullCamera(view.frozenCamera,
        cfg.enabled && ViewFreezeRequested(cfg, sceneView), sceneView ? cfg.freezeSerial : 0u, live, frameIndex);
    const bool cullIsLive = !view.frozenCamera.frozen;
    const bool wantStats = cfg.collectStats || (sceneView && m_hudStatsActive);

    GpuDrivenSceneViewSnapshot snap;
    snap.frameIndex = frameIndex;
    snap.rendered = true;
    snap.enabled = cfg.enabled;
    snap.hasPixels = !HasNoPixels(viewData);
    snap.cameraValid = live.valid;
    snap.frozen = view.frozenCamera.frozen;
    snap.frozenFrame = view.frozenCamera.frozen ? view.frozenCamera.frame : 0;
    snap.cullCameraValid = cullCamera.valid;
    snap.cullViewProjection = cullCamera.viewProjection;

    const auto finish = [&] {
        view.tested.Record(frameIndex, snap.testedClusters);
        snap.ledgerBytes = m_state.HeldBytes(scope);
        view.snapshot = std::move(snap);
        if (sceneView) {
            m_link->report = MakeViewCullReport(cfg, view.snapshot);
        }
    };

    if (!cfg.enabled || !snap.hasPixels || !live.valid) {
        snap.refusal = !cfg.enabled ? "disabled" : (!snap.hasPixels ? "view has no pixels" : "camera is not valid");
        // Nothing was culled: never report a cull camera or a stale matrix.
        snap.cullCameraValid = false;
        snap.cullViewProjection = Mat4::Identity();
        // An idle scope still gives its buffers back.
        m_state.TickIdle(f, false);
        finish();
        return;
    }

    const std::span<const fx::ClusterInstance> candidates = f.ClusterCandidates();
    ClusterCullFlags flags = CullFlagsOf(tests);
    fx::ClusterDrawChain chain = f.ClusterDraw(m_state, 0);
    bool pyramidBound = false;
    if (HizMatchesCullCamera(tests.occlusion, cullIsLive)) {
        // Last frame's pyramid, only while its guards hold (first frame, resize, cut, missed build).
        const fx::HizPrevious hiz = m_hiz.Previous(f);
        snap.hizReason = hiz.reason;
        if (hiz.usable) {
            pyramidBound = true;
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
        .Stats(wantStats);
    const fx::ClusterStats stats = chain.Record();

    snap.claimedEntities = stats.instances;
    snap.parts = stats.parts;
    snap.testedClusters = stats.workItems;
    snap.inertReason = stats.inertReason;
    snap.refused = ClusterOutcomeRefused(candidates.size(), stats);
    snap.refusal = DescribeClusterOutcome(candidates.size(), stats);
    snap.occlusion = OcclusionStateOf(tests.occlusion, cullIsLive, stats.occlusionApplied);
    if (pyramidBound && !stats.occlusionApplied) {
        snap.hizReason = HizReason::Unavailable; // The pyramid was ready, the chain still dropped the test.
    }
    snap.statsCollected = wantStats;

    // The count is two frames old: it is only shown next to the tested value of its own frame.
    const fx::ClusterVisibleCount visible = m_state.VisibleClusters(f);
    if (wantStats && visible.valid) {
        if (const std::optional<std::uint64_t> tested = view.tested.At(visible.frame)) {
            snap.visibleValid = true;
            snap.visibleClusters = visible.value;
            snap.visibleFrame = visible.frame;
            snap.testedAtVisibleFrame = *tested;
            if (const std::optional<std::uint64_t> hidden = fx::HiddenClusterCount(*tested, visible.value)) {
                snap.hidden = *hidden;
                snap.hiddenValid = true;
            }
        }
    }
    finish();
}

void GpuDrivenSceneFeature::HizPass(fx::PostOpaqueFrame& f)
{
    GpuDrivenSceneViewState& view = m_views.For(f.Scope());
    const bool wants = ViewWantsHiz(m_settings, f.Scope() == rg::ViewScope::SceneView);
    // Build() asks for movement tracking before any guard, so "asked" equals "will build".
    view.hizBuildAsked = wants;
    view.hizBuildFrame = f.FrameIndex();
    if (!wants) {
        m_hiz.Reset(f);
        return;
    }
    m_hiz.Build(f);
}

GTE_REGISTER_RENDER_FEATURE("GpuDrivenScene", GpuDrivenSceneFeature);

} // namespace gte
