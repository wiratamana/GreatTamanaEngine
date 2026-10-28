#include "VolumetricLightFeature.h"

#include "VolumetricLightMath.h"
#include "../../Core/PixelFormat.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

#include <algorithm>
#include <optional>

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 45;
constexpr std::int32_t kVolumePriority = 60;
constexpr std::int32_t kCompositePriority = -250; // After the shadow darkening, so the mask never dims the shafts.

// A snapshot older than this many frames (against the newest frame of any view) counts as not rendered.
constexpr std::uint64_t kSnapshotStaleFrames = 2;

constexpr std::uint64_t kTexelBytes = 8; // RGBA16 float.
constexpr std::uint64_t kVolumeBytes =
    std::uint64_t{ kVolumetricLightGridWidth } * kVolumetricLightGridHeight * kVolumetricLightGridDepth * kTexelBytes;
constexpr std::uint64_t kAtlasBytes = std::uint64_t{ kVolumetricLightAtlasWidth } * kVolumetricLightAtlasHeight * kTexelBytes;

constexpr Extent2D kGridColumns{ kVolumetricLightGridWidth, kVolumetricLightGridHeight };

} // namespace

VolumetricLightFeature::VolumetricLightFeature(fx::FeatureHost&) { }

void VolumetricLightFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a toggle from freezing the frame clock the snapshots age against.
    s.PreOpaque("VolumetricLight.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); }, fx::Gate::Always());

    // The secondary view skips the volume and its composite by default.
    s.DefaultSecondaryViewEnabled(false);

    s.PostOpaque("VolumetricLight.Volume", kVolumePriority, [this](fx::PostOpaqueFrame& f) { VolumePass(f); },
        fx::Gate{ "VolumetricLight.Inject.Dispatch", "VolumetricLight.Temporal.Dispatch",
            "VolumetricLight.Integrate.Dispatch" });
    // The volume shades air with the sun cascades, so the cascades must exist even when the view shows no surface.
    s.ReadsChannelInEmptyViews("VolumetricLight.Volume", kSunShadowReceiverOwner, kSunShadowReceiverChannel);
    s.PostComposite("VolumetricLight.Composite", kCompositePriority, fx::Blend::AlphaOver,
        [this](fx::CompositeFrame& f) { CompositePass(f); },
        fx::Gate{ "VolumetricLight.Composite.Dispatch", "VolumetricLight.Composite.CopyToPrivateTarget" });
}

void VolumetricLightFeature::ApplySettings(const VolumetricLightSettings& candidate)
{
    m_settings = SanitizeVolumetricLightSettings(candidate);
}

VolumetricLightViewSnapshot VolumetricLightFeature::Snapshot(rg::ViewScope scope) const
{
    VolumetricLightViewSnapshot snapshot = m_views.For(scope).snapshot;
    const bool stale = snapshot.frameIndex > m_newestFrame || m_newestFrame - snapshot.frameIndex > kSnapshotStaleFrames;
    if (stale) {
        snapshot.status = VolumetricLightFrameStatus::NotRendered;
    }
    return snapshot;
}

VolumetricLightFeature::StageResult VolumetricLightFeature::RunInject(fx::PostOpaqueFrame& f,
    VolumetricLightViewState& view, const VolumetricLightFrameFacts& facts, const SunShadowReceiver* receiver)
{
    StageResult result;
    result.texture = view.raw.Acquire(f);
    if (!result.texture.IsValid()) {
        result.failure = VolumetricLightFrameStatus::TargetFailed;
        return result;
    }

    // Without cascades the shader never samples the maps, but every sampler needs a valid depth texture.
    const auto pick = [&](std::uint32_t i) {
        return receiver != nullptr ? receiver->maps[std::min(i, receiver->cascadeCount - 1)] : f.SceneDepth();
    };
    const bool dispatched = f.Compute(m_injectShader, "VolumetricLight.Inject.Dispatch")
                                .Params(view.injectParams, MakeVolumetricLightInjectParams(m_settings, receiver, facts))
                                .Bind("shadowDepth0", fx::Depth(pick(0)))
                                .Bind("shadowDepth1", fx::Depth(pick(1)))
                                .Bind("shadowDepth2", fx::Depth(pick(2)))
                                .Bind("shadowDepth3", fx::Depth(pick(3)))
                                .Bind("injectOutput", result.texture)
                                .Dispatch(kGridColumns, kVolumetricLightGridDepth);
    if (!dispatched) {
        f.ReportOnce("VolumetricLight.Inject.Dispatch was refused");
        result.failure = VolumetricLightFrameStatus::ShadersNotReady;
    }
    return result;
}

VolumetricLightFeature::TemporalResult VolumetricLightFeature::RunTemporal(fx::PostOpaqueFrame& f,
    VolumetricLightViewState& view, const VolumetricLightFrameFacts& facts, fx::Texture raw, std::uint64_t lightingKey)
{
    TemporalResult result;
    const VolumetricLightSettings& cfg = m_settings;
    if (!cfg.temporal || !f.PassEnabled("VolumetricLight.Temporal.Dispatch") || !f.ShaderReady(m_temporalShader)) {
        return result;
    }

    // Each target is acquired once per frame; a second request in the same frame is refused.
    const std::uint32_t slot = static_cast<std::uint32_t>(f.FrameIndex() & 1u);
    const Extent2D atlas = VolumetricLightAtlasExtent();
    const fx::Texture blended = view.blended.Acquire(f);
    const fx::Texture current = view.history[slot].Acquire(f, atlas, PixelFormat::RGBA16_Float);
    const fx::Texture previous = view.history[slot ^ 1u].Acquire(f, atlas, PixelFormat::RGBA16_Float);
    if (!blended.IsValid() || !current.IsValid() || !previous.IsValid()) {
        return result;
    }

    const bool usable = VolumetricLightHistoryUsable(view.previous, facts.eye, facts.forward, facts.frameIndex, lightingKey);
    const float weight = usable ? VolumetricLightHistoryWeight(view.previous, facts.sunDirection, cfg.historyBlend) : 0.0f;
    const bool dispatched =
        f.Compute(m_temporalShader, "VolumetricLight.Temporal.Dispatch")
            .Params(view.temporalParams, MakeVolumetricLightTemporalParams(cfg, facts, view.previous, weight, usable))
            .Bind("rawVolume", raw)
            .Bind("historyAtlas", previous)
            .Bind("blendedOutput", blended)
            .Bind("atlasOutput", current)
            .Dispatch(kGridColumns, kVolumetricLightGridDepth);
    if (!dispatched) {
        f.ReportOnce("VolumetricLight.Temporal.Dispatch was refused");
        return result;
    }
    result.blended = blended;
    result.historyUsed = usable && weight > 0.0f;
    return result;
}

VolumetricLightFeature::StageResult VolumetricLightFeature::RunIntegrate(
    fx::PostOpaqueFrame& f, VolumetricLightViewState& view, float maxDistance, fx::Texture lit)
{
    StageResult result;
    result.texture = view.integrated.Acquire(f);
    if (!result.texture.IsValid()) {
        result.failure = VolumetricLightFrameStatus::TargetFailed;
        return result;
    }
    const bool dispatched =
        f.Compute(m_integrateShader, "VolumetricLight.Integrate.Dispatch")
            .Push(VolumetricLightIntegratePush{ maxDistance, m_settings.depthExponent })
            .Bind("blendedVolume", lit)
            .Bind("integratedOutput", result.texture)
            .Dispatch(kGridColumns);
    if (!dispatched) {
        f.ReportOnce("VolumetricLight.Integrate.Dispatch was refused");
        result.failure = VolumetricLightFrameStatus::ShadersNotReady;
    }
    return result;
}

void VolumetricLightFeature::VolumePass(fx::PostOpaqueFrame& f)
{
    // Shadow cascade data of this view; it may be missing.
    const std::optional<SunShadowReceiver> published = m_receiver.Fetch(f);

    VolumetricLightViewState& view = m_views.For(f.Scope());
    const fx::View& v = f.View();
    const VolumetricLightSettings& cfg = m_settings;

    VolumetricLightViewSnapshot snap;
    snap.frameIndex = f.FrameIndex();

    // Every early exit drops the history: it was not written this frame, so the next frame must not read it.
    const auto bail = [&](VolumetricLightFrameStatus status) {
        view.previous = VolumetricLightHistory{};
        snap.status = status;
        view.snapshot = snap;
    };

    if (!cfg.enabled) {
        bail(VolumetricLightFrameStatus::Disabled);
        return;
    }
    if (v.extent.width == 0 || v.extent.height == 0 || !v.invViewProjectionValid) {
        bail(VolumetricLightFrameStatus::NoValidCamera);
        return;
    }
    if (!f.PassEnabled("VolumetricLight.Inject.Dispatch")) {
        bail(VolumetricLightFrameStatus::PassDisabled);
        return;
    }
    if (!f.ShaderReady(m_injectShader) || !f.ShaderReady(m_integrateShader)) {
        bail(VolumetricLightFrameStatus::ShadersNotReady);
        return;
    }

    const bool hasReceiver = published.has_value() && SunShadowReceiverUsable(*published);
    const SunShadowReceiver* receiver = hasReceiver ? &*published : nullptr;
    snap.receiver = hasReceiver ? VolumetricLightReceiverState::Used
        : published.has_value() ? VolumetricLightReceiverState::Rejected
                                : VolumetricLightReceiverState::Missing;
    if (snap.receiver == VolumetricLightReceiverState::Rejected) {
        f.ReportOnce("VolumetricLight: the shadow receiver data was rejected");
    }

    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
    view.windOffset = VolumetricLightAdvanceWind(view.windOffset, cfg.windX, cfg.windZ, f.DeltaSeconds());

    VolumetricLightFrameFacts facts;
    facts.viewProjection = v.viewProjection;
    facts.invViewProjection = v.invViewProjection;
    facts.eye = v.eye;
    facts.forward = hasReceiver ? receiver->cameraForward : DeriveVolumetricLightForward(v.invViewProjection);
    facts.sunDirection = sun.directionTowardSun;
    facts.sunIlluminance = sun.sunIlluminance;
    const std::optional<SunLightModulation> sunLight = m_sunLight.Fetch(f);
    const SunLightModulation light = sunLight ? SanitizeSunLightModulation(*sunLight) : SunLightModulation{};
    facts.atmosphereLight = light.active;
    facts.sunTint = light.sunTint;
    facts.ambientScale = light.ambientScale;
    facts.windOffset = view.windOffset;
    facts.frameIndex = f.FrameIndex();
    facts.maxDistance = VolumetricLightEffectiveMaxDistance(
        cfg, hasReceiver, hasReceiver ? receiver->splitFar[receiver->cascadeCount - 1] : 0.0f);
    if (cfg.jitter) {
        snap.jitterIndex = static_cast<std::uint32_t>(f.FrameIndex() % kVolumetricLightHaltonCycle);
        facts.jitter = VolumetricLightHaltonJitter(snap.jitterIndex);
    }

    const StageResult injected = RunInject(f, view, facts, receiver);
    if (injected.failure != VolumetricLightFrameStatus::Ok) {
        bail(injected.failure);
        return;
    }

    // Only a frame whose temporal pass ran becomes history.
    const std::uint64_t lightingKey = VolumetricLightLightingKey(cfg, facts.atmosphereLight);
    const TemporalResult temporal = RunTemporal(f, view, facts, injected.texture, lightingKey);
    const bool blended = temporal.blended.IsValid();
    view.previous = blended ? MakeVolumetricLightHistory(facts, cfg.depthExponent, lightingKey, view.previous)
                            : VolumetricLightHistory{};

    snap.cascadeCount = hasReceiver ? receiver->cascadeCount : 0;
    snap.atmosphereLight = facts.atmosphereLight;
    snap.sunTint = facts.sunTint;
    snap.ambientScale = facts.ambientScale;
    snap.effectiveMaxDistance = facts.maxDistance;
    snap.historyUsed = temporal.historyUsed;
    // Raw volume, plus the blended volume and both history atlases when the temporal pass ran.
    snap.textureBytes = kVolumeBytes * (blended ? 2u : 1u) + (blended ? 2u * kAtlasBytes : 0u);

    // A disabled integrate pass publishes nothing, so the composite has nothing to apply.
    if (!f.PassEnabled("VolumetricLight.Integrate.Dispatch")) {
        snap.status = VolumetricLightFrameStatus::PassDisabled;
        view.snapshot = snap;
        return;
    }
    const StageResult integrated =
        RunIntegrate(f, view, facts.maxDistance, blended ? temporal.blended : injected.texture);
    if (integrated.failure != VolumetricLightFrameStatus::Ok) {
        bail(integrated.failure);
        return;
    }
    snap.textureBytes += kVolumeBytes;

    // Published only after the integrate dispatch was declared.
    m_result.Publish(f, VolumetricLightResult{ integrated.texture, facts.maxDistance, cfg.depthExponent },
        { integrated.texture });
    snap.status = VolumetricLightFrameStatus::Ok;
    view.snapshot = snap;
}

void VolumetricLightFeature::CompositePass(fx::CompositeFrame& f)
{
    // Every return below declares nothing, so the compositor skips this entry.
    const VolumetricLightSettings& cfg = m_settings;
    if (!f.View().invViewProjectionValid || !(cfg.strength > 0.0f) || !f.PassEnabled("VolumetricLight.Composite.Dispatch")
        || !f.PassEnabled("VolumetricLight.Composite.CopyToPrivateTarget") || !f.ShaderReady(m_compositeShader)) {
        return;
    }
    const std::optional<VolumetricLightResult> result = m_result.Fetch(f);
    if (!result || !result->integrated.IsValid() || !(result->maxDistance > 0.0f)) {
        return;
    }
    const fx::Texture output = m_composited.Acquire(f, f.Extent(), PixelFormat::RGBA16_Float);
    if (!output.IsValid()) {
        return;
    }

    const VolumetricLightCompositePush push =
        MakeVolumetricLightCompositePush(cfg, f.View().invViewProjection, f.View().eye, *result, f.FrameIndex());
    const bool dispatched = f.Compute(m_compositeShader, "VolumetricLight.Composite.Dispatch")
                                .Push(push)
                                .Bind("sourceColor", f.SceneColor())
                                .Bind("sourceDepth", fx::Depth(f.SceneDepth()))
                                .Bind("integratedVolume", result->integrated)
                                .Bind("destinationImage", output)
                                .Dispatch(f.Extent());
    if (!dispatched) {
        f.ReportOnce("VolumetricLight.Composite.Dispatch was refused");
        return;
    }
    f.ExposeAsViewOutput(m_composited, output);
    f.Blit("VolumetricLight.Composite.CopyToPrivateTarget", output, f.PrivateTarget(), fx::BlitFilter::Linear);
}

GTE_REGISTER_RENDER_FEATURE("VolumetricLight", VolumetricLightFeature);

} // namespace gte
