#include "ShadowFeature.h"

#include "../../Core/PixelFormat.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

#include <algorithm>
#include <optional>

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 30;
constexpr std::int32_t kCascadesPriority = 10;
constexpr std::int32_t kDepthRangePriority = 100;
constexpr std::int32_t kMaskPriority = 0;
constexpr std::int32_t kCompositePriority = -500;

// A snapshot older than this many frames (against the newest frame of any view) counts as not rendered.
constexpr std::uint64_t kSnapshotStaleFrames = 2;

constexpr std::uint64_t kDepthBytesPerTexel = 4; // 32-bit depth.

constexpr std::array<rg::StaticName, kMaxShadowCascades> kCascadeDrawNames{
    "Shadow.Cascade0.Draw",
    "Shadow.Cascade1.Draw",
    "Shadow.Cascade2.Draw",
    "Shadow.Cascade3.Draw",
};

// The depth-range clear, reduce and readback passes work as one unit.
bool SdsmGroupEnabled(const fx::Frame& f)
{
    return f.PassEnabled("Shadow.DepthRange.Clear") && f.PassEnabled("Shadow.DepthRange.Reduce")
        && f.PassEnabled("Shadow.DepthRange.Readback");
}

bool HasNoPixels(const fx::View& view) noexcept
{
    return view.extent.width == 0 || view.extent.height == 0;
}

// Materials apply the shadow themselves; the mask and composite passes run only for the debug views.
bool ShadowDebugViewNeeded(const ShadowSettings& settings) noexcept
{
    return !settings.applyInMaterials || settings.debugView != ShadowDebugView::Off;
}

// Memory of the cascade maps that exist for the frame's view.
std::uint64_t MapBytes(const ShadowViewState& view, const fx::Frame& f) noexcept
{
    std::uint64_t bytes = 0;
    for (const fx::OffscreenTarget& map : view.maps) {
        const Extent2D e = map.Extent(f);
        bytes += std::uint64_t{ e.width } * e.height * kDepthBytesPerTexel;
    }
    return bytes;
}

} // namespace

ShadowFeature::ShadowFeature(fx::FeatureHost&) { }

std::vector<std::string> ShadowClusterDeclaredPasses(const std::vector<std::string>& names)
{
    constexpr std::string_view kStatsSuffix = ".Stats";
    std::vector<std::string> declared;
    declared.reserve(names.size());
    for (const std::string& name : names) {
        const bool isStats = name.size() >= kStatsSuffix.size()
            && std::string_view{ name }.substr(name.size() - kStatsSuffix.size()) == kStatsSuffix;
        if (!isStats) {
            declared.push_back(name);
        }
    }
    return declared;
}

void ShadowFeature::Setup(fx::FeatureSetup& s)
{
    if (!m_clusterState.Valid()) {
        s.Fail("the cascade cluster draw state is invalid: " + m_clusterState.Error());
        return;
    }

    // Not a render pass: Always() keeps a toggle from freezing the frame clock the snapshots age against.
    s.PreOpaque("Shadow.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); }, fx::Gate::Always());

    // Each gate lists the pass names shown in the Render Graph panel; a callback runs while any of them is enabled.
    fx::Gate cascadeGate{ "Shadow.Cascade0.Draw", "Shadow.Cascade1.Draw", "Shadow.Cascade2.Draw", "Shadow.Cascade3.Draw" };
    const std::vector<std::string> clusterPasses = ShadowClusterDeclaredPasses(m_clusterState.AllPassNames());
    cascadeGate.passNames.insert(cascadeGate.passNames.end(), clusterPasses.begin(), clusterPasses.end());
    s.PreOpaque("Shadow.Cascades", kCascadesPriority, [this](fx::PreOpaqueFrame& f) { CascadesPass(f); }, cascadeGate);
    for (const std::string& name : clusterPasses) {
        s.CountInViewStats(name);
    }
    s.PostOpaque("Shadow.DepthRange", kDepthRangePriority, [this](fx::PostOpaqueFrame& f) { DepthRangePass(f); },
        fx::Gate{ "Shadow.DepthRange.Reduce" });
    s.PostOpaque("Shadow.Mask", kMaskPriority, [this](fx::PostOpaqueFrame& f) { MaskPass(f); },
        fx::Gate{ "Shadow.Mask.Dispatch" });
    s.PostComposite("Shadow.Composite", kCompositePriority, fx::Blend::ScreenSpaceMask,
        [this](fx::CompositeFrame& f) { CompositePass(f); }, fx::Gate{ "Shadow.Composite.Draw" });

    // The slot index is the shader binding in set 1; material shaders read the cascade maps there.
    static constexpr const char* kSlotNames[kMaxShadowCascades] = {
        "Shadow.Cascade0.Slot", "Shadow.Cascade1.Slot", "Shadow.Cascade2.Slot", "Shadow.Cascade3.Slot"
    };
    for (std::uint32_t c = 0; c < kMaxShadowCascades; ++c) {
        m_cascadeSlots[c] = s.DeclareSceneSlot(kSlotNames[c], fx::SceneSlotKind::Depth2D, c);
    }
}

void ShadowFeature::ApplySettings(const ShadowSettings& candidate)
{
    m_settings = SanitizeShadowSettings(candidate);
}

ShadowViewSnapshot ShadowFeature::Snapshot(rg::ViewScope scope) const
{
    ShadowViewSnapshot snapshot = m_views.For(scope).snapshot;
    const bool stale = snapshot.frameIndex > m_newestFrame || m_newestFrame - snapshot.frameIndex > kSnapshotStaleFrames;
    if (stale) {
        snapshot.status = ShadowFrameStatus::NotRendered;
        snapshot.maskStatus = ShadowFrameStatus::NotRendered;
    }
    return snapshot;
}

void ShadowFeature::CascadesPass(fx::PreOpaqueFrame& f)
{
    const ShadowSettings& cfg = m_settings;
    const std::uint32_t resolution = cfg.mapResolution;
    ShadowViewState& view = m_views.For(f.Scope());

    ShadowViewSnapshot snap;
    snap.frameIndex = f.FrameIndex();
    snap.deltaSeconds = f.DeltaSeconds();
    const auto finish = [&](ShadowFrameStatus status) {
        snap.status = status;
        snap.cascadeBytes = MapBytes(view, f);
        snap.sdsm = view.sdsm;
        snap.sdsmAgeFrames = view.sdsm.valid ? f.FrameIndex() - view.sdsm.lastSampleFrame : 0;
        view.snapshot = snap;
    };

    if (!f.View().invViewProjectionValid) {
        finish(ShadowFrameStatus::NoValidCamera);
        return;
    }
    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
    const ShadowViewFrustum frustum = ExtractViewFrustum(f.View().eye, f.View().invViewProjection);
    snap.sunElevationDegrees = SunElevationDegrees(sun.directionTowardSun);
    if (frustum.valid) {
        snap.effectiveMaxDistance = std::min(frustum.viewFar, cfg.maxDistance);
    }

    // Fit the splits to the measured depth range; without a usable range the cascades use fixed splits.
    std::optional<ShadowDepthRange> sdsmRange;
    if (cfg.useSdsm && SdsmGroupEnabled(f) && f.ShaderReady(m_depthReduceShader)) {
        std::optional<ShadowDepthSample> sample;
        if (const auto latest = view.depthRange.Latest(f)) {
            if (const auto range = DecodeDepthRange(latest->value)) {
                sample = ShadowDepthSample{ *range, latest->frame };
            }
        }
        view.sdsm = UpdateSdsmState(view.sdsm, sample, frustum, cfg, f.FrameIndex(), f.DeltaSeconds());
        if (view.sdsm.valid && f.FrameIndex() - view.sdsm.lastSampleFrame <= kSdsmMaxHoldFrames) {
            sdsmRange = view.sdsm.used;
        }
    } else {
        view.sdsm = ShadowSdsmState{};
    }

    const ShadowCascadeSet set = BuildShadowCascades(frustum, sun.directionTowardSun, cfg, resolution, sdsmRange);
    snap.cascades = set;
    if (!set.valid) {
        finish(ShadowFrameStatus::NoValidCamera);
        return;
    }

    // A toggled-off cascade pass voids the whole set, so no cascade is drawn then.
    bool allEnabled = true;
    for (std::uint32_t c = 0; c < set.count; ++c) {
        allEnabled = allEnabled && f.PassEnabled(kCascadeDrawNames[c]);
    }

    // The Map channel is published only when every active cascade pass was recorded.
    ShadowMapData data;
    data.resolution = resolution;
    data.cascades = set;
    ShadowFrameStatus failure = allEnabled ? ShadowFrameStatus::Ok : ShadowFrameStatus::PassDisabled;
    const auto fail = [&](ShadowFrameStatus status) {
        if (failure == ShadowFrameStatus::Ok) {
            failure = status;
        }
    };

    for (std::uint32_t c = 0; allEnabled && c < set.count; ++c) {
        const fx::Texture map = view.maps[c].Acquire(f, fx::DepthOnly{ resolution });
        if (!map.IsValid()) {
            fail(ShadowFrameStatus::TargetFailed);
            continue;
        }
        data.maps[c] = map;

        // The cluster chain plans first so the CPU draw skips what it takes over; it loads the map the CPU draw clears.
        // A plan that is not ready leaves the CPU draw complete.
        const fx::ClusterCamera cascadeCamera = fx::MakeClusterCamera(set.cascades[c].viewProjection, Vec3{});
        fx::ClusterDrawChain clusterChain = f.ClusterDraw(m_clusterState, c);
        clusterChain.Instances(f.ClusterCandidates())
            .Camera(cascadeCamera)
            .CullCamera(cascadeCamera)
            .Culling(cfg.cullCasters ? ClusterCullFlags::Frustum : ClusterCullFlags::None)
            .DepthOnlyTarget(map);
        const fx::ClusterPlan clusterPlan = clusterChain.Plan();

        fx::SceneDrawChain chain = f.SceneDraw(kCascadeDrawNames[c]);
        chain.Shaders(m_depthShaders).Camera(set.cascades[c].viewProjection).ClearDepthTarget(map, 1.0f);
        if (cfg.cullCasters) {
            chain.CullToFrustum(cfg.casterCullPaddingFraction);
        }
        if (clusterPlan.ready) {
            chain.Exclude(clusterPlan.exclusion);
        }
        if (!chain.Record()) {
            fail(ShadowFrameStatus::ShadersNotReady);
            continue;
        }
        if (clusterPlan.ready) {
            clusterChain.Record();
        }
    }

    // Unused cascades give their memory back.
    for (std::uint32_t c = set.count; c < kMaxShadowCascades; ++c) {
        if (f.PassEnabled(kCascadeDrawNames[c])) {
            view.maps[c].Trim(f);
        }
    }

    snap.resolutionInUse = resolution;
    if (failure == ShadowFrameStatus::Ok) {
        for (std::uint32_t c = 0; c < set.count; ++c) {
            f.PublishToScene(m_cascadeSlots[c], data.maps[c]);
        }
        m_mapData.Publish(f, data);
        m_receiverData.Publish(f, MakeSunShadowReceiver(data, cfg));
    }
    finish(failure);
}

void ShadowFeature::DepthRangePass(fx::PostOpaqueFrame& f)
{
    const ShadowSettings& cfg = m_settings;
    if (HasNoPixels(f.View()) || !f.View().invViewProjectionValid || !cfg.useSdsm || !SdsmGroupEnabled(f)
        || !f.ShaderReady(m_depthReduceShader)) {
        return;
    }
    const ShadowViewFrustum frustum = ExtractViewFrustum(f.View().eye, f.View().invViewProjection);
    if (!frustum.valid) {
        return;
    }
    ShadowViewState& view = m_views.For(f.Scope());

    DepthReducePush push;
    push.invViewProjection = f.View().invViewProjection;
    push.eyeAndPad[0] = frustum.eye.x;
    push.eyeAndPad[1] = frustum.eye.y;
    push.eyeAndPad[2] = frustum.eye.z;
    push.forwardAndPad[0] = frustum.forward.x;
    push.forwardAndPad[1] = frustum.forward.y;
    push.forwardAndPad[2] = frustum.forward.z;

    // Without the clear no reduce may run: its result would be read against stale buffer contents.
    if (!f.ClearReadback("Shadow.DepthRange.Clear", view.depthRange, 0xFFFFFFFFu)) {
        return;
    }
    const bool dispatched = f.Compute(m_depthReduceShader, "Shadow.DepthRange.Reduce")
                                .Push(push)
                                .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                                .BindOutput("reduceResult", view.depthRange)
                                .Dispatch(f.View().extent);
    if (!dispatched) {
        f.ReportOnce("Shadow.DepthRange.Reduce was refused; the cascades keep fixed splits");
        return;
    }
    f.CopyReadbackToHost("Shadow.DepthRange.Readback", view.depthRange);
}

void ShadowFeature::MaskPass(fx::PostOpaqueFrame& f)
{
    ShadowViewState& view = m_views.For(f.Scope());
    const auto finish = [&](ShadowFrameStatus status) { view.snapshot.maskStatus = status; };

    const std::optional<ShadowMapData> map = m_mapData.Fetch(f);
    if (!ShadowDebugViewNeeded(m_settings) || !map || HasNoPixels(f.View()) || !f.View().invViewProjectionValid) {
        finish(ShadowFrameStatus::NotRendered);
        return;
    }
    const fx::Texture mask = view.mask.Acquire(f, f.View().extent, PixelFormat::R8_UNorm);
    const fx::Texture cascadeId = view.cascadeId.Acquire(f, f.View().extent, PixelFormat::R8_UNorm);
    if (!mask.IsValid() || !cascadeId.IsValid()) {
        finish(ShadowFrameStatus::TargetFailed);
        return;
    }

    const ShadowMaskParamsGpu params = MakeShadowMaskParams(*map, m_settings, f.View().invViewProjection);

    // Cascades past the active count read the first map; the shader never selects them.
    const auto& maps = map->maps;
    const auto pick = [&](std::uint32_t i) { return i < map->cascades.count ? maps[i] : maps[0]; };
    const bool dispatched = f.Compute(m_maskShader, "Shadow.Mask.Dispatch")
                                .Params(view.params, params)
                                .Bind("shadowDepth0", fx::Depth(pick(0)))
                                .Bind("shadowDepth1", fx::Depth(pick(1)))
                                .Bind("shadowDepth2", fx::Depth(pick(2)))
                                .Bind("shadowDepth3", fx::Depth(pick(3)))
                                .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                                .Bind("maskOutput", mask)
                                .Bind("cascadeOutput", cascadeId)
                                .Dispatch(f.View().extent);
    if (!dispatched) {
        f.ReportOnce("Shadow.Mask.Dispatch was refused");
        finish(ShadowFrameStatus::ShadersNotReady);
        return;
    }
    m_maskData.Publish(f, mask);
    m_cascadeIdData.Publish(f, cascadeId);
    finish(ShadowFrameStatus::Ok);
}

void ShadowFeature::CompositePass(fx::CompositeFrame& f)
{
    const ShadowSettings& cfg = m_settings;
    if (!ShadowDebugViewNeeded(cfg)) {
        return;
    }
    const std::optional<fx::Texture> mask = m_maskData.Fetch(f);
    const std::optional<fx::Texture> cascadeId = m_cascadeIdData.Fetch(f);
    if (!mask || !cascadeId) {
        return;
    }
    const CompositeParams params{ cfg.strength, static_cast<std::uint32_t>(cfg.debugView), cfg.cascadeCount, 0u };
    const bool drawn = f.Fullscreen(m_compositeShader, "Shadow.Composite.Draw")
                           .Bind("sceneColor", f.SceneColor())
                           .Bind("shadowMask", *mask)
                           .Bind("cascadeId", *cascadeId)
                           .Push(params)
                           .Acknowledge(f.SceneDepth())
                           .Target(f.PrivateTarget())
                           .Draw();
    if (!drawn) {
        f.ReportOnce("Shadow.Composite.Draw was refused");
    }
}

GTE_REGISTER_RENDER_FEATURE("Shadow", ShadowFeature);

} // namespace gte
