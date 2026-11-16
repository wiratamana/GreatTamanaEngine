#include "GrassFeature.h"

#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Math/Vec3.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 38;
// After the terrain (Opaque stage), before the shadow mask (0), the water (25) and the sky (100).
constexpr std::int32_t kDrawPriority = -20;
constexpr std::uint64_t kCommandBytes = kGrassCommandStride * kGrassLodCount;
constexpr std::uint64_t kTileBytes = std::uint64_t{ kGrassMaxTileTexels } * kGrassMaxTileTexels * 2u * sizeof(float);
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

// Sky and ground ambient of the scene lighting defaults (colour x intensity).
constexpr float kSkyAmbient[3] = { 1.26f * 0.35f, 1.31f * 0.35f, 1.40f * 0.35f };
constexpr float kGroundAmbient[3] = { 0.66f * 0.35f, 0.57f * 0.35f, 0.46f * 0.35f };

} // namespace

GrassFeature::GrassFeature(fx::FeatureHost&) { }

void GrassFeature::Setup(fx::FeatureSetup& s)
{
    if (!m_hiz.Valid()) {
        s.Fail("the occlusion pyramid is invalid: " + m_hiz.Error());
        return;
    }

    // Not a render pass: Always() keeps a toggle from freezing the wind and the synthetic tile.
    s.PreOpaque("Grass.FrameStamp", kFrameStampPriority, [this](fx::PreOpaqueFrame& f) { FrameStamp(f); },
        fx::Gate::Always());

    // The editor's second view skips the grass by default.
    s.DefaultSecondaryViewEnabled(false);

    fx::Gate gate{ "Grass.Init", "Grass.Cull", "Grass.Stats.Copy", "Grass.Draw.L0", "Grass.Draw.L1", "Grass.Draw.L2" };
    for (std::string& name : m_hiz.AllPassNames()) {
        gate.passNames.push_back(std::move(name));
    }
    s.PostOpaque("Grass.Draw", kDrawPriority, [this](fx::PostOpaqueFrame& f) { DrawPass(f); }, std::move(gate));
}

void GrassFeature::ApplySettings(const GrassSettings& candidate)
{
    m_settings = SanitizeGrassSettings(candidate);
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

void GrassFeature::FillEnvironment(fx::PostOpaqueFrame& f, GrassFrameInputs& in, GrassViewSnapshot& snap)
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

void GrassFeature::DrawPass(fx::PostOpaqueFrame& f)
{
    GrassViewState& view = m_views.For(f.Scope());
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
    if (!f.PassEnabled("Grass.Init") || !f.PassEnabled("Grass.Cull") || !f.PassEnabled("Grass.Draw.L0")
        || !f.PassEnabled("Grass.Draw.L1") || !f.PassEnabled("Grass.Draw.L2")) {
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
    const fx::GpuBufferRef commands = m_commands.Acquire(f, kCommandBytes);
    const fx::GpuBufferRef heights = m_heights.Acquire(f, kTileBytes);
    if (!instances.IsValid() || !commands.IsValid() || !heights.IsValid()) {
        bail(GrassFrameStatus::TargetFailed);
        return;
    }

    // Each frame-slot copy of the tile is refreshed once per tile change (and after the copy was recreated).
    const std::size_t slot = f.OffscreenTicket().slot % view.slotRevision.size();
    const std::uint64_t generation = m_heights.Generation(f);
    if (view.slotRevision[slot] != m_tileRevision || view.slotGeneration[slot] != generation) {
        const std::uint64_t bytes = std::uint64_t{ tile->size } * tile->size * 2u * sizeof(float);
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
    FillEnvironment(f, in, snap);

    // The scene depth is final here, so the pyramid sees the terrain of this very frame.
    fx::Texture hizAtlas;
    if (cfg.hizCulling) {
        const fx::HizPlan plan = m_hiz.Plan(f, f.SceneDepth());
        const HizDepthMap map = HizDepthMapOf(v.relative.viewProjection);
        if (plan.Usable() && !map.raw) {
            hizAtlas = m_hiz.Build(f, plan, f.SceneDepth());
            if (hizAtlas.IsValid()) {
                in.hizUsable = true;
                in.hizAtlas[0] = static_cast<float>(plan.layout.atlasWidth);
                in.hizAtlas[1] = static_cast<float>(plan.layout.atlasHeight);
                in.hizBase[0] = static_cast<float>(plan.layout.baseWidth);
                in.hizBase[1] = static_cast<float>(plan.layout.baseHeight);
                in.hizDepthK = map.k;
                in.hizDepthB = map.b;
                in.hizLevels = static_cast<float>(plan.layout.levelCount);
            }
        }
    }
    snap.hizUsed = in.hizUsable;
    const GrassParamsGpu params = MakeGrassParams(cfg, in);

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
    // A sampler needs a valid image even when the pyramid is off (the flag skips the read).
    const std::uint32_t chunkCount = params.grid[2] > 0 ? static_cast<std::uint32_t>(params.grid[2] * params.grid[2]) : 0u;
    const bool culled = f.Compute(m_cullShader, "Grass.Cull")
                            .Params(view.cullParams, params)
                            .BindBuffer("heightTile", heights)
                            .BindBuffer("instances", instances)
                            .BindBuffer("commands", commands)
                            .BindOutput("stats", m_stats)
                            .Bind("hizAtlas", hizAtlas.IsValid() ? hizAtlas : f.SceneColor())
                            .Dispatch(Extent2D{ chunkCount * kGrassCullGroupSize, 1 });
    if (!culled) {
        f.ReportOnce("Grass.Cull was refused");
        bail(GrassFrameStatus::ShadersNotReady);
        return;
    }
    f.CopyReadbackToHost("Grass.Stats.Copy", m_stats);

    bool drawn = true;
    const std::array<std::uint32_t, kGrassLodCount> bases{ params.instanceBase[0], params.instanceBase[1], params.instanceBase[2] };
    drawn &= f.Fullscreen(m_drawShader, "Grass.Draw.L0")
                 .Params(view.drawParams[0], params)
                 .BindBuffer("instances", instances)
                 .Push(GrassPushGpu{ 0, bases[0], 0, 0 })
                 .IndirectDraw(commands, 0)
                 .Target(f.SceneColor())
                 .DepthTest(f.SceneDepth(), fx::DepthCompare::LessEqual, /*write=*/true)
                 .Draw();
    drawn &= f.Fullscreen(m_drawShader, "Grass.Draw.L1")
                 .Params(view.drawParams[1], params)
                 .BindBuffer("instances", instances)
                 .Push(GrassPushGpu{ 1, bases[1], 0, 0 })
                 .IndirectDraw(commands, kGrassCommandStride)
                 .Target(f.SceneColor())
                 .DepthTest(f.SceneDepth(), fx::DepthCompare::LessEqual, /*write=*/true)
                 .Draw();
    drawn &= f.Fullscreen(m_drawShader, "Grass.Draw.L2")
                 .Params(view.drawParams[2], params)
                 .BindBuffer("instances", instances)
                 .Push(GrassPushGpu{ 2, bases[2], 0, 0 })
                 .IndirectDraw(commands, 2 * kGrassCommandStride)
                 .Target(f.SceneColor())
                 .DepthTest(f.SceneDepth(), fx::DepthCompare::LessEqual, /*write=*/true)
                 .Draw();
    if (!drawn) {
        f.ReportOnce("A Grass.Draw pass was refused");
        bail(GrassFrameStatus::ShadersNotReady);
        return;
    }

    if (const auto sample = m_stats.Latest(f)) {
        snap.stats = sample->value;
        snap.statsValid = true;
        snap.statsFrame = sample->frame;
    }
    snap.extent = v.extent;
    snap.gridCells = cfg.gridCells;
    snap.tileRevision = tile->revision;
    snap.instanceBytes = instanceBytes;
    snap.status = GrassFrameStatus::Ok;
    view.snapshot = snap;
}

GTE_REGISTER_RENDER_FEATURE("Grass", GrassFeature);

} // namespace gte
