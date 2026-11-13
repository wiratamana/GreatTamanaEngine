#include "VolumetricCloudFeature.h"

#include "VolumetricCloudMath.h"
#include "../../Core/PixelFormat.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

#include <algorithm>
#include <optional>

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 49;
constexpr std::int32_t kShadowMapPriority = 12; // After the sun light record, before the scene uniforms read the map.
constexpr std::uint32_t kShadowSlotIndex = 4;
constexpr float kShadowFadeSunLow = 0.04f;     // Sun height (sine of the elevation) where the shadows start to fade in.
constexpr float kShadowFadeSunHigh = 0.10f;
constexpr float kHazeFadeSunLow = 0.30f;  // Sun height (sine of the elevation) where the beam haze starts to thin out.
constexpr float kHazeFadeSunHigh = 0.75f; // And where it is gone.
constexpr float kSunRayOutsideView = 0.2f;   // How far the sun may lie outside the picture (view height units) before the rays are gone.
constexpr float kSunRayFadeSunLow = 0.55f;  // Sun height where the sun rays start to fade out.
constexpr float kSunRayFadeSunHigh = 0.95f; // And where they are gone.
constexpr float kShaftFadeSunLow = 0.30f;  // Sun height where the air shadows (light shafts) start to fade out.
constexpr float kShaftFadeSunHigh = 0.62f; // And where they are gone.
constexpr double kShadowMaxSkipFrames = 2.0;   // A longer gap makes the old map useless as history.
constexpr std::int32_t kCompositePriority = -900; // After the aerial composite, so sky pixels are final.
constexpr float kMetresPerKilometer = 1000.0f;    // World units are metres, like the sky model.

} // namespace

VolumetricCloudFeature::VolumetricCloudFeature(fx::FeatureHost&) { }

void VolumetricCloudFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a toggle from freezing the frame clock the snapshots age against.
    s.PreOpaque("VolumetricCloud.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); }, fx::Gate::Always());

    // The secondary view skips the clouds by default.
    s.DefaultSecondaryViewEnabled(false);

    // The map is a scene service: the lit materials sample it where they apply the sun's cascade shadows.
    m_shadowSlot = s.DeclareSceneSlot("VolumetricCloud.ShadowMap", fx::SceneSlotKind::Image2D, kShadowSlotIndex);
    s.PreOpaque("VolumetricCloud.ShadowMap", kShadowMapPriority, [this](fx::PreOpaqueFrame& f) { ShadowMapPass(f); },
        fx::Gate{ "VolumetricCloud.Shadow.Dispatch" });

    s.PostComposite("VolumetricCloud", kCompositePriority, fx::Blend::Replace,
        [this](fx::CompositeFrame& f) { CompositePass(f); },
        fx::Gate{ "VolumetricCloud.March.Dispatch", "VolumetricCloud.Resolve.Dispatch", "VolumetricCloud.Debug.Dispatch",
            "VolumetricCloud.CopyToPrivateTarget" });
}

void VolumetricCloudFeature::ApplySettings(const VolumetricCloudSettings& candidate)
{
    m_settings = SanitizeVolumetricCloudSettings(candidate);
}

VolumetricCloudViewSnapshot VolumetricCloudFeature::Snapshot(rg::ViewScope scope) const
{
    VolumetricCloudViewSnapshot snapshot = m_views.For(scope).snapshot;
    if (fx::SnapshotIsStale(snapshot.frameIndex, m_newestFrame)) {
        snapshot.status = VolumetricCloudFrameStatus::NotRendered;
    }
    return snapshot;
}

fx::Texture VolumetricCloudFeature::AcquireDebugSource(fx::CompositeFrame& f)
{
    switch (m_settings.debugView) {
    case VolumetricCloudDebugView::ShapeSlice: return m_shape.Acquire(f);
    case VolumetricCloudDebugView::DetailSlice: return m_detail.Acquire(f);
    case VolumetricCloudDebugView::WeatherMap: return m_weather.Acquire(f);
    case VolumetricCloudDebugView::BlueNoise: return m_blueNoise.Acquire(f);
    case VolumetricCloudDebugView::Off: break;
    }
    return fx::Texture{};
}

bool VolumetricCloudFeature::RunSliceView(
    fx::CompositeFrame& f, fx::Texture output, VolumetricCloudViewSnapshot& snapshot)
{
    if (!f.PassEnabled("VolumetricCloud.Debug.Dispatch")) {
        snapshot.status = VolumetricCloudFrameStatus::PassDisabled;
        return false;
    }
    const bool volumeView = m_settings.debugView == VolumetricCloudDebugView::ShapeSlice
        || m_settings.debugView == VolumetricCloudDebugView::DetailSlice;
    if (!f.ShaderReady(volumeView ? m_slice3dShader : m_slice2dShader)) {
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
        return false;
    }
    const fx::Texture source = AcquireDebugSource(f);
    if (!source.IsValid()) {
        snapshot.status = VolumetricCloudFrameStatus::AssetMissing;
        return false;
    }
    const VolumetricCloudSlicePush push = MakeVolumetricCloudSlicePush(m_settings);
    const bool dispatched = volumeView ? f.Compute(m_slice3dShader, "VolumetricCloud.Debug.Dispatch")
                                             .Push(push)
                                             .Bind("noiseVolume", source)
                                             .Bind("destinationImage", output)
                                             .Dispatch(f.Extent())
                                       : f.Compute(m_slice2dShader, "VolumetricCloud.Debug.Dispatch")
                                             .Push(push)
                                             .Bind("noiseTexture", source)
                                             .Bind("destinationImage", output)
                                             .Dispatch(f.Extent());
    if (!dispatched) {
        f.ReportOnce("VolumetricCloud.Debug.Dispatch was refused");
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
    }
    return dispatched;
}

bool VolumetricCloudFeature::RunMarch(fx::CompositeFrame& f, VolumetricCloudViewState& view, fx::Texture output,
    VolumetricCloudViewSnapshot& snapshot)
{
    if (!f.PassEnabled("VolumetricCloud.March.Dispatch") || !f.PassEnabled("VolumetricCloud.Resolve.Dispatch")) {
        snapshot.status = VolumetricCloudFrameStatus::PassDisabled;
        return false;
    }
    // Ask both before testing either, so a failed build of one does not hide the other.
    const bool marchReady = f.ShaderReady(m_marchShader);
    const bool resolveReady = f.ShaderReady(m_resolveShader);
    if (!marchReady || !resolveReady) {
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
        return false;
    }
    const fx::Texture shape = m_shape.Acquire(f);
    const fx::Texture detail = m_detail.Acquire(f);
    const fx::Texture weather = m_weather.Acquire(f);
    const fx::Texture blueNoise = m_blueNoise.Acquire(f);
    const fx::Texture curl = m_curl.Acquire(f);
    if (!shape.IsValid() || !detail.IsValid() || !weather.IsValid() || !blueNoise.IsValid() || !curl.IsValid()) {
        snapshot.status = VolumetricCloudFrameStatus::AssetMissing;
        return false;
    }

    // Each target is acquired once per frame; a refused Acquire returns an invalid texture.
    const Extent2D full = f.Extent();
    const std::uint32_t divisor = CloudResolutionDivisor(m_settings, full.width);
    const Extent2D low = fx::ScaledExtent(full, divisor);
    const std::uint32_t slot = static_cast<std::uint32_t>(f.FrameIndex() & 1u);
    const fx::Texture lowColor = view.lowColor.Acquire(f, low, PixelFormat::RGBA16_Float);
    const fx::Texture lowAux = view.lowAux.Acquire(f, low, PixelFormat::RGBA16_Float);
    const fx::Texture currentColor = view.historyColor[slot].Acquire(f, full, PixelFormat::RGBA16_Float);
    const fx::Texture currentAux = view.historyAux[slot].Acquire(f, full, PixelFormat::RGBA16_Float);
    const fx::Texture previousColor = view.historyColor[slot ^ 1u].Acquire(f, full, PixelFormat::RGBA16_Float);
    const fx::Texture previousAux = view.historyAux[slot ^ 1u].Acquire(f, full, PixelFormat::RGBA16_Float);
    if (!lowColor.IsValid() || !lowAux.IsValid() || !currentColor.IsValid() || !currentAux.IsValid()
        || !previousColor.IsValid() || !previousAux.IsValid()) {
        snapshot.status = VolumetricCloudFrameStatus::TargetFailed;
        return false;
    }

    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
    const std::optional<SunLightModulation> modulation = m_sunLight.Fetch(f);
    const SunLightModulation light = modulation ? SanitizeSunLightModulation(*modulation) : SunLightModulation{};

    AdvanceCloudWind(view.windOffset, view.windStep, m_settings.windSpeed, m_settings.windDirection, f.DeltaSeconds(),
        CloudWindPeriodM(m_settings));
    VolumetricCloudFrameFacts facts;
    // Rays and reprojection are formed relative to the eye, in double precision.
    const fx::View& cameraView = f.View();
    if (!cameraView.relative.valid) {
        snapshot.status = VolumetricCloudFrameStatus::NoValidCamera;
        return false;
    }
    facts.invViewProjection = cameraView.relative.inverse;
    facts.eye = cameraView.eye;
    facts.sunDirection = sun.directionTowardSun;
    facts.sunIlluminance = sun.sunIlluminance;
    facts.sunTint = light.sunTint;
    facts.ambientScale = light.ambientScale;
    facts.windOffset[0] = static_cast<float>(view.windOffset[0]);
    facts.windOffset[1] = static_cast<float>(view.windOffset[1]);
    facts.frameIndex = f.FrameIndex();
    facts.viewWidth = full.width;
    facts.viewHeight = full.height;

    // The atmosphere's tables light the clouds when asked for and published; the standalone look otherwise.
    // The march shader has the table bindings either way, so stand-ins fill them in the standalone case.
    const std::optional<AtmosphereSharedLuts> sharedLuts = m_sharedLuts.Fetch(f);
    const std::optional<AtmosphereViewLuts> viewLuts = m_viewLuts.Fetch(f);
    const bool tablesReady = sharedLuts && viewLuts && sharedLuts->transmittance.IsValid()
        && sharedLuts->multiScattering.IsValid() && viewLuts->skyView.IsValid();
    const bool wanted = m_settings.atmosphereMode >= 0.5f;
    facts.atmosphereLit = wanted && tablesReady;
    snapshot.skyLight = facts.atmosphereLit ? VolumetricCloudSkyLight::Atmosphere
        : (wanted ? VolumetricCloudSkyLight::Unavailable : VolumetricCloudSkyLight::Standalone);
    AtmosphereParametersGpu air;
    fx::Texture transmittanceLut = weather;
    fx::Texture multiScatteringLut = weather;
    fx::Texture skyViewLut = weather;
    if (facts.atmosphereLit) {
        air = sharedLuts->parameters;
        transmittanceLut = sharedLuts->transmittance;
        multiScatteringLut = sharedLuts->multiScattering;
        skyViewLut = viewLuts->skyView;
        facts.skyIntensity = viewLuts->skyIntensity;
        facts.atmospherePlanetRadiusKm = air.planetRadiusKm;
        // Planet-centred frame like the sky: only the height above the ground matters.
        const float heightKm = std::max(f.View().eye.y, 0.0f) / kMetresPerKilometer;
        facts.atmosphereCameraKm = Vec3(0.0f, air.planetRadiusKm + heightKm, 0.0f);
    }

    // The air in front of a cloud sees the map of this frame, when the map pass made one.
    const bool shafts = view.shadowPublishedFrame == f.FrameIndex() && CloudShadowMapUsable(view.shadowPublished)
        && view.shadowPublished.shaftStrength > 0.0f && m_settings.shaftStrength > 0.0f;
    VolumetricCloudParamsGpu marchParams = MakeVolumetricCloudParams(m_settings, facts);
    if (shafts) {
        SetCloudShaftLookup(marchParams, view.shadowPublished);
    }
    const bool marched = f.Compute(m_marchShader, "VolumetricCloud.March.Dispatch")
                             .Params(view.params, marchParams)
                             .Params("atmosphere", view.atmosphereParams, air)
                             .Bind("transmittanceLut", transmittanceLut)
                             .Bind("multiScatteringLut", multiScatteringLut)
                             .Bind("skyViewLut", skyViewLut)
                             .Bind("sourceDepth", fx::Depth(f.SceneDepth()))
                             .Bind("shapeVolume", shape)
                             .Bind("detailVolume", detail)
                             .Bind("weatherMap", weather)
                             .Bind("blueNoise", blueNoise)
                             .Bind("curlMap", curl)
                             .Bind("shadowHistory", shafts ? view.shadowPublished.map : weather)
                             .Bind("lowColorOut", lowColor)
                             .Bind("lowAuxOut", lowAux)
                             .Dispatch(low);
    if (!marched) {
        f.ReportOnce("VolumetricCloud.March.Dispatch was refused");
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
        return false;
    }

    // The history reprojects through the previous unjittered view-projection, which this feature keeps itself.
    // The basis comes from the eye-relative camera: an absolute float matrix is too coarse far from the origin.
    const fx::CameraBasis basis = fx::DeriveViewCameraBasis(cameraView);
    const bool accumulate = m_settings.temporal >= 0.5f;
    VolumetricCloudResolveFacts resolveFacts;
    resolveFacts.prevViewProjection = fx::PreviousViewProjectionForShader(basis, view.previous);
    resolveFacts.windDelta[0] = static_cast<float>(view.windStep[0]);
    resolveFacts.windDelta[1] = static_cast<float>(view.windStep[1]);
    resolveFacts.historyValid = accumulate && basis.valid && fx::HistoryUsable(view.previous, basis, f.FrameIndex(), full, divisor);
    resolveFacts.lowWidth = low.width;
    resolveFacts.lowHeight = low.height;
    {
        // The sun in the view: a point far along the sun direction, in the eye-relative double camera.
        const Vec3 toSun = sun.directionTowardSun;
        const Vec4 clip = cameraView.relative.viewProjection * Vec4(toSun.x * 1000.0f, toSun.y * 1000.0f, toSun.z * 1000.0f, 1.0f);
        const float sunUpNow = toSun.y;
        if (clip.w > 1e-3f && sunUpNow > 0.0f) {
            resolveFacts.sunUv[0] = clip.x / clip.w * 0.5f + 0.5f;
            resolveFacts.sunUv[1] = clip.y / clip.w * 0.5f + 0.5f;
            const float lowSun = 1.0f - std::clamp((sunUpNow - kSunRayFadeSunLow) / (kSunRayFadeSunHigh - kSunRayFadeSunLow), 0.0f, 1.0f);
            const float onSet = std::clamp(sunUpNow / 0.05f, 0.0f, 1.0f);
            // A sun well outside the picture gives no rays: no glare from a sun nobody sees.
            const float aspect = static_cast<float>(full.width) / static_cast<float>(std::max(full.height, 1u));
            const float outX = std::max({ 0.0f, -resolveFacts.sunUv[0], resolveFacts.sunUv[0] - 1.0f }) * aspect;
            const float outY = std::max({ 0.0f, -resolveFacts.sunUv[1], resolveFacts.sunUv[1] - 1.0f });
            const float outside = std::clamp(std::sqrt(outX * outX + outY * outY) / kSunRayOutsideView, 0.0f, 1.0f);
            const float inView = 1.0f - outside * outside * (3.0f - 2.0f * outside);
            resolveFacts.sunRayFade = lowSun * lowSun * (3.0f - 2.0f * lowSun) * onSet * inView;
        }
        resolveFacts.sunColor[0] = light.sunTint.x;
        resolveFacts.sunColor[1] = light.sunTint.y;
        resolveFacts.sunColor[2] = light.sunTint.z;
    }
    snapshot.historyUsed = resolveFacts.historyValid;
    snapshot.divisor = divisor;

    const bool resolved = f.Compute(m_resolveShader, "VolumetricCloud.Resolve.Dispatch")
                              .Params(view.resolveParams, MakeVolumetricCloudResolveParams(m_settings, facts, resolveFacts))
                              .Bind("sourceColor", f.SceneColor())
                              .Bind("sourceDepth", fx::Depth(f.SceneDepth()))
                              .Bind("lowColor", lowColor)
                              .Bind("lowAux", lowAux)
                              .Bind("historyColor", previousColor)
                              .Bind("historyAux", previousAux)
                              .Bind("historyColorOut", currentColor)
                              .Bind("historyAuxOut", currentAux)
                              .Bind("destinationImage", output)
                              .Dispatch(full);
    if (!resolved) {
        f.ReportOnce("VolumetricCloud.Resolve.Dispatch was refused");
        snapshot.status = VolumetricCloudFrameStatus::ShadersNotReady;
        return false;
    }
    // Only a frame whose resolve pass ran becomes history.
    view.previous = basis.valid ? fx::MakeHistory(basis, f.View().viewProjection, f.FrameIndex(), full, divisor, view.previous)
                                : fx::TemporalHistory{};
    return true;
}

void VolumetricCloudFeature::ShadowMapPass(fx::PreOpaqueFrame& f)
{
    VolumetricCloudViewState& view = m_views.For(f.Scope());
    const VolumetricCloudSettings& cfg = m_settings;
    // Whatever stops the map also drops its history, so a later map never blends with a stale one.
    const auto stop = [&] { view.shadowGridValid = false; };

    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
    const float sunUp = sun.directionTowardSun.y;
    if (!cfg.enabled || !(cfg.shadowStrength > 0.0f || cfg.shaftStrength > 0.0f) || cfg.debugView != VolumetricCloudDebugView::Off
        || !m_shadowSlot.IsValid() || !f.View().relative.valid || !(sunUp > kShadowFadeSunLow)
        || !f.PassEnabled("VolumetricCloud.Shadow.Dispatch")) {
        stop();
        return;
    }
    if (!f.ShaderReady(m_marchShader)) {
        stop();
        return;
    }
    const fx::Texture shape = m_shape.Acquire(f);
    const fx::Texture detail = m_detail.Acquire(f);
    const fx::Texture weather = m_weather.Acquire(f);
    const fx::Texture blueNoise = m_blueNoise.Acquire(f);
    const fx::Texture curl = m_curl.Acquire(f);
    if (!shape.IsValid() || !detail.IsValid() || !weather.IsValid() || !blueNoise.IsValid() || !curl.IsValid()) {
        stop();
        return;
    }

    const Extent2D mapExtent{ kCloudShadowMapSize, kCloudShadowMapSize };
    const std::uint32_t slot = static_cast<std::uint32_t>(f.FrameIndex() & 1u);
    const fx::Texture current = view.shadowMap[slot].Acquire(f, mapExtent, PixelFormat::RGBA16_Float);
    const fx::Texture previous = view.shadowMap[slot ^ 1u].Acquire(f, mapExtent, PixelFormat::RGBA16_Float);
    const fx::Texture aux = view.shadowAux.Acquire(f, mapExtent, PixelFormat::RGBA16_Float);
    if (!current.IsValid() || !previous.IsValid() || !aux.IsValid()) {
        stop();
        return;
    }

    // The clouds drift with the wind; the shadow map follows the same field.
    VolumetricCloudFrameFacts facts;
    facts.eye = f.View().eye;
    facts.sunDirection = sun.directionTowardSun;
    facts.sunIlluminance = sun.sunIlluminance;
    facts.windOffset[0] = static_cast<float>(view.windOffset[0]);
    facts.windOffset[1] = static_cast<float>(view.windOffset[1]);
    facts.frameIndex = f.FrameIndex();
    const std::optional<AtmosphereSharedLuts> sharedLuts = m_sharedLuts.Fetch(f);
    const std::optional<AtmosphereViewLuts> viewLuts = m_viewLuts.Fetch(f);
    const bool tablesReady = sharedLuts && viewLuts && sharedLuts->transmittance.IsValid()
        && sharedLuts->multiScattering.IsValid() && viewLuts->skyView.IsValid();
    AtmosphereParametersGpu air;
    fx::Texture transmittanceLut = weather;
    fx::Texture multiScatteringLut = weather;
    fx::Texture skyViewLut = weather;
    if (m_settings.atmosphereMode >= 0.5f && tablesReady) {
        air = sharedLuts->parameters;
        transmittanceLut = sharedLuts->transmittance;
        multiScatteringLut = sharedLuts->multiScattering;
        skyViewLut = viewLuts->skyView;
        facts.atmosphereLit = true;
        facts.atmospherePlanetRadiusKm = air.planetRadiusKm;
    }

    VolumetricCloudShadowFacts shadow;
    shadow.grid = MakeCloudShadowGrid(cfg, facts.eye, sun.directionTowardSun, kCloudShadowMapSize);
    const bool sameLattice = view.shadowGridValid && view.shadowGrid.texelM == shadow.grid.texelM
        && static_cast<double>(f.FrameIndex() - view.shadowFrame) <= kShadowMaxSkipFrames && f.FrameIndex() > view.shadowFrame;
    if (sameLattice) {
        shadow.shiftX = view.windStep[0] + (shadow.grid.centerX - view.shadowGrid.centerX);
        shadow.shiftZ = view.windStep[1] + (shadow.grid.centerZ - view.shadowGrid.centerZ);
        shadow.historyBlend = 0.3f;
    }

    const bool written = f.Compute(m_marchShader, "VolumetricCloud.Shadow.Dispatch")
                             .Params(view.shadowParams, MakeVolumetricCloudShadowParams(cfg, facts, shadow))
                             .Params("atmosphere", view.shadowAtmosphereParams, air)
                             .Bind("transmittanceLut", transmittanceLut)
                             .Bind("multiScatteringLut", multiScatteringLut)
                             .Bind("skyViewLut", skyViewLut)
                             .Bind("sourceDepth", weather)
                             .Bind("shapeVolume", shape)
                             .Bind("detailVolume", detail)
                             .Bind("weatherMap", weather)
                             .Bind("blueNoise", blueNoise)
                             .Bind("curlMap", curl)
                             .Bind("shadowHistory", previous)
                             .Bind("lowColorOut", current)
                             .Bind("lowAuxOut", aux)
                             .Dispatch(mapExtent);
    if (!written) {
        f.ReportOnce("VolumetricCloud.Shadow.Dispatch was refused");
        stop();
        return;
    }
    view.shadowGrid = shadow.grid;
    view.shadowFrame = f.FrameIndex();
    view.shadowGridValid = true;

    const float fade = std::clamp((sunUp - kShadowFadeSunLow) / (kShadowFadeSunHigh - kShadowFadeSunLow), 0.0f, 1.0f);
    CloudShadowMap map;
    map.map = current;
    map.sunDirection = sun.directionTowardSun;
    map.centerX = static_cast<float>(shadow.grid.centerX);
    map.centerZ = static_cast<float>(shadow.grid.centerZ);
    map.extentM = shadow.grid.texelM * static_cast<float>(kCloudShadowMapSize);
    map.layerBaseM = cfg.layerBaseM;
    map.layerTopM = cfg.layerTopM;
    const float fadeSmooth = fade * fade * (3.0f - 2.0f * fade);
    map.strength = cfg.shadowStrength * fadeSmooth;
    const float shaftFade = 1.0f - std::clamp((sunUp - kShaftFadeSunLow) / (kShaftFadeSunHigh - kShaftFadeSunLow), 0.0f, 1.0f);
    map.shaftStrength = cfg.shaftStrength * fadeSmooth * shaftFade * shaftFade * (3.0f - 2.0f * shaftFade);
    map.shaftSharpness = cfg.shaftSharpness;
    // The beam haze is thickest for a low sun and thins out above about 45 degrees.
    const float hazeFade = 1.0f - std::clamp((sunUp - kHazeFadeSunLow) / (kHazeFadeSunHigh - kHazeFadeSunLow), 0.0f, 1.0f);
    map.shaftHazePerM = cfg.shaftHaze * 0.001f * hazeFade * hazeFade * (3.0f - 2.0f * hazeFade) * fadeSmooth;
    map.shaftHeightM = cfg.shaftHeightM;
    map.skyFill = cfg.shadowFill;
    f.PublishToScene(m_shadowSlot, current);
    m_shadowData.Publish(f, map);
    view.shadowPublished = map;
    view.shadowPublishedFrame = f.FrameIndex();
}

void VolumetricCloudFeature::CompositePass(fx::CompositeFrame& f)
{
    // Every return below that is not Ok declares nothing, so the compositor skips this entry.
    VolumetricCloudViewState& view = m_views.For(f.Scope());
    VolumetricCloudViewSnapshot& snapshot = view.snapshot;
    snapshot.frameIndex = f.FrameIndex();

    // A frame that does not write the history must not leave the last one usable.
    const auto bail = [&](VolumetricCloudFrameStatus status) {
        view.previous = fx::TemporalHistory{};
        snapshot.status = status;
    };

    if (!m_settings.enabled) {
        bail(VolumetricCloudFrameStatus::Disabled);
        return;
    }
    if (!f.View().relative.valid) {
        bail(VolumetricCloudFrameStatus::NoValidCamera);
        return;
    }
    if (!f.PassEnabled("VolumetricCloud.CopyToPrivateTarget")) {
        bail(VolumetricCloudFrameStatus::PassDisabled);
        return;
    }
    const fx::Texture output = m_output.Acquire(f, f.Extent(), PixelFormat::RGBA16_Float);
    if (!output.IsValid()) {
        bail(VolumetricCloudFrameStatus::TargetFailed);
        return;
    }

    const bool sliceView = m_settings.debugView != VolumetricCloudDebugView::Off;
    if (!(sliceView ? RunSliceView(f, output, snapshot) : RunMarch(f, view, output, snapshot))) {
        view.previous = fx::TemporalHistory{};
        return;
    }
    if (sliceView) {
        view.previous = fx::TemporalHistory{};
    }
    snapshot.status = VolumetricCloudFrameStatus::Ok;
    f.ExposeAsViewOutput(m_output, output);
    f.Blit("VolumetricCloud.CopyToPrivateTarget", output, f.PrivateTarget(), fx::BlitFilter::Linear);
}

GTE_REGISTER_RENDER_FEATURE("VolumetricCloud", VolumetricCloudFeature);

} // namespace gte
