#include "AtmosphereFeature.h"

#include "AtmosphereMath.h"
#include "AtmosphereParameters.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

#include <algorithm>
#include <cstdint>

namespace gte {

namespace {

constexpr std::int32_t kSharedLutPriority = -10;
constexpr std::int32_t kViewLutPriority = 0;
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
};
static_assert(sizeof(CompositePush) == 96, "layout differs from AtmosphereAerialPerspectiveComposite.comp");

// Push constants of AtmosphereSkyBackground.frag.
struct SkyBackgroundPush {
    Mat4 invViewProjection;
    float eyeHeightKm;
    float planetRadiusKm;
    float skyExposure;
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
    uniforms.cameraPositionAtmosphere =
        Vec3(0.0f, MakeDefaultEarthAtmosphereParameters().planetRadiusKm + heightAboveGroundKm, 0.0f);

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
    }

    if (f.PassEnabled("AtmosphereAerialPerspectiveVolumePass")) {
        const fx::Texture volume = view.aerialVolume.Acquire(f);
        if (volume.IsValid()) {
            const bool done = f.Compute(m_aerialVolumeShader, "AtmosphereAerialPerspectiveVolumePass")
                                  .Params(m_sharedParams, shared->parameters)
                                  .Params("frame", view.frameParams, luts.frameUniforms)
                                  .Bind("transmittanceLut", shared->transmittance)
                                  .Bind("multiScatteringLut", shared->multiScattering)
                                  .Bind("destinationVolume", volume)
                                  .Dispatch(kAerialDispatchSize);
            if (done) {
                luts.aerialVolume = volume;
            }
        }
    }
    m_viewLuts.Publish(f, luts);
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
    push.skyExposure = m_settings.skyExposure;
    f.ViewBackground(m_skyBackgroundShader, "DrawSkyBackground").Bind("skyViewLut", luts->skyView).Push(push).Draw();
}

void AtmosphereFeature::Composite(fx::CompositeFrame& f)
{
    if (!f.View().invViewProjectionValid || !f.PassEnabled("AtmosphereAerialPerspectiveCompositePass")) {
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

    const fx::View& view = f.View();
    CompositePush push{};
    push.invViewProjection = view.invViewProjection;
    push.cameraWorldPositionAndScale = Vec4(view.eye, kWorldUnitsPerKilometer);
    push.strengthMaxDistanceExponent = Vec4(m_settings.aerialPerspectiveStrength, m_settings.aerialPerspectiveMaxDistanceKm,
        m_settings.aerialPerspectiveDepthExponent, 0.0f);

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
