#include "AtmosphereFeature.h"

#include "AtmosphereMath.h"
#include "AtmosphereParameters.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gte {

static_assert(std::string_view(kSunLightModulationOwner) == std::string_view(kAtmosphereFeatureName));

namespace {

constexpr std::int32_t kSharedLutPriority = -10;
constexpr std::int32_t kViewLutPriority = 0;
constexpr std::int32_t kAerialVolumePriority = 20;
constexpr std::int32_t kSkyBackgroundPriority = 100;
constexpr std::int32_t kCompositePriority = -1000;

constexpr Extent2D kTransmittanceSize{ 256, 256 };
constexpr Extent2D kMultiScatteringSize{ 64, 64 };
constexpr Extent2D kSkyViewSize{ 200, 100 };
// Each aerial perspective column marches through every Z slice itself, so only X and Y are dispatched.
constexpr Extent2D kAerialDispatchSize{ kAerialVolumeSize.width, kAerialVolumeSize.height };

// Push constants of AtmosphereAerialPerspectiveComposite.comp.
struct CompositePush {
    Mat4 invViewProjection;
    Vec4 cameraWorldPositionAndScale; // xyz = camera world position, w = world units per km.
    Vec4 strengthMaxDistanceExponent; // x = strength, y = max distance (km), z = depth exponent, w = unused.
    Vec4 skyIntensityAndPad;          // x = sky intensity.
};
static_assert(sizeof(CompositePush) == 112, "layout differs from AtmosphereAerialPerspectiveComposite.comp");

// Push constants of AtmosphereSkyBackground.frag.
struct SkyBackgroundPush {
    Mat4 invViewProjection;
    float eyeHeightKm;
    float planetRadiusKm;
    float skyIntensity;
    float pad;
};
static_assert(sizeof(SkyBackgroundPush) == 80, "layout differs from AtmosphereSkyBackground.frag");

AtmosphereParametersGpu MakeParameters(const AtmosphereSettings& settings)
{
    AtmosphereParametersGpu params;
    params.rayleighScattering = settings.rayleighScattering;
    params.rayleighDensityExpScale = ReciprocalScaleHeightFromKm(settings.rayleighScaleHeightKm);
    params.mieScattering = settings.mieScattering;
    params.miePhaseG = settings.miePhaseG;
    params.mieAbsorption = settings.mieAbsorption;
    params.mieDensityExpScale = ReciprocalScaleHeightFromKm(settings.mieScaleHeightKm);
    params.ozoneAbsorption = settings.ozoneAbsorption;
    params.ozoneTentCenterKm = settings.ozoneTentCenterKm;
    params.groundAlbedo = settings.groundAlbedo;
    params.ozoneTentHalfWidthKm = settings.ozoneTentHalfWidthKm;
    params.planetRadiusKm = settings.planetRadiusKm;
    params.atmosphereThicknessKm = settings.atmosphereThicknessKm;
    params.multiScatteringStrength = settings.multiScatteringStrength;
    return params;
}

// The view's inverse view-projection must be valid (checked by the caller).
AtmosphereFrameUniforms MakeFrameUniforms(Registry& scene, const fx::View& view, const AtmosphereSettings& settings)
{
    AtmosphereFrameUniforms uniforms;

    // Planet-centered frame: only the height above the ground matters, never the horizontal position.
    const Vec3 eyeKm = WorldPositionToAtmosphereSpaceKm(view.eye);
    const float heightAboveGroundKm = std::max(eyeKm.y, 0.0f);
    uniforms.cameraPositionAtmosphere = Vec3(0.0f, settings.planetRadiusKm + heightAboveGroundKm, 0.0f);

    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(scene);
    uniforms.sunDirection = sun.directionTowardSun;
    uniforms.sunIlluminance = sun.sunIlluminance;

    uniforms.invViewProjection = view.invViewProjection;
    uniforms.aerialPerspectiveMaxDistanceKm = settings.aerialPerspectiveMaxDistanceKm;
    uniforms.aerialPerspectiveDepthExponent = settings.aerialPerspectiveDepthExponent;
    uniforms.aerialPerspectiveSamplesPerSliceAsFloat = static_cast<float>(settings.aerialPerspectiveSamplesPerSlice);
    uniforms.aerialPerspectiveScatteringExaggeration = settings.aerialPerspectiveScatteringExaggeration;
    return uniforms;
}

} // namespace

AtmosphereFeature::AtmosphereFeature(fx::FeatureHost&) { }

void AtmosphereFeature::ApplySettings(const AtmosphereSettings& candidate)
{
    m_settings = SanitizeAtmosphereSettings(candidate);
}

void AtmosphereFeature::Setup(fx::FeatureSetup& s)
{
    s.PreOpaqueShared("AtmosphereSharedLut", kSharedLutPriority, [this](fx::SharedFrame& f) { SharedLuts(f); },
        fx::Gate{ "AtmosphereTransmittanceLutPass", "AtmosphereMultiScatteringLutPass" });
    s.PreOpaque("AtmosphereViewLut", kViewLutPriority, [this](fx::PreOpaqueFrame& f) { ViewLuts(f); },
        fx::Gate{ "AtmosphereSkyViewLutPass" });
    // Not a render pass: Always() keeps a toggle from silencing the sun light record.
    s.PreOpaque("Atmosphere.SunLight", kSunLightModulationPriority,
        [this](fx::PreOpaqueFrame& f) { PublishSunLight(f); }, fx::Gate::Always());
    // After the sun cascades exist (PreOpaque), before the composite reads the volume.
    s.PostOpaque("AtmosphereAerialVolume", kAerialVolumePriority, [this](fx::PostOpaqueFrame& f) { AerialVolume(f); },
        fx::Gate{ "AtmosphereAerialPerspectiveVolumePass" });
    s.PostOpaque("DrawSkyBackground", kSkyBackgroundPriority, [this](fx::PostOpaqueFrame& f) { SkyBackground(f); });
    s.PostComposite("AtmosphereComposite", kCompositePriority, fx::Blend::AlphaOver,
        [this](fx::CompositeFrame& f) { Composite(f); },
        fx::Gate{ "AtmosphereAerialPerspectiveCompositePass", "AtmosphereComposite.CopyToPrivateTarget" });
}

void AtmosphereFeature::SharedLuts(fx::SharedFrame& f)
{
    const AtmosphereParametersGpu params = MakeParameters(m_settings);

    if (!f.PassEnabled("AtmosphereTransmittanceLutPass")) {
        return;
    }
    const fx::Texture transmittance = m_transmittance.Acquire(f, kTransmittanceSize, PixelFormat::RGBA8_UNorm);
    if (!transmittance.IsValid()) {
        return;
    }
    const bool transmittanceDone = f.Compute(m_transmittanceShader, "AtmosphereTransmittanceLutPass")
                                       .Params(m_sharedParams, params)
                                       .Bind("transmittanceLutImage", transmittance)
                                       .Dispatch(kTransmittanceSize);
    if (!transmittanceDone) {
        return;
    }

    if (!f.PassEnabled("AtmosphereMultiScatteringLutPass")) {
        return;
    }
    const fx::Texture multiScattering = m_multiScattering.Acquire(f, kMultiScatteringSize, PixelFormat::RGBA16_Float);
    if (!multiScattering.IsValid()) {
        return;
    }
    const bool multiScatteringDone = f.Compute(m_multiScatteringShader, "AtmosphereMultiScatteringLutPass")
                                         .Params(m_sharedParams, params)
                                         .Bind("transmittanceLut", transmittance)
                                         .Bind("multiScatteringLutImage", multiScattering)
                                         .Dispatch(kMultiScatteringSize);
    if (!multiScatteringDone) {
        return;
    }
    m_sharedLuts.Publish(f, AtmosphereSharedLuts{ transmittance, multiScattering, params }, { transmittance, multiScattering });
}

void AtmosphereFeature::ViewLuts(fx::PreOpaqueFrame& f)
{
    if (!f.View().invViewProjectionValid) {
        return;
    }
    const std::optional<AtmosphereSharedLuts> shared = m_sharedLuts.Fetch(f);
    if (!shared) {
        return;
    }
    AtmosphereViewState& view = m_views.For(f.Scope());
    AtmosphereViewLuts luts;
    luts.frameUniforms = MakeFrameUniforms(f.Scene(), f.View(), m_settings);

    if (f.PassEnabled("AtmosphereSkyViewLutPass")) {
        const fx::Texture skyView = view.skyView.Acquire(f, kSkyViewSize, PixelFormat::RGBA16_Float);
        if (skyView.IsValid()) {
            const bool done = f.Compute(m_skyViewShader, "AtmosphereSkyViewLutPass")
                                  .Params(m_sharedParams, shared->parameters)
                                  .Params("frame", view.skyFrameParams, luts.frameUniforms)
                                  .Bind("transmittanceLut", shared->transmittance)
                                  .Bind("multiScatteringLut", shared->multiScattering)
                                  .Bind("destinationImage", skyView)
                                  .Dispatch(kSkyViewSize);
            if (done) {
                luts.skyView = skyView;
                f.Keep(skyView);
            }
        }
    }
    m_viewLuts.Publish(f, luts);
}

void AtmosphereFeature::AerialVolume(fx::PostOpaqueFrame& f)
{
    const fx::View& v = f.View();
    AtmosphereViewState& view = m_views.For(f.Scope());
    view.aerialSnapshot = AtmosphereAerialSnapshot{};
    view.aerialSnapshot.frameIndex = f.FrameIndex();

    if (v.extent.width == 0 || v.extent.height == 0 || !v.invViewProjectionValid
        || !f.PassEnabled("AtmosphereAerialPerspectiveVolumePass")) {
        return;
    }
    const std::optional<AtmosphereSharedLuts> shared = m_sharedLuts.Fetch(f);
    if (!shared) {
        return;
    }

    AtmosphereFrameUniforms frame = MakeFrameUniforms(f.Scene(), v, m_settings);
    AtmosphereAerialSnapshot snap = view.aerialSnapshot;

    // Sun cascades of this view; air stays unshadowed when they are missing, rejected or from another sun.
    const std::optional<SunShadowReceiver> published = m_receiver.Fetch(f);
    snap.receiver = ClassifyAerialReceiver(published ? &*published : nullptr, frame.sunDirection);
    if (snap.receiver == AerialReceiverState::Rejected) {
        f.ReportOnce("Atmosphere: the sun shadow receiver data was rejected");
    } else if (snap.receiver == AerialReceiverState::Mismatch) {
        f.ReportOnce("Atmosphere: the sun shadow receiver does not match this frame's sun");
    }
    const SunShadowReceiver* receiver = snap.receiver == AerialReceiverState::Used ? &*published : nullptr;

    const AerialScaleResult resolved = ResolveAerialScale(view.scaleState, receiver, m_settings, f.FrameIndex());
    frame.aerialPerspectiveMaxDistanceKm = resolved.scale.maxDistanceKm;
    frame.aerialPerspectiveScatteringExaggeration = resolved.scale.exaggeration;
    const AerialShadowParamsGpu shadowParams = MakeAerialShadowParams(receiver, resolved.scale.shadowBlend);
    const bool sampled = shadowParams.counts[0] > 0;

    snap.maxDistanceKm = resolved.scale.maxDistanceKm;
    snap.exaggeration = resolved.scale.exaggeration;
    snap.shadowBlend = resolved.scale.shadowBlend;
    snap.cascadeCount = shadowParams.counts[0];
    snap.scaleHeld = resolved.held;
    view.aerialSnapshot = snap;

    const fx::Texture volume = view.aerialVolume.Acquire(f);
    if (!volume.IsValid()) {
        return;
    }

    // No cascades: the lean variant runs with 2 samplers and writes no shaft delta.
    fx::Texture shaftDelta;
    bool done = false;
    if (sampled) {
        shaftDelta = view.shaftDelta.Acquire(
            f, Extent2D{ kAerialVolumeSize.width, kAerialVolumeSize.height }, PixelFormat::RGBA16_Float);
        if (!shaftDelta.IsValid()) {
            return;
        }
        const auto map = [&](std::uint32_t i) { return receiver->maps[std::min(i, receiver->cascadeCount - 1)]; };
        done = f.Compute(m_aerialVolumeShader, "AtmosphereAerialPerspectiveVolumePass")
                   .Params(view.aerialAtmosphereParams, shared->parameters)
                   .Params("frame", view.aerialFrameParams, frame)
                   .Params("shadow", view.aerialShadowParams, shadowParams)
                   .Bind("transmittanceLut", shared->transmittance)
                   .Bind("multiScatteringLut", shared->multiScattering)
                   .Bind("shadowDepth0", fx::Depth(map(0)))
                   .Bind("shadowDepth1", fx::Depth(map(1)))
                   .Bind("shadowDepth2", fx::Depth(map(2)))
                   .Bind("shadowDepth3", fx::Depth(map(3)))
                   .Bind("destinationVolume", volume)
                   .Bind("destinationShaftDelta", shaftDelta)
                   .Dispatch(kAerialDispatchSize);
    } else {
        done = f.Compute(m_aerialVolumeUnshadowedShader, "AtmosphereAerialPerspectiveVolumePass")
                   .Params(view.aerialAtmosphereParams, shared->parameters)
                   .Params("frame", view.aerialFrameParams, frame)
                   .Bind("transmittanceLut", shared->transmittance)
                   .Bind("multiScatteringLut", shared->multiScattering)
                   .Bind("destinationVolume", volume)
                   .Dispatch(kAerialDispatchSize);
    }
    if (!done) {
        f.ReportOnce("AtmosphereAerialPerspectiveVolumePass was refused");
        return;
    }
    // The keep list stops the graph from culling a pass whose outputs nothing reads this frame.
    const AtmosphereAerialResult result{ volume, shaftDelta, frame.aerialPerspectiveMaxDistanceKm,
        frame.aerialPerspectiveDepthExponent };
    if (sampled) {
        m_aerial.Publish(f, result, { volume, shaftDelta });
    } else {
        m_aerial.Publish(f, result, { volume });
    }
}

void AtmosphereFeature::PublishSunLight(fx::PreOpaqueFrame& f)
{
    SunLightModulation light;
    if (m_settings.sceneLightFollowsAtmosphere) {
        const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
        const float heightKm = std::max(WorldPositionToAtmosphereSpaceKm(f.View().eye).y, 0.0f);
        light = ComputeAtmosphereSunLight(MakeParameters(m_settings), heightKm, sun.directionTowardSun.y);
    }
    m_sunLight.Publish(f, light);
    m_views.For(f.Scope()).sunLight = SunLightModulationSnapshot{ light, f.FrameIndex() };
}

void AtmosphereFeature::SkyBackground(fx::PostOpaqueFrame& f)
{
    if (!f.View().invViewProjectionValid) {
        return;
    }
    const std::optional<AtmosphereViewLuts> luts = m_viewLuts.Fetch(f);
    const std::optional<AtmosphereSharedLuts> shared = m_sharedLuts.Fetch(f);
    if (!luts || !shared || !luts->skyView.IsValid()) {
        return;
    }

    SkyBackgroundPush push{};
    push.invViewProjection = f.View().invViewProjection;
    push.eyeHeightKm = luts->frameUniforms.cameraPositionAtmosphere.y;
    push.planetRadiusKm = shared->parameters.planetRadiusKm;
    push.skyIntensity = m_settings.skyIntensity;
    f.ViewBackground(m_skyBackgroundShader, "DrawSkyBackground").Bind("skyViewLut", luts->skyView).Push(push).Draw();
}

void AtmosphereFeature::Composite(fx::CompositeFrame& f)
{
    if (!f.View().invViewProjectionValid || !f.PassEnabled("AtmosphereAerialPerspectiveCompositePass")) {
        return;
    }
    const std::optional<AtmosphereAerialResult> aerial = m_aerial.Fetch(f);
    if (!aerial || !aerial->volume.IsValid()) {
        return;
    }
    const fx::Texture output = m_composited.Acquire(f, f.Extent(), PixelFormat::RGBA16_Float);
    if (!output.IsValid()) {
        return;
    }

    const fx::View& view = f.View();
    CompositePush push{};
    push.invViewProjection = view.invViewProjection;
    push.cameraWorldPositionAndScale = Vec4(view.eye, kWorldUnitsPerKilometer);
    push.strengthMaxDistanceExponent =
        Vec4(m_settings.aerialPerspectiveStrength, aerial->maxDistanceKm, aerial->depthExponent, 0.0f);
    push.skyIntensityAndPad = Vec4(m_settings.skyIntensity, 0.0f, 0.0f, 0.0f);

    // An invalid shaft delta means the volume was made without shadows, so sky pixels pass through.
    bool dispatched = false;
    if (aerial->shaftDelta.IsValid()) {
        dispatched = f.Compute(m_compositeShader, "AtmosphereAerialPerspectiveCompositePass")
                         .Push(push)
                         .Bind("sourceColor", f.SceneColor())
                         .Bind("sourceDepth", fx::Depth(f.SceneDepth()))
                         .Bind("aerialPerspectiveVolume", aerial->volume)
                         .Bind("aerialShaftDelta", aerial->shaftDelta)
                         .Bind("destinationImage", output)
                         .Dispatch(f.Extent());
    } else {
        dispatched = f.Compute(m_compositeUnshadowedShader, "AtmosphereAerialPerspectiveCompositePass")
                         .Push(push)
                         .Bind("sourceColor", f.SceneColor())
                         .Bind("sourceDepth", fx::Depth(f.SceneDepth()))
                         .Bind("aerialPerspectiveVolume", aerial->volume)
                         .Bind("destinationImage", output)
                         .Dispatch(f.Extent());
    }
    if (!dispatched) {
        return;
    }
    f.ExposeAsViewOutput(m_composited, output);
    f.Blit("AtmosphereComposite.CopyToPrivateTarget", output, f.PrivateTarget(), fx::BlitFilter::Linear);
}

GTE_REGISTER_RENDER_FEATURE("Atmosphere", AtmosphereFeature);

} // namespace gte
