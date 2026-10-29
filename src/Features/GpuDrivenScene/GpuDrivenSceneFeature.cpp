#include "GpuDrivenSceneFeature.h"

#include <algorithm>
#include <string>
#include <utility>

namespace gte {

namespace {

constexpr std::int32_t kOpaquePriority = 0;
constexpr std::int32_t kCullPriority = -10; // Runs before the opaque pass and leaves its cull plan for it.

// Fraction of a box's largest extent added before the CPU frustum test.
constexpr float kCpuCullPadding = 0.05f;

bool HasNoPixels(const fx::View& view) noexcept
{
    return view.extent.width == 0 || view.extent.height == 0;
}

CullPlan PlanCull(fx::OpaqueFrame& f, const GpuDrivenSceneSettings& cfg, bool secondary)
{
    CullPlan plan;
    const fx::View& view = f.View();
    plan.live = fx::ClusterCameraOf(view);

    // Without an active camera the view matrix is identity and must not cull.
    std::optional<fx::View> primaryView;
    if (cfg.mode != GpuCullMode::NoCull) {
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

    // Every pass name of the draw state and of the occlusion pyramid is a toggle; the callback runs while any
    // of them is enabled.
    fx::Gate gate;
    gate.passNames = m_state.AllPassNames();
    for (std::string& name : m_hiz.AllPassNames()) {
        gate.passNames.push_back(std::move(name));
    }
    s.Opaque("GpuDrivenScene.Opaque", kOpaquePriority, [this](fx::OpaqueFrame& f) { OpaquePass(f); }, gate);

    // The CPU draw cull is not a render pass: no pass toggle and no `enabled` flag may silence it.
    s.Opaque("GpuDrivenScene.SceneCull", kCullPriority, [this](fx::OpaqueFrame& f) { SceneCullPass(f); }, fx::Gate::Always());
}

void GpuDrivenSceneFeature::ApplySettings(const GpuDrivenSceneSettings& candidate)
{
    m_settings = SanitizeGpuDrivenSceneSettings(candidate);
}

GpuDrivenSceneViewSnapshot GpuDrivenSceneFeature::Snapshot(rg::ViewScope scope) const
{
    const GpuDrivenSceneViewState& view = m_views.For(scope);
    GpuDrivenSceneViewSnapshot snapshot = view.snapshot;
    if (fx::SnapshotIsStale(snapshot.frameIndex, m_newestFrame)) {
        snapshot.rendered = false;
    }
    // GPU-path numbers show only when the cluster pass ran in the same frame as the liveness report.
    if (view.gpuFrame != snapshot.frameIndex) {
        snapshot.gpu = GpuPathReport{};
    }
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
    const bool hizRequested = cfg.mode == GpuCullMode::HiZ;
    const bool wantStats = cfg.collectStats;

    // Written to the view snapshot by `finish`.
    GpuPathReport snap;

    const auto finish = [&] {
        view.tested.Record(frameIndex, snap.testedClusters);
        snap.ledgerBytes = m_state.HeldBytes(scope);
        view.snapshot.gpu = snap;
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
    const ClusterCullFlags flags = culls ? CullFlagsOf(tests) : ClusterCullFlags::None;
    // Only the primary view owns a pyramid, and only while it culls with its own camera.
    const bool wantsHiz = ViewWantsHiz(cfg, secondary) && plan.source == CullCameraSource::Own;
    fx::ClusterDrawChain chain = f.ClusterDraw(m_state, 0);
    if (wantsHiz) {
        chain.TwoPhase(m_hiz, f.SceneDepth());
        snap.hizSource = CullCameraSource::Own;
    }
    // A secondary view that culls with the primary camera draws the primary view's survivors when they exist.
    if (secondary && plan.source == CullCameraSource::Primary) {
        chain.ReuseSurvivors();
    }
    chain.Instances(candidates)
        .Camera(plan.live)
        .CullCamera(plan.cull)
        .Culling(flags)
        .Target(f.SceneTarget())
        .IdTint(cfg.debugView != GpuDrivenSceneDebugView::Off, cfg.debugView == GpuDrivenSceneDebugView::ClusterPhase)
        .Stats(wantStats);
    const fx::ClusterStats stats = chain.Record();

    snap.claimedEntities = ClaimedInstancesOf(stats);
    snap.parts = stats.parts;
    snap.skippedParts = stats.skippedParts;
    snap.skippedInstances = stats.skippedInstances;
    snap.testedClusters = stats.workItems;
    snap.inertReason = stats.inertReason;
    snap.refused = ClusterOutcomeRefused(candidates.size(), stats);
    snap.refusal = DescribeClusterOutcome(candidates.size(), stats);
    snap.sharedSurvivors = stats.sharedSurvivors;
    snap.cpuPath = CpuPathReasonsOf(f.ClusterRejections(), stats.noSlotInstances);
    // The secondary view inherits the primary view's occlusion result through the shared survivors.
    const bool occluded = secondary ? stats.sharedOcclusion : stats.occlusionApplied;
    snap.occlusion = OcclusionStateOf(hizRequested, culls, occluded);
    if (secondary && occluded) {
        snap.hizSource = CullCameraSource::Primary;
    }
    if (const std::optional<HizReason> reason =
            HizReasonOfView(secondary, hizRequested, culls, occluded, wantsHiz, ChainSkippedEverything(stats), stats.hizReason)) {
        snap.hizReason = *reason;
    }
    snap.statsCollected = wantStats;
    if (wantStats) {
        snap.reasons = m_state.ReasonCounts(f);
    }

    // The count is two frames old: it is only shown next to the tested value of its own frame.
    const fx::ClusterVisibleCount visible = m_state.VisibleClusters(f);
    if (ChainSkippedEverything(stats)) {
        // Every part was outside the view: the chain knows the exact count this frame.
        snap.visibleValid = true;
        snap.visibleClusters = 0;
        snap.visibleFrame = frameIndex;
        snap.testedAtVisibleFrame = 0;
        snap.hidden = 0;
        snap.hiddenValid = true;
    } else if (wantStats && visible.valid) {
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

    if (cfg.mode == GpuCullMode::NoCull) {
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

GTE_REGISTER_RENDER_FEATURE("GpuDrivenScene", GpuDrivenSceneFeature);

} // namespace gte
