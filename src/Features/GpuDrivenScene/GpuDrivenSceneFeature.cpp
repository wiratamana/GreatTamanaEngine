#include "GpuDrivenSceneFeature.h"

#include <algorithm>
#include <string>
#include <utility>

namespace gte {

namespace {

constexpr std::int32_t kOpaquePriority = 0;
constexpr std::int32_t kCullPriority = -10; // Runs before the opaque pass and leaves its cull plan for it.
constexpr std::int32_t kHizBuildPriority = -100; // Unique in PostOpaque; runs first, right after the opaque depth is final.

// Fraction of a box's largest extent added before the CPU frustum test.
constexpr float kCpuCullPadding = 0.05f;

// A report older than this many frames (against the newest frame of any view) counts as not rendered.
constexpr std::uint64_t kSnapshotStaleFrames = 2;

bool HasNoPixels(const fx::View& view) noexcept
{
    return view.extent.width == 0 || view.extent.height == 0;
}

// Copies the fields only the cluster pass writes; every other field of `to` is kept.
void CopyGpuFields(GpuDrivenSceneViewSnapshot& to, const GpuDrivenSceneViewSnapshot& from)
{
    to.claimedEntities = from.claimedEntities;
    to.parts = from.parts;
    to.occlusion = from.occlusion;
    to.hizReason = from.hizReason;
    to.hizSource = from.hizSource;
    to.testedClusters = from.testedClusters;
    to.testedAtVisibleFrame = from.testedAtVisibleFrame;
    to.visibleClusters = from.visibleClusters;
    to.visibleFrame = from.visibleFrame;
    to.statsCollected = from.statsCollected;
    to.visibleValid = from.visibleValid;
    to.hidden = from.hidden;
    to.hiddenValid = from.hiddenValid;
    to.inertReason = from.inertReason;
    to.refused = from.refused;
    to.refusal = from.refusal;
    to.ledgerBytes = from.ledgerBytes;
}

CullPlan PlanCull(fx::OpaqueFrame& f, const GpuDrivenSceneSettings& cfg, bool secondary)
{
    CullPlan plan;
    const fx::View& view = f.View();
    plan.live = fx::ClusterCameraOf(view);

    // Without an active camera the view matrix is identity and must not cull.
    std::optional<fx::View> primaryView;
    if (cfg.mode != CullMode::NoCull) {
        const float height = view.extent.height > 0 ? static_cast<float>(view.extent.height) : 1.0f;
        primaryView = f.PrimaryCamera(static_cast<float>(view.extent.width) / height);
    }
    fx::ClusterCamera primary;
    primary.valid = false;
    if (secondary && primaryView) {
        primary = fx::ClusterCameraOf(*primaryView);
    }

    plan.source = ChooseCullCamera(secondary, cfg.mode, primary.valid, plan.live.valid && primaryView.has_value());
    plan.cull = plan.source == CullCameraSource::Primary ? primary : plan.live;
    return plan;
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

    // The CPU draw cull is not a render pass: no pass toggle and no `enabled` flag may silence it.
    s.Opaque("GpuDrivenScene.SceneCull", kCullPriority, [this](fx::OpaqueFrame& f) { SceneCullPass(f); }, fx::Gate::Always());

    // The pyramid is built from the depth the opaque stage left behind and read by the next frame's cull.
    fx::Gate hizGate;
    hizGate.passNames = m_hiz.AllPassNames();
    s.PostOpaque("GpuDrivenScene.HizBuild", kHizBuildPriority, [this](fx::PostOpaqueFrame& f) { HizPass(f); }, hizGate);
}

void GpuDrivenSceneFeature::ApplySettings(const GpuDrivenSceneSettings& candidate)
{
    m_settings = SanitizeGpuDrivenSceneSettings(candidate);
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
    // GPU-path numbers show only when the cluster pass ran in the same frame as the liveness report.
    if (view.gpuFrame != snapshot.frameIndex) {
        CopyGpuFields(snapshot, GpuDrivenSceneViewSnapshot{});
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
}

void GpuDrivenSceneFeature::OpaquePass(fx::OpaqueFrame& f)
{
    const rg::ViewScope scope = f.Scope();
    const bool secondary = scope == rg::ViewScope::Secondary;
    GpuDrivenSceneViewState& view = m_views.For(scope);
    const std::uint64_t frameIndex = f.FrameIndex();
    BeginFrame(frameIndex);
    m_supported = f.ClusterPathSupported();

    const GpuDrivenSceneSettings cfg = m_settings;
    const ViewCullTests tests = ResolveCullTests(cfg);
    // SceneCullPass normally planned this frame already; plan here if it has not run first.
    const CullPlan plan = view.hasPlan && view.planFrame == frameIndex ? view.plan : PlanCull(f, cfg, secondary);
    const bool hasPixels = !HasNoPixels(f.View());
    const bool hizRequested = cfg.mode == CullMode::HiZ;
    const bool wantStats = cfg.collectStats;

    // Holds the GPU-path fields only; `finish` copies them into the view snapshot.
    GpuDrivenSceneViewSnapshot snap;

    const auto finish = [&] {
        view.tested.Record(frameIndex, snap.testedClusters);
        snap.ledgerBytes = m_state.HeldBytes(scope);
        CopyGpuFields(view.snapshot, snap);
        view.gpuFrame = frameIndex;
    };

    if (!cfg.enabled || !hasPixels || !plan.live.valid) {
        snap.refusal = !cfg.enabled ? "disabled" : (!hasPixels ? "view has no pixels" : "camera is not valid");
        if (cfg.enabled && hasPixels && hizRequested) {
            snap.occlusion = OcclusionState::NoCullCamera;
        }
        // An idle scope still gives its buffers back.
        m_state.TickIdle(f, false);
        finish();
        return;
    }

    const std::span<const fx::ClusterInstance> candidates = f.ClusterCandidates();
    const bool culls = plan.source != CullCameraSource::None;
    ClusterCullFlags flags = culls ? CullFlagsOf(tests) : ClusterCullFlags::None;
    fx::ClusterDrawChain chain = f.ClusterDraw(m_state, 0);
    bool pyramidBound = false;
    if (!secondary && tests.occlusion && plan.source == CullCameraSource::Own) {
        // Last frame's pyramid, only while its guards hold (first frame, resize, cut, missed build).
        const fx::HizPrevious hiz = m_hiz.Previous(f);
        snap.hizReason = hiz.reason;
        snap.hizSource = CullCameraSource::Own;
        if (hiz.usable) {
            pyramidBound = true;
            flags = flags | ClusterCullFlags::Occlusion;
            chain.Occlusion(hiz.atlas, hiz.viewProjection, hiz.layout);
        }
    }
    chain.Instances(candidates)
        .Camera(plan.live)
        .CullCamera(plan.cull)
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
    snap.occlusion = OcclusionStateOf(hizRequested, culls, !secondary && stats.occlusionApplied);
    if (secondary && hizRequested && culls) {
        snap.hizReason = HizReason::Unavailable; // Only the primary view owns a pyramid.
    } else if (pyramidBound && !stats.occlusionApplied) {
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
            if (const std::optional<std::uint64_t> hidden = HiddenClusterCount(*tested, visible.value)) {
                snap.hidden = *hidden;
                snap.hiddenValid = true;
            }
        }
    }
    finish();
}

void GpuDrivenSceneFeature::SceneCullPass(fx::OpaqueFrame& f)
{
    const rg::ViewScope scope = f.Scope();
    const bool secondary = scope == rg::ViewScope::Secondary;
    GpuDrivenSceneViewState& view = m_views.For(scope);
    const std::uint64_t frameIndex = f.FrameIndex();
    BeginFrame(frameIndex);

    const GpuDrivenSceneSettings cfg = m_settings;
    const CullPlan plan = PlanCull(f, cfg, secondary);
    view.plan = plan;
    view.planFrame = frameIndex;
    view.hasPlan = true;

    // This pass is always on, so it owns the liveness and CPU fields of the snapshot.
    GpuDrivenSceneViewSnapshot& snap = view.snapshot;
    snap.frameIndex = frameIndex;
    snap.rendered = true;
    snap.enabled = cfg.enabled;
    snap.hasPixels = !HasNoPixels(f.View());
    snap.cameraValid = plan.live.valid;
    snap.cullCamera = plan.source;
    const fx::SceneCullCounts counts = f.LastSceneDrawCullCounts();
    snap.cpuTested = counts.tested;
    snap.cpuCulled = counts.culled;
    snap.cpuUnbounded = counts.unbounded;

    if (cfg.mode == CullMode::NoCull) {
        snap.cpuCullState = CpuCullState::Off;
    } else if (f.ViewCaptureArmed()) {
        snap.cpuCullState = CpuCullState::SuppressedCapture;
    } else if (plan.source == CullCameraSource::None) {
        snap.cpuCullState = CpuCullState::NoCamera;
    } else {
        snap.cpuCullState = f.CullSceneDraw(plan.cull.viewProjection, kCpuCullPadding) ? CpuCullState::Active
                                                                                         : CpuCullState::NoCamera;
    }
}

void GpuDrivenSceneFeature::HizPass(fx::PostOpaqueFrame& f)
{
    GpuDrivenSceneViewState& view = m_views.For(f.Scope());
    const bool wants = ViewWantsHiz(m_settings, f.Scope() == rg::ViewScope::Secondary);
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
