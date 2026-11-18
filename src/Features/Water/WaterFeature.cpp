#include "WaterFeature.h"

#include "WaterMath.h"
#include "../../Core/PixelFormat.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

#include <optional>

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 36;
// Before the passes that read depth (Gtao 50, Ssgi 55, VolumetricLight 60) and the sky background (100).
constexpr std::int32_t kSurfacePriority = 25;

constexpr std::uint64_t kBytesPerPixel = 8 + 4; // RGBA16 float + R32 float.

} // namespace

WaterFeature::WaterFeature(fx::FeatureHost&) { }

void WaterFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a toggle from freezing the wave clock.
    s.PreOpaque("Water.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) {
            m_newestFrame = f.FrameIndex();
            AdvanceClock(f);
        },
        fx::Gate::Always());

    // The editor's second view skips the water by default.
    s.DefaultSecondaryViewEnabled(false);

    s.PostOpaque("Water.Surface", kSurfacePriority, [this](fx::PostOpaqueFrame& f) { SurfacePass(f); },
        fx::Gate{ "Water.Shade.Dispatch", "Water.Composite.Draw" });
}

void WaterFeature::ApplySettings(const WaterSettings& candidate)
{
    m_settings = SanitizeWaterSettings(candidate);
}

WaterViewSnapshot WaterFeature::Snapshot(rg::ViewScope scope) const
{
    WaterViewSnapshot snapshot = m_views.For(scope).snapshot;
    if (fx::SnapshotIsStale(snapshot.frameIndex, m_newestFrame)) {
        snapshot.status = WaterFrameStatus::NotRendered;
    }
    return snapshot;
}

bool WaterFeature::AddLocalBody(WaterBodyDesc body)
{
    if (m_localBodies.size() >= kWaterMaxBodies) {
        return false;
    }
    m_localBodies.push_back(std::move(body));
    return true;
}

bool WaterFeature::RemoveLocalBody(std::size_t index)
{
    if (index >= m_localBodies.size()) {
        return false;
    }
    m_localBodies.erase(m_localBodies.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void WaterFeature::ClearLocalBodies()
{
    m_localBodies.clear();
}

WaterProbe WaterFeature::Probe(Vec2 xz)
{
    return WaterProbePoint(m_localBodies, m_settings, m_time, xz);
}

void WaterFeature::AdvanceClock(const fx::Frame& f)
{
    // Both views call this in one frame; the clock moves once.
    if (f.FrameIndex() != m_clockFrame) {
        m_clockFrame = f.FrameIndex();
        m_time += static_cast<double>(f.DeltaSeconds());
    }
}

void WaterFeature::SurfacePass(fx::PostOpaqueFrame& f)
{
    WaterViewState& view = m_views.For(f.Scope());
    const fx::View& v = f.View();
    const WaterSettings& cfg = m_settings;

    WaterViewSnapshot snap;
    snap.frameIndex = f.FrameIndex();
    const auto bail = [&](WaterFrameStatus status) {
        snap.status = status;
        view.snapshot = snap;
    };

    if (!cfg.enabled) {
        bail(WaterFrameStatus::Disabled);
        return;
    }
    if (v.extent.width == 0 || v.extent.height == 0 || !v.relative.valid
        || ApproximatelyEqual(v.viewProjection, Mat4::Identity())) {
        bail(WaterFrameStatus::NoValidCamera);
        return;
    }
    const fx::CameraBasis basis = fx::DeriveViewCameraBasis(v);
    if (!basis.valid) {
        f.ReportOnce("Water: camera is not a square-pixel perspective camera, water is off");
        bail(WaterFrameStatus::NoValidCamera);
        return;
    }
    if (!f.PassEnabled("Water.Shade.Dispatch") || !f.PassEnabled("Water.Composite.Draw")) {
        bail(WaterFrameStatus::PassDisabled);
        return;
    }
    std::uint32_t bodyCount = 0;
    const WaterBodiesGpu bodies = PackWaterBodies(m_localBodies, cfg.seaLevel, bodyCount);
    snap.bodyCount = bodyCount;
    if (!WaterAnyBodyBelowEye(bodies, v.eye.y)) {
        bail(bodyCount == 0 ? WaterFrameStatus::NoBodies : WaterFrameStatus::CameraUnderwater);
        return;
    }
    if (!f.ShaderReady(m_shadeShader)) {
        bail(WaterFrameStatus::ShadersNotReady);
        return;
    }

    // Half-res shading: the shade pass runs on a quarter of the pixels; the composite looks each full pixel up by uv.
    constexpr std::uint64_t kAutoHalfPixels = 600000;
    const bool half = cfg.halfRes || (cfg.halfResAuto && std::uint64_t{ v.extent.width } * v.extent.height >= kAutoHalfPixels);
    Extent2D shadeExtent = v.extent;
    if (half) {
        shadeExtent.width = (v.extent.width + 1) / 2;
        shadeExtent.height = (v.extent.height + 1) / 2;
    }
    const fx::Texture color = view.color.Acquire(f, shadeExtent, PixelFormat::RGBA16_Float);
    const fx::Texture depth = view.depth.Acquire(f, shadeExtent, PixelFormat::R32_Float);
    if (!color.IsValid() || !depth.IsValid()) {
        bail(WaterFrameStatus::TargetFailed);
        return;
    }

    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
    const std::optional<SunLightModulation> modulation = m_sunLight.Fetch(f);
    const SunLightModulation air = modulation ? SanitizeSunLightModulation(*modulation) : SunLightModulation{};
    const std::optional<AtmosphereViewLuts> luts = m_viewLuts.Fetch(f);
    const std::optional<AtmosphereSharedLuts> shared = m_sharedLuts.Fetch(f);
    const bool skyValid = luts.has_value() && shared.has_value() && luts->skyView.IsValid();
    const std::optional<CloudShadowMap> cloud = m_cloudShadow.Fetch(f);
    const bool cloudValid = cloud.has_value() && CloudShadowMapUsable(*cloud);

    WaterFrameInputs in;
    in.invViewProjection = v.relative.inverse;
    in.viewProjection = v.relative.viewProjection;
    in.eye = v.eye;
    in.jitterNdc = v.jitterNdc;
    in.pixelScale = basis.pixelScale;
    in.timeSeconds = m_time;
    in.sunDirection = sun.directionTowardSun;
    in.sunRadiance = air.active ? sun.sunIlluminance * air.sunTint : sun.sunIlluminance;
    in.ambientScale = air.active ? air.ambientScale : 1.0f;
    in.skyValid = skyValid;
    if (skyValid) {
        in.skyIntensity = luts->skyIntensity;
        in.eyeHeightKm = luts->frameUniforms.cameraPositionAtmosphere.y;
        in.planetRadiusKm = shared->parameters.planetRadiusKm;
    }
    if (cloudValid) {
        in.cloudShadowValid = true;
        in.cloudShadowCenterX = cloud->centerX;
        in.cloudShadowCenterZ = cloud->centerZ;
        in.cloudShadowExtentM = cloud->extentM;
        in.cloudShadowStrength = cloud->strength;
        in.cloudShadowLayerBaseM = cloud->layerBaseM;
        in.cloudShadowLayerTopM = cloud->layerTopM;
        in.cloudShadowMapSize = kCloudShadowMapSize;
    }

    // Samplers need a valid image even when sky or cloud shadow is missing (the flags skip the reads).
    const bool shaded = f.Compute(m_shadeShader, "Water.Shade.Dispatch")
                            .Params(view.shadeParams, MakeWaterShadeParams(cfg, in))
                            .Params("bodies", view.bodyParams, bodies)
                            .Bind("sceneDepth", fx::Depth(f.SceneDepth()))
                            .Bind("sceneColor", f.SceneColor())
                            .Bind("skyViewLut", skyValid ? luts->skyView : f.SceneColor())
                            .Bind("cloudShadowMap", cloudValid ? cloud->map : f.SceneColor())
                            .Bind("waterColorOut", color)
                            .Bind("waterDepthOut", depth)
                            .Dispatch(shadeExtent);
    if (!shaded) {
        f.ReportOnce("Water.Shade.Dispatch was refused");
        bail(WaterFrameStatus::ShadersNotReady);
        return;
    }

    const bool drawn = f.Fullscreen(m_compositeShader, "Water.Composite.Draw")
                           .Bind("waterColor", color)
                           .Bind("waterDepth", depth)
                           .Target(f.SceneColor())
                           .DepthTest(f.SceneDepth(), half ? fx::DepthCompare::LessEqual : fx::DepthCompare::Always, /*write=*/true)
                           .Draw();
    if (!drawn) {
        f.ReportOnce("Water.Composite.Draw was refused");
        bail(WaterFrameStatus::ShadersNotReady);
        return;
    }

    snap.extent = v.extent;
    snap.waveCount = cfg.waveCount;
    snap.skyValid = skyValid;
    snap.textureBytes = std::uint64_t{ shadeExtent.width } * shadeExtent.height * kBytesPerPixel;
    snap.status = WaterFrameStatus::Ok;
    view.snapshot = snap;
}

GTE_REGISTER_RENDER_FEATURE("Water", WaterFeature);

} // namespace gte
