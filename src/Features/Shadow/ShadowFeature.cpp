#include "ShadowFeature.h"

#include "../../Core/PixelFormat.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

#include <algorithm>
#include <optional>

namespace gte {

namespace {

constexpr std::int32_t kCascadesPriority = 10;
constexpr std::int32_t kDepthRangePriority = 100;
constexpr std::int32_t kMaskPriority = 0;
constexpr std::int32_t kCompositePriority = -500;

// A snapshot older than this many frames (against the newest frame of any view) counts as not rendered.
constexpr std::uint64_t kSnapshotStaleFrames = 2;

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

std::uint64_t MapBytes(const std::array<std::uint32_t, kMaxShadowCascades>& extents) noexcept
{
    std::uint64_t bytes = 0;
    for (const std::uint32_t side : extents) {
        bytes += static_cast<std::uint64_t>(side) * side * 4u; // 32-bit depth.
    }
    return bytes;
}

} // namespace

ShadowFeature::ShadowFeature(fx::FeatureHost&) { }

void ShadowFeature::Setup(fx::FeatureSetup& s)
{
    // Each gate lists the pass names shown in the Render Graph panel; a callback runs while any of them is enabled.
    s.PreOpaque("Shadow.Cascades", kCascadesPriority, [this](fx::PreOpaqueFrame& f) { CascadesPass(f); },
        fx::Gate{ "Shadow.Cascade0.Draw", "Shadow.Cascade1.Draw", "Shadow.Cascade2.Draw", "Shadow.Cascade3.Draw" });
    s.PostOpaque("Shadow.DepthRange", kDepthRangePriority, [this](fx::PostOpaqueFrame& f) { DepthRangePass(f); },
        fx::Gate{ "Shadow.DepthRange.Reduce" });
    s.PostOpaque("Shadow.Mask", kMaskPriority, [this](fx::PostOpaqueFrame& f) { MaskPass(f); },
        fx::Gate{ "Shadow.Mask.Dispatch" });
    s.PostComposite("Shadow.Composite", kCompositePriority, fx::Blend::ScreenSpaceMask,
        [this](fx::CompositeFrame& f) { CompositePass(f); }, fx::Gate{ "Shadow.Composite.Draw" });
}

void ShadowFeature::ApplySettings(const ShadowSettings& candidate)
{
    m_settings = SanitizeShadowSettings(candidate);
}

ShadowViewSnapshot ShadowFeature::Snapshot(rg::ViewScope scope) const
{
    ShadowViewSnapshot snapshot = m_views.For(scope).snapshot;
    if (m_newestFrame - snapshot.frameIndex > kSnapshotStaleFrames) {
        snapshot.status = ShadowFrameStatus::NotRendered;
    }
    return snapshot;
}

void ShadowFeature::CascadesPass(fx::PreOpaqueFrame& f)
{
    const ShadowSettings cfg = SanitizeShadowSettings(m_settings);
    const std::uint32_t resolution = cfg.mapResolution;
    ShadowViewState& view = m_views.For(f.Scope());
    m_newestFrame = std::max(m_newestFrame, f.FrameIndex());

    ShadowViewSnapshot snap;
    snap.frameIndex = f.FrameIndex();
    snap.deltaSeconds = f.DeltaSeconds();
    snap.cascadeBytes = MapBytes(view.mapExtent);
    const auto finish = [&](ShadowFrameStatus status) {
        snap.status = status;
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

    // Every enabled cascade pass is declared; the Map channel is published only when all of them were.
    ShadowMapData data;
    data.resolution = resolution;
    data.cascades = set;
    bool allRecorded = true;
    ShadowFrameStatus failure = ShadowFrameStatus::Ok;
    const auto fail = [&](ShadowFrameStatus status) {
        allRecorded = false;
        if (failure == ShadowFrameStatus::Ok) {
            failure = status;
        }
    };

    for (std::uint32_t c = 0; c < set.count; ++c) {
        if (!f.PassEnabled(kCascadeDrawNames[c])) {
            fail(ShadowFrameStatus::PassDisabled);
            continue;
        }
        const fx::Texture map = view.maps[c].Acquire(f, fx::DepthOnly{ resolution });
        if (!map.IsValid()) {
            fail(ShadowFrameStatus::TargetFailed);
            continue;
        }
        view.mapExtent[c] = resolution;
        data.maps[c] = map;

        fx::SceneDrawChain chain = f.SceneDraw(kCascadeDrawNames[c]);
        chain.Shaders(m_depthShaders).Camera(set.cascades[c].viewProjection).ClearDepthTarget(map, 1.0f);
        if (cfg.cullCasters) {
            chain.CullToFrustum(cfg.casterCullPaddingFraction);
        }
        if (!chain.Record()) {
            fail(ShadowFrameStatus::ShadersNotReady);
        }
    }

    // Unused cascades give their memory back.
    for (std::uint32_t c = set.count; c < kMaxShadowCascades; ++c) {
        if (f.PassEnabled(kCascadeDrawNames[c])) {
            view.maps[c].Trim(f);
            if (view.mapExtent[c] != 0) {
                view.mapExtent[c] = fx::OffscreenTarget::kTrimmedExtent;
            }
        }
    }

    snap.cascadeBytes = MapBytes(view.mapExtent);
    snap.resolutionInUse = resolution;
    if (allRecorded) {
        m_mapData.Publish(f, data);
    }
    finish(failure);
}

void ShadowFeature::DepthRangePass(fx::PostOpaqueFrame& f)
{
    const ShadowSettings cfg = SanitizeShadowSettings(m_settings);
    if (!f.View().invViewProjectionValid || !cfg.useSdsm || !SdsmGroupEnabled(f) || !f.ShaderReady(m_depthReduceShader)) {
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

    f.ClearReadback("Shadow.DepthRange.Clear", view.depthRange, 0xFFFFFFFFu);
    const bool dispatched = f.Compute(m_depthReduceShader, "Shadow.DepthRange.Reduce")
                                .Push(push)
                                .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                                .BindOutput("reduceResult", view.depthRange)
                                .Dispatch(f.View().extent);
    if (!dispatched) {
        f.ReportOnce("Shadow.DepthRange.Reduce was refused; the cascades keep fixed splits");
    }
    f.CopyReadbackToHost("Shadow.DepthRange.Readback", view.depthRange);
}

void ShadowFeature::MaskPass(fx::PostOpaqueFrame& f)
{
    const std::optional<ShadowMapData> map = m_mapData.Fetch(f);
    if (!map || !f.View().invViewProjectionValid) {
        return;
    }
    ShadowViewState& view = m_views.For(f.Scope());
    const fx::Texture mask = view.mask.Acquire(f, f.View().extent, PixelFormat::R8_UNorm);
    const fx::Texture cascadeId = view.cascadeId.Acquire(f, f.View().extent, PixelFormat::R8_UNorm);
    if (!mask.IsValid() || !cascadeId.IsValid()) {
        return;
    }

    const ShadowSettings cfg = SanitizeShadowSettings(m_settings);
    const ShadowMaskParamsGpu params = MakeShadowMaskParams(*map, cfg, f.View().invViewProjection);

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
    if (dispatched) {
        m_maskData.Publish(f, mask);
        m_cascadeIdData.Publish(f, cascadeId);
    }
}

void ShadowFeature::CompositePass(fx::CompositeFrame& f)
{
    const std::optional<fx::Texture> mask = m_maskData.Fetch(f);
    const std::optional<fx::Texture> cascadeId = m_cascadeIdData.Fetch(f);
    if (!mask || !cascadeId) {
        return;
    }
    const ShadowSettings cfg = SanitizeShadowSettings(m_settings);
    const CompositeParams params{ cfg.strength, static_cast<float>(cfg.debugView),
        static_cast<float>(cfg.cascadeCount), 0.0f };
    f.Fullscreen(m_compositeShader, "Shadow.Composite.Draw")
        .Bind("sceneColor", f.SceneColor())
        .Bind("shadowMask", *mask)
        .Bind("cascadeId", *cascadeId)
        .Push(params)
        .Acknowledge(f.SceneDepth())
        .Target(f.PrivateTarget())
        .Draw();
}

GTE_REGISTER_RENDER_FEATURE("Shadow", ShadowFeature);

} // namespace gte
