#include "AtmosphereFeature.h"

#include "AtmosphereMath.h"
#include "AtmosphereParameters.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace gte {

namespace {

constexpr std::int32_t kSharedLutPriority = -10;
constexpr std::int32_t kViewLutPriority = 0;
constexpr std::int32_t kSkyBackgroundPriority = 100;
constexpr std::int32_t kCompositePriority = -1000;

constexpr Extent2D kTransmittanceSize{ 256, 256 };
constexpr Extent2D kMultiScatteringSize{ 64, 64 };
constexpr Extent2D kSkyViewSize{ 200, 100 };
constexpr Extent2D kAerialVolumeSize{ 128, 128 }; // Each column marches through every Z slice itself.

// Push constants of AtmosphereAerialPerspectiveComposite.comp.
struct CompositePush {
    float invViewProjection[16];
    float cameraWorldPositionAndScale[4]; // xyz = camera world position, w = world units per km.
    float strengthMaxDistanceExponentPad[4]; // x = strength, y = max distance (km), z = depth exponent.
};
static_assert(sizeof(CompositePush) == 96, "must match AtmosphereAerialPerspectiveComposite.comp");

// Push constants of AtmosphereSkyBackground.frag.
struct SkyBackgroundPush {
    float invViewProjection[16];
    float eyeHeightKm;
    float planetRadiusKm;
    float skyExposure;
    float pad;
};
static_assert(sizeof(SkyBackgroundPush) == 80, "must match AtmosphereSkyBackground.frag");

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

AtmosphereFrameUniforms MakeFrameUniforms(Registry& scene, const fx::View& view, const AtmosphereSettings& settings)
{
    AtmosphereFrameUniforms uniforms;

    // Planet-centered frame: only the height above the ground matters, never the horizontal position.
    const Vec3 eyeKm = WorldPositionToAtmosphereSpaceKm(view.eye);
    const float heightAboveGroundKm = std::max(eyeKm.y, 0.0f);
    uniforms.cameraPositionAtmosphere =
        Vec3(0.0f, MakeDefaultEarthAtmosphereParameters().planetRadiusKm + heightAboveGroundKm, 0.0f);

    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(scene);
    uniforms.sunDirection = sun.directionTowardSun;
    uniforms.sunIlluminance = sun.sunIlluminance;

    uniforms.invViewProjection = view.viewProjection.Inverse();
    uniforms.aerialPerspectiveMaxDistanceKm = settings.aerialPerspectiveMaxDistanceKm;
    uniforms.aerialPerspectiveDepthExponent = settings.aerialPerspectiveDepthExponent;
    uniforms.aerialPerspectiveSamplesPerSliceAsFloat = static_cast<float>(settings.aerialPerspectiveSamplesPerSlice);
    uniforms.aerialPerspectiveScatteringExaggeration = settings.aerialPerspectiveScatteringExaggeration;
    return uniforms;
}

} // namespace

AtmosphereFeature::AtmosphereFeature(fx::FeatureHost&) { }

void AtmosphereFeature::Setup(fx::FeatureSetup& s)
{
    s.PreOpaqueShared("AtmosphereSharedLut", kSharedLutPriority, [this](fx::SharedFrame& f) { SharedLuts(f); },
        fx::Gate{ "AtmosphereTransmittanceLutPass", "AtmosphereMultiScatteringLutPass" });
    s.PreOpaque("AtmosphereViewLut", kViewLutPriority, [this](fx::PreOpaqueFrame& f) { ViewLuts(f); },
        fx::Gate{ "AtmosphereSkyViewLutPass", "AtmosphereAerialPerspectiveVolumePass" });
    s.PostOpaque("DrawSkyBackground", kSkyBackgroundPriority, [this](fx::PostOpaqueFrame& f) { SkyBackground(f); });
    s.PostComposite("AtmosphereComposite", kCompositePriority, fx::Blend::AlphaOver,
        [this](fx::CompositeFrame& f) { Composite(f); },
        fx::Gate{ "AtmosphereAerialPerspectiveCompositePass", "AtmosphereComposite.CopyToPrivateTarget" });
    s.CountInViewStats("DrawSkyBackground");
}

void AtmosphereFeature::SharedLuts(fx::SharedFrame& f)
{
    const AtmosphereParametersGpu params = MakeParameters(m_settings);

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
    const std::optional<AtmosphereSharedLuts> shared = m_sharedLuts.Fetch(f);
    if (!shared) {
        return;
    }
    AtmosphereViewState& view = m_views.For(f.Scope());
    AtmosphereViewLuts luts;
    luts.frameUniforms = MakeFrameUniforms(f.Scene(), f.View(), m_settings);

    const fx::Texture skyView = view.skyView.Acquire(f, kSkyViewSize, PixelFormat::RGBA16_Float);
    if (skyView.IsValid()) {
        const bool done = f.Compute(m_skyViewShader, "AtmosphereSkyViewLutPass")
                              .Params(m_sharedParams, shared->parameters)
                              .Params("frame", view.frameParams, luts.frameUniforms)
                              .Bind("transmittanceLut", shared->transmittance)
                              .Bind("multiScatteringLut", shared->multiScattering)
                              .Bind("destinationImage", skyView)
                              .Dispatch(kSkyViewSize);
        if (done) {
            luts.skyView = skyView;
            f.Keep(skyView);
        }
    }

    const fx::Texture volume = view.aerialVolume.Acquire(f);
    if (volume.IsValid()) {
        const bool done = f.Compute(m_aerialVolumeShader, "AtmosphereAerialPerspectiveVolumePass")
                              .Params(m_sharedParams, shared->parameters)
                              .Params("frame", view.frameParams, luts.frameUniforms)
                              .Bind("transmittanceLut", shared->transmittance)
                              .Bind("multiScatteringLut", shared->multiScattering)
                              .Bind("destinationVolume", volume)
                              .Dispatch(kAerialVolumeSize);
        if (done) {
            luts.aerialVolume = volume;
        }
    }
    m_viewLuts.Publish(f, luts);
}

void AtmosphereFeature::SkyBackground(fx::PostOpaqueFrame& f)
{
    const std::optional<AtmosphereViewLuts> luts = m_viewLuts.Fetch(f);
    const std::optional<AtmosphereSharedLuts> shared = m_sharedLuts.Fetch(f);
    if (!luts || !shared || !luts->skyView.IsValid()) {
        return;
    }
    Mat4 invViewProjection;
    if (!f.View().viewProjection.TryInverse(invViewProjection)) {
        return;
    }

    SkyBackgroundPush push{};
    std::memcpy(push.invViewProjection, invViewProjection.Data(), sizeof(push.invViewProjection));
    push.eyeHeightKm = luts->frameUniforms.cameraPositionAtmosphere.y;
    push.planetRadiusKm = shared->parameters.planetRadiusKm;
    push.skyExposure = m_settings.skyExposure;
    f.ViewBackground(m_skyBackgroundShader, "DrawSkyBackground").Bind("skyViewLut", luts->skyView).Push(push).Draw();
}

void AtmosphereFeature::Composite(fx::CompositeFrame& f)
{
    if (!f.View().invViewProjectionValid) {
        return;
    }
    const std::optional<AtmosphereViewLuts> luts = m_viewLuts.Fetch(f);
    if (!luts || !luts->aerialVolume.IsValid()) {
        return;
    }
    const fx::Texture output = m_composited.Acquire(f, f.Extent(), PixelFormat::RGBA8_UNorm);
    if (!output.IsValid()) {
        return;
    }

    CompositePush push{};
    std::memcpy(push.invViewProjection, luts->frameUniforms.invViewProjection.Data(), sizeof(push.invViewProjection));
    push.cameraWorldPositionAndScale[0] = f.View().eye.x;
    push.cameraWorldPositionAndScale[1] = f.View().eye.y;
    push.cameraWorldPositionAndScale[2] = f.View().eye.z;
    push.cameraWorldPositionAndScale[3] = kWorldUnitsPerKilometer;
    push.strengthMaxDistanceExponentPad[0] = m_settings.aerialPerspectiveStrength;
    push.strengthMaxDistanceExponentPad[1] = m_settings.aerialPerspectiveMaxDistanceKm;
    push.strengthMaxDistanceExponentPad[2] = m_settings.aerialPerspectiveDepthExponent;

    const bool dispatched = f.Compute(m_compositeShader, "AtmosphereAerialPerspectiveCompositePass")
                                .Push(push)
                                .Bind("sourceColor", f.SceneColor())
                                .Bind("sourceDepth", fx::Depth(f.SceneDepth()))
                                .Bind("aerialPerspectiveVolume", luts->aerialVolume)
                                .Bind("destinationImage", output)
                                .Dispatch(f.Extent());
    if (!dispatched) {
        return;
    }
    f.ExposeAsViewOutput(m_composited, output);
    f.Blit("AtmosphereComposite.CopyToPrivateTarget", output, f.PrivateTarget(), fx::BlitFilter::Linear);
}

GTE_REGISTER_RENDER_FEATURE("Atmosphere", AtmosphereFeature);

} // namespace gte
