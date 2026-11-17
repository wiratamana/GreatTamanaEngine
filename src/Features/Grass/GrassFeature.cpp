#include "GrassFeature.h"

#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Math/Vec3.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 38;
// After Shadow.Cascades (10), VolumetricCloud.ShadowMap (12) and VolumetricCloud.FrameStamp (49, wind), before Opaque.
constexpr std::int32_t kPreparePriority = 50;
// After the terrain (Opaque stage), before the shadow mask (0), the water (25) and the sky (100).
constexpr std::int32_t kDrawPriority = -20;
constexpr std::uint64_t kTileBytes = std::uint64_t{ kGrassMaxTileTexels } * kGrassMaxTileTexels * kGrassTileStride * sizeof(float);
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

constexpr std::array<std::array<rg::StaticName, kGrassShadowLodCount>, kSunShadowReceiverMaxCascades> kShadowPassNames{ {
    { "Grass.Shadow.C0.L0", "Grass.Shadow.C0.L1" },
    { "Grass.Shadow.C1.L0", "Grass.Shadow.C1.L1" },
    { "Grass.Shadow.C2.L0", "Grass.Shadow.C2.L1" },
    { "Grass.Shadow.C3.L0", "Grass.Shadow.C3.L1" },
} };

// Sky and ground ambient of the scene lighting defaults (colour x intensity).
constexpr float kSkyAmbient[3] = { 1.26f * 0.35f, 1.31f * 0.35f, 1.40f * 0.35f };
constexpr float kGroundAmbient[3] = { 0.66f * 0.35f, 0.57f * 0.35f, 0.46f * 0.35f };

} // namespace

GrassFeature::GrassFeature(fx::FeatureHost&) { }

void GrassFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a toggle from freezing the wind and the synthetic tile.
    s.PreOpaque("Grass.FrameStamp", kFrameStampPriority, [this](fx::PreOpaqueFrame& f) { FrameStamp(f); },
        fx::Gate::Always());

    // The editor's second view skips the grass by default.
    s.DefaultSecondaryViewEnabled(false);

    fx::Gate prepareGate{ "Grass.Init", "Grass.Cull", "Grass.Stats.Copy" };
    for (const auto& cascadeNames : kShadowPassNames) {
        for (const rg::StaticName& passName : cascadeNames) {
            prepareGate.passNames.emplace_back(passName.value);
        }
    }
    s.PreOpaque("Grass.Prepare", kPreparePriority, [this](fx::PreOpaqueFrame& f) { PreparePass(f); },
        std::move(prepareGate));
    s.ReadsChannelInEmptyViews("Grass.Prepare", kSunShadowReceiverOwner, kSunShadowReceiverChannel);

    fx::Gate drawGate{ "Grass.Draw.L0", "Grass.Draw.L1", "Grass.Draw.L2" };
    s.PostOpaque("Grass.Draw", kDrawPriority, [this](fx::PostOpaqueFrame& f) { DrawPass(f); }, std::move(drawGate));
}

void GrassFeature::ApplySettings(const GrassSettings& candidate)
{
    m_user = SanitizeGrassSettings(candidate, /*enforceOrder=*/false);
    m_settings = SanitizeGrassSettings(m_user);
}

GrassViewSnapshot GrassFeature::Snapshot(rg::ViewScope scope) const
{
    GrassViewSnapshot snapshot = m_views.For(scope).snapshot;
    if (fx::SnapshotIsStale(snapshot.frameIndex, m_newestFrame)) {
        snapshot.status = GrassFrameStatus::NotRendered;
    }
    return snapshot;
}

void GrassFeature::SubmitHeightTile(std::shared_ptr<const GrassHeightTile> tile)
{
    m_syntheticOn = false;
    m_tile = std::move(tile);
    ++m_tileRevision;
}

void GrassFeature::EnableSyntheticTile(const GrassSyntheticSpec& spec)
{
    m_syntheticSpec = spec;
    m_syntheticOn = true;
    m_tile.reset(); // Rebuilt around the camera on the next frame.
}

void GrassFeature::DisableSyntheticTile()
{
    if (m_syntheticOn) {
        m_syntheticOn = false;
        m_tile.reset();
        ++m_tileRevision;
    }
}

void GrassFeature::UpdateSyntheticTile(const fx::View& view)
{
    if (!m_syntheticOn || !view.relative.valid) {
        return;
    }
    const float texel = m_settings.heightTileTexelSize;
    const std::uint32_t size = kGrassMaxTileTexels;
    const double span = static_cast<double>(size) * texel;
    const double slack = std::max(span * 0.5 - (m_settings.lod2Distance + 2.0 * kGrassChunkSizeM), 8.0);
    if (m_tile != nullptr) {
        const double cx = m_tile->originX + span * 0.5;
        const double cz = m_tile->originZ + span * 0.5;
        const double drift = std::max(std::abs(static_cast<double>(view.eye.x) - cx), std::abs(static_cast<double>(view.eye.z) - cz));
        if (drift < slack * 0.6 && m_tile->texelSize == texel) {
            return;
        }
    }
    ++m_tileRevision;
    m_tile = std::make_shared<const GrassHeightTile>(
        MakeSyntheticGrassTile(m_syntheticSpec, view.eye.x, view.eye.z, size, texel, m_tileRevision));
}

void GrassFeature::FrameStamp(fx::PreOpaqueFrame& f)
{
    m_newestFrame = f.FrameIndex();
    if (f.FrameIndex() != m_clockFrame) {
        m_clockFrame = f.FrameIndex();
        const double dt = static_cast<double>(f.DeltaSeconds());
        m_time += dt;
        // Same convention as the cloud drift: the offset moves against the wind.
        const double radians = static_cast<double>(m_settings.windDirectionDegFallback) * kDegreesToRadians;
        const double distance = static_cast<double>(m_settings.windSpeedFallback) * dt;
        m_ownOffset[0] -= std::cos(radians) * distance;
        m_ownOffset[1] -= std::sin(radians) * distance;
    }
    if (f.Scope() == rg::ViewScope::Primary) {
        m_lastEye = f.View().eye;
        UpdateSyntheticTile(f.View());
    }
}

void GrassFeature::FillEnvironment(fx::Frame& f, GrassFrameInputs& in, GrassViewSnapshot& snap)
{
    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(f.Scene());
    const std::optional<SunLightModulation> modulation = m_sunLight.Fetch(f);
    const SunLightModulation air = modulation ? SanitizeSunLightModulation(*modulation) : SunLightModulation{};
    const Vec3 radiance = air.active ? sun.sunIlluminance * air.sunTint : sun.sunIlluminance;
    const float ambientScale = air.active ? air.ambientScale : 1.0f;
    in.sunDirection[0] = sun.directionTowardSun.x;
    in.sunDirection[1] = sun.directionTowardSun.y;
    in.sunDirection[2] = sun.directionTowardSun.z;
    in.sunRadiance[0] = radiance.x;
    in.sunRadiance[1] = radiance.y;
    in.sunRadiance[2] = radiance.z;
    for (int i = 0; i < 3; ++i) {
        in.skyAmbient[i] = kSkyAmbient[i] * ambientScale;
        in.groundAmbient[i] = kGroundAmbient[i] * ambientScale;
    }

    in.timeSeconds = static_cast<float>(std::fmod(m_time, 100000.0));
    const std::optional<WindState> shared = m_settings.followCloudWind ? m_wind.Fetch(f) : std::nullopt;
    if (shared) {
        const WindState wind = SanitizeWindState(*shared);
        in.windDirX = wind.directionX;
        in.windDirZ = wind.directionZ;
        in.windSpeed = wind.speed;
        in.windOffsetX = wind.offsetX;
        in.windOffsetZ = wind.offsetZ;
        snap.windShared = true;
    } else {
        const double radians = static_cast<double>(m_settings.windDirectionDegFallback) * kDegreesToRadians;
        in.windDirX = static_cast<float>(std::cos(radians));
        in.windDirZ = static_cast<float>(std::sin(radians));
        in.windSpeed = m_settings.windSpeedFallback;
        in.windOffsetX = m_ownOffset[0];
        in.windOffsetZ = m_ownOffset[1];
    }
    snap.windDirX = in.windDirX;
    snap.windDirZ = in.windDirZ;
    snap.windSpeed = in.windSpeed;
    snap.windOffsetX = static_cast<float>(in.windOffsetX);
    snap.windOffsetZ = static_cast<float>(in.windOffsetZ);
}

void GrassFeature::PreparePass(fx::PreOpaqueFrame& f)
{
    GrassViewState& view = m_views.For(f.Scope());
    view.prepareRanSerial = f.Serial();
    view.preparedSerial = 0;
    view.preparedInstances = fx::GpuBufferRef{};
    view.preparedCommands = fx::GpuBufferRef{};

    const fx::View& v = f.View();
    const GrassSettings& cfg = m_settings;

    GrassViewSnapshot snap;
    snap.frameIndex = f.FrameIndex();
    const auto bail = [&](GrassFrameStatus status) {
        snap.status = status;
        view.snapshot = snap;
    };

    if (!cfg.enabled) {
        bail(GrassFrameStatus::Disabled);
        return;
    }
    if (v.extent.width == 0 || v.extent.height == 0 || !v.relative.valid) {
        bail(GrassFrameStatus::NoValidCamera);
        return;
    }
    if (!f.PassEnabled("Grass.Init") || !f.PassEnabled("Grass.Cull")) {
        bail(GrassFrameStatus::PassDisabled);
        return;
    }
    const std::shared_ptr<const GrassHeightTile> tile = m_tile;
    if (tile == nullptr || !tile->Valid() || tile->size > kGrassMaxTileTexels) {
        bail(GrassFrameStatus::NoTile);
        return;
    }
    // Ask both before testing either, so a failed build of one does not hide the other.
    const bool initReady = f.ShaderReady(m_initShader);
    const bool cullReady = f.ShaderReady(m_cullShader);
    if (!initReady || !cullReady) {
        bail(GrassFrameStatus::ShadersNotReady);
        return;
    }

    const std::uint64_t instanceBytes = GrassInstanceBytes(cfg);
    const fx::GpuBufferRef instances = m_instances.Acquire(f, instanceBytes);
    const fx::GpuBufferRef commands = m_commands.Acquire(f, kGrassCommandBytes);
    const fx::GpuBufferRef heights = m_heights.Acquire(f, kTileBytes);
    const fx::Texture standIn = view.standIn.Acquire(f, Extent2D{ 1, 1 }, PixelFormat::RGBA8_UNorm, /*storage=*/false);
    if (!instances.IsValid() || !commands.IsValid() || !heights.IsValid() || !standIn.IsValid()) {
        bail(GrassFrameStatus::TargetFailed);
        return;
    }

    // Each frame-slot copy of the tile is refreshed once per tile change (and after the copy was recreated).
    const std::size_t slot = f.OffscreenTicket().slot % view.slotRevision.size();
    const std::uint64_t generation = m_heights.Generation(f);
    if (view.slotRevision[slot] != m_tileRevision || view.slotGeneration[slot] != generation) {
        const std::uint64_t bytes = std::uint64_t{ tile->size } * tile->size * kGrassTileStride * sizeof(float);
        if (!m_heights.Upload(f, tile->data.data(), bytes)) {
            bail(GrassFrameStatus::TargetFailed);
            return;
        }
        view.slotRevision[slot] = m_tileRevision;
        view.slotGeneration[slot] = generation;
    }

    GrassFrameInputs in;
    in.viewProjection = v.relative.viewProjection;
    in.drawViewProjection = fx::ApplyClipJitter(v.relative.viewProjection, v.jitterNdc);
    in.eyeX = v.eye.x;
    in.eyeY = v.eye.y;
    in.eyeZ = v.eye.z;
    in.tile = tile.get();
    // PreOpaque runs before opaque draws, so scene depth does not exist here; Hi-Z stays off.
    in.hizUsable = false;
    FillEnvironment(f, in, snap);

    const std::optional<SunShadowReceiver> publishedReceiver = m_shadowReceiver.Fetch(f);
    const std::optional<CloudShadowMap> publishedCloud = m_cloudShadow.Fetch(f);
    const Vec3 liveSun{ in.sunDirection[0], in.sunDirection[1], in.sunDirection[2] };
    const GrassShadowParamsGpu shadowParams = MakeGrassShadowParams(
        publishedReceiver ? &*publishedReceiver : nullptr, publishedCloud ? &*publishedCloud : nullptr, liveSun, cfg);
    const GrassShadowCastPlan castPlan = MakeGrassShadowCastPlan(
        publishedReceiver ? &*publishedReceiver : nullptr, liveSun, in.eyeX, in.eyeY, in.eyeZ, cfg);

    bool anyShadowPassEnabled = false;
    for (std::uint32_t i = 0; i < castPlan.count; ++i) {
        const std::uint32_t c = castPlan.cascades[i].cascadeIndex;
        for (std::uint32_t lod = 0; lod < kGrassShadowLodCount; ++lod) {
            anyShadowPassEnabled = anyShadowPassEnabled || f.PassEnabled(kShadowPassNames[c][lod]);
        }
    }
    in.shadowCasting = anyShadowPassEnabled;
    snap.hizUsed = false;

    const GrassParamsGpu params = MakeGrassParams(cfg, in);
    const bool useCascades = shadowParams.counts[0] > 0;
    const bool useCloud = shadowParams.cloudGrid[3] > 0.0f;
    snap.shadowCascades = shadowParams.counts[0];
    snap.shadowCloud = useCloud;

    const bool inited = f.Compute(m_initShader, "Grass.Init")
                            .Params(view.initParams, params)
                            .BindBuffer("commands", commands)
                            .BindOutput("stats", m_stats)
                            .Dispatch(Extent2D{ 64, 1 });
    if (!inited) {
        f.ReportOnce("Grass.Init was refused");
        bail(GrassFrameStatus::ShadersNotReady);
        return;
    }

    // Unused samplers receive a 1x1 stand-in colour texture while zero counts/strength skip sampling.
    const std::uint32_t chunkCount = params.grid[2] > 0 ? static_cast<std::uint32_t>(params.grid[2] * params.grid[2]) : 0u;
    fx::ComputeChain cull = f.Compute(m_cullShader, "Grass.Cull");
    cull.Params(view.cullParams, params)
        .Params("shadow", view.cullShadowParams, shadowParams)
        .BindBuffer("heightTile", heights)
        .BindBuffer("instances", instances)
        .BindBuffer("commands", commands)
        .BindOutput("stats", m_stats)
        .Bind("hizAtlas", standIn)
        .Bind("cloudShadowMap", useCloud ? publishedCloud->map : standIn);
    static constexpr std::array<const char*, kSunShadowReceiverMaxCascades> kShadowSamplerNames{
        "shadowDepth0", "shadowDepth1", "shadowDepth2", "shadowDepth3"
    };
    for (std::uint32_t i = 0; i < kSunShadowReceiverMaxCascades; ++i) {
        if (useCascades) {
            cull.Bind(kShadowSamplerNames[i], fx::Depth(publishedReceiver->maps[std::min(i, shadowParams.counts[0] - 1u)]));
        } else {
            cull.Bind(kShadowSamplerNames[i], standIn);
        }
    }
    const bool culled = cull.Dispatch(
        Extent2D{ chunkCount * static_cast<std::uint32_t>(params.extra[3]) * kGrassCullGroupSize, 1 });
    if (!culled) {
        f.ReportOnce("Grass.Cull was refused");
        bail(GrassFrameStatus::ShadersNotReady);
        return;
    }
    f.CopyReadbackToHost("Grass.Stats.Copy", m_stats);

    if (const auto sample = m_stats.Latest(f)) {
        snap.stats = sample->value;
        snap.statsValid = true;
        snap.statsFrame = sample->frame;
    }

    const std::uint32_t drawnLod[kGrassShadowLodCount] = {
        snap.statsValid ? std::min(snap.stats.Get(GrassStatsWord::ShadowSlot0), cfg.instanceCapLod0) : 0u,
        snap.statsValid ? std::min(snap.stats.Get(GrassStatsWord::ShadowSlot1), cfg.instanceCapLod1) : 0u,
    };
    std::uint32_t castCascades = 0;
    std::uint64_t castBlades = 0;
    for (std::uint32_t i = 0; i < castPlan.count; ++i) {
        const GrassShadowCascadeDraw& item = castPlan.cascades[i];
        const fx::Texture targetDepth = publishedReceiver->maps[item.cascadeIndex];
        if (!f.Owns(targetDepth)) {
            continue;
        }
        bool cascadeDrew = false;
        for (std::uint32_t lod = 0; lod < kGrassShadowLodCount; ++lod) {
            const rg::StaticName passName = kShadowPassNames[item.cascadeIndex][lod];
            if (!f.PassEnabled(passName)) {
                continue;
            }
            GrassShadowPushGpu push{};
            std::memcpy(push.cascadeVpRel, item.viewProjectionRelative.Data(), sizeof(push.cascadeVpRel));
            push.lod = lod;
            push.instanceBase = params.instanceBase[lod];
            push.minWidth = item.minWidth;
            push.biasM = item.biasM;
            const std::uint64_t cmdOffset = std::uint64_t{ kGrassLodCount + lod } * kGrassCommandStride;
            const bool ok = f.Fullscreen(m_shadowShader, passName)
                                .Params(view.shadowDrawParams[item.cascadeIndex][lod], params)
                                .BindBuffer("instances", instances)
                                .Push(push)
                                .IndirectDraw(commands, cmdOffset)
                                .DepthTest(targetDepth, fx::DepthCompare::Less, /*write=*/true)
                                .Draw();
            if (!ok) {
                f.ReportOnce("A Grass.Shadow pass was refused");
                bail(GrassFrameStatus::ShadersNotReady);
                return;
            }
            cascadeDrew = true;
            castBlades += drawnLod[lod];
        }
        if (cascadeDrew) {
            ++castCascades;
        }
    }
    snap.shadowCastCascades = castCascades;
    snap.shadowCastBlades = castBlades;
    snap.extent = v.extent;
    snap.gridCells = cfg.gridCells;
    snap.tileRevision = tile->revision;
    snap.instanceBytes = instanceBytes;
    snap.status = GrassFrameStatus::NotRendered;

    view.preparedInstances = instances;
    view.preparedCommands = commands;
    view.preparedParams = params;
    view.preparedSerial = f.Serial();
    view.snapshot = snap;
}

void GrassFeature::DrawPass(fx::PostOpaqueFrame& f)
{
    GrassViewState& view = m_views.For(f.Scope());
    if (view.preparedSerial != f.Serial() || !f.Owns(view.preparedInstances) || !f.Owns(view.preparedCommands)) {
        if (view.prepareRanSerial != f.Serial()) {
            GrassViewSnapshot snap;
            snap.frameIndex = f.FrameIndex();
            snap.status = GrassFrameStatus::NotRendered;
            view.snapshot = snap;
        }
        return;
    }

    if (!f.PassEnabled("Grass.Draw.L0") || !f.PassEnabled("Grass.Draw.L1") || !f.PassEnabled("Grass.Draw.L2")) {
        view.snapshot.status = GrassFrameStatus::PassDisabled;
        return;
    }

    const fx::View& v = f.View();
    GrassParamsGpu params = view.preparedParams;
    const Mat4 jitteredVp = fx::ApplyClipJitter(v.relative.viewProjection, v.jitterNdc);
    std::memcpy(params.drawViewProjection, jitteredVp.Data(), sizeof(params.drawViewProjection));

    bool drawn = true;
    const std::uint32_t viewportHeightBits = std::bit_cast<std::uint32_t>(static_cast<float>(v.extent.height));
    const std::array<std::uint32_t, kGrassLodCount> bases{ params.instanceBase[0], params.instanceBase[1], params.instanceBase[2] };
    drawn &= f.Fullscreen(m_drawShader, "Grass.Draw.L0")
                 .Params(view.drawParams[0], params)
                 .BindBuffer("instances", view.preparedInstances)
                 .Push(GrassPushGpu{ 0, bases[0], viewportHeightBits, 0 })
                 .IndirectDraw(view.preparedCommands, 0)
                 .Target(f.SceneColor())
                 .DepthTest(f.SceneDepth(), fx::DepthCompare::LessEqual, /*write=*/true)
                 .Draw();
    drawn &= f.Fullscreen(m_drawShader, "Grass.Draw.L1")
                 .Params(view.drawParams[1], params)
                 .BindBuffer("instances", view.preparedInstances)
                 .Push(GrassPushGpu{ 1, bases[1], viewportHeightBits, 0 })
                 .IndirectDraw(view.preparedCommands, kGrassCommandStride)
                 .Target(f.SceneColor())
                 .DepthTest(f.SceneDepth(), fx::DepthCompare::LessEqual, /*write=*/true)
                 .Draw();
    drawn &= f.Fullscreen(m_drawShader, "Grass.Draw.L2")
                 .Params(view.drawParams[2], params)
                 .BindBuffer("instances", view.preparedInstances)
                 .Push(GrassPushGpu{ 2, bases[2], viewportHeightBits, 0 })
                 .IndirectDraw(view.preparedCommands, 2 * kGrassCommandStride)
                 .Target(f.SceneColor())
                 .DepthTest(f.SceneDepth(), fx::DepthCompare::LessEqual, /*write=*/true)
                 .Draw();
    if (!drawn) {
        f.ReportOnce("A Grass.Draw pass was refused");
        view.snapshot.status = GrassFrameStatus::ShadersNotReady;
        return;
    }
    view.snapshot.status = GrassFrameStatus::Ok;
}

GTE_REGISTER_RENDER_FEATURE("Grass", GrassFeature);

} // namespace gte
