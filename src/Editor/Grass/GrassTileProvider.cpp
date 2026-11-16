#include "GrassTileProvider.h"

#include "../../Core/Logging.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <exception>

namespace gte {

namespace {

constexpr const char* kLogCategory = "GrassTiles";
constexpr double kMinRebuildSeconds = 0.25;
constexpr float kMinNodeCoverage = 0.5f;     // Below this the terrain is not resident yet: submit nothing.
constexpr float kCompleteCoverage = 0.98f;   // Level-0 share that counts as a finished tile.
constexpr double kSettleSeconds = 4.0; // After a terrain edit every rebuilt node triggers a tile rebuild.

using Clock = std::chrono::steady_clock;

std::uint64_t Mix(std::uint64_t h, std::uint64_t v) noexcept
{
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t SettingsKey(const GrassSettings& s) noexcept
{
    std::uint64_t h = 0xCBF29CE484222325ull;
    h = Mix(h, s.heightTileTexels);
    h = Mix(h, std::bit_cast<std::uint32_t>(s.heightTileTexelSize));
    h = Mix(h, std::bit_cast<std::uint32_t>(s.lod2Distance));
    h = Mix(h, std::bit_cast<std::uint32_t>(s.waterMargin));
    return h;
}

} // namespace

GrassTileProvider::GrassTileProvider(ServicesFn services)
    : m_services(std::move(services))
    , m_worker([this](const std::stop_token& stop) { WorkerLoop(stop); })
{
}

GrassTileProvider::~GrassTileProvider()
{
    Disconnect();
    m_worker.request_stop();
}

void GrassTileProvider::Connect(GrassFeature& grass)
{
    Disconnect();
    grass.SetTileSourceHooks([this] { return Report(); }, [this](double x, double z) { return Probe(x, z); });
    m_hooked = &grass;
}

void GrassTileProvider::Disconnect()
{
    if (m_hooked != nullptr) {
        m_hooked->SetTileSourceHooks({}, {});
        m_hooked = nullptr;
    }
}

void GrassTileProvider::WorkerLoop(const std::stop_token& stop)
{
    while (!stop.stop_requested()) {
        Pending pending;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return m_pending.has_value(); })) {
                return;
            }
            pending = std::move(*m_pending);
            m_pending.reset();
        }
        Finished finished = std::move(pending.meta);
        try {
            finished.build = BuildGrassTile(pending.job);
        } catch (const std::exception& e) {
            GTE_LOG_ERROR(kLogCategory, std::string("tile build failed: ") + e.what());
        }
        std::lock_guard lock(m_mutex);
        m_finished = std::move(finished);
        m_busy = false;
    }
}

void GrassTileProvider::Accept(const Finished& finished, GrassFeature& grass)
{
    const GrassTileBuild& build = finished.build;
    {
        std::lock_guard lock(m_mutex);
        ++m_report.builds;
        m_report.lastBuildMs = build.buildMs;
        m_report.maxBuildMs = std::max(m_report.maxBuildMs, build.buildMs);
        m_report.coverageLevel0 = build.coverageLevel0;
        m_report.coverageNodes = build.coverageNodes;
        m_report.submergedShare = build.submergedShare;
    }
    m_lastSeen = finished.seen;
    m_lastComplete = build.coverageLevel0 >= kCompleteCoverage;
    if (build.tile == nullptr || build.coverageNodes < kMinNodeCoverage) {
        // The terrain around the camera is not resident yet; a tile of noise guesses would put grass in the air.
        m_waitingForNodes = true;
        std::lock_guard lock(m_mutex);
        ++m_report.skippedIncomplete;
        return;
    }
    m_waitingForNodes = false;
    grass.SubmitHeightTile(build.tile);
    m_submitted = build.tile;
    std::lock_guard lock(m_mutex);
    m_report.revision = build.tile->revision;
    m_report.size = build.tile->size;
    m_report.texelSize = build.tile->texelSize;
    m_report.originX = build.tile->originX;
    m_report.originZ = build.tile->originZ;
    m_report.complete = m_lastComplete;
}

void GrassTileProvider::Tick(double nowSeconds, double cameraX, double cameraZ)
{
    const GrassTileServices svc = m_services();
    if (svc.grass == nullptr) {
        Disconnect();
        return;
    }
    GrassFeature& grass = *svc.grass;
    if (m_hooked != &grass) {
        Connect(grass);
    }
    const bool open = svc.terrain != nullptr && svc.terrain->IsOpen();
    {
        std::lock_guard lock(m_mutex);
        m_report.terrainOpen = open;
    }
    if (!open || grass.SyntheticTileEnabled()) {
        if (m_terrainWasOpen || m_submitted != nullptr) {
            ++m_epoch;
            if (m_submitted != nullptr && !grass.SyntheticTileEnabled()) {
                grass.SubmitHeightTile(nullptr);
            }
            m_submitted.reset();
            m_waitingForNodes = false;
            std::lock_guard lock(m_mutex);
            m_finished.reset();
        }
        m_terrainWasOpen = open;
        return;
    }
    m_terrainWasOpen = true;

    std::optional<Finished> done;
    {
        std::lock_guard lock(m_mutex);
        if (m_finished.has_value()) {
            done = std::move(m_finished);
            m_finished.reset();
        }
    }
    if (done.has_value() && done->epoch == m_epoch) {
        Accept(*done, grass);
    }

    {
        std::lock_guard lock(m_mutex);
        if (m_busy || m_pending.has_value()) {
            return;
        }
    }
    if (nowSeconds - m_lastLaunch < kMinRebuildSeconds) {
        return;
    }

    const Clock::time_point start = Clock::now();
    const GrassSettings& settings = grass.Settings();
    const std::uint32_t size = std::clamp(settings.heightTileTexels, 2u, kGrassMaxTileTexels);
    const float texel = settings.heightTileTexelSize;
    const double origin[2] = { SnapGrassTileOrigin(cameraX, size, texel), SnapGrassTileOrigin(cameraZ, size, texel) };
    const double extent = static_cast<double>(size) * static_cast<double>(texel);

    terrain::SurfaceSnapshot surface = svc.terrain->SnapshotSurface(origin[0], origin[1], origin[0] + extent, origin[1] + extent);
    if (!surface.open) {
        return;
    }
    const std::shared_ptr<const water::WaterBodySet> bodies = svc.water != nullptr ? svc.water->Bodies() : nullptr;
    const std::uint64_t waterHash = bodies != nullptr ? bodies->Hash() : 0;
    GrassTileSeen seen;
    seen.settingsKey = SettingsKey(settings);
    seen.waterHash = waterHash;
    seen.carveHash = surface.carveHash;
    seen.terrainHash = terrain::Hash(svc.terrain->Settings());
    seen.streamRevision = surface.streamRevision;
    seen.rebuilding = surface.rebuilding;
    for (const std::shared_ptr<const terrain::NodeMesh>& node : surface.nodes) {
        if (node->key.level <= 1) {
            seen.fineNodes.emplace(node->key, node.get());
            seen.pinned.push_back(node);
        }
    }

    GrassTileView view;
    view.haveTile = m_submitted != nullptr;
    if (m_submitted != nullptr) {
        view.originX = m_submitted->originX;
        view.originZ = m_submitted->originZ;
        view.extent = static_cast<double>(m_submitted->size) * static_cast<double>(m_submitted->texelSize);
    }
    view.reach = static_cast<double>(settings.lod2Distance) + 32.0;
    view.cameraX = cameraX;
    view.cameraZ = cameraZ;
    view.now = nowSeconds;
    view.settleUntil = m_settleUntil;
    const std::optional<GrassTileReason> reason = DecideGrassTileRebuild(view, m_lastSeen, seen);
    if (!reason.has_value()) {
        return;
    }
    if (*reason == GrassTileReason::Terrain) {
        m_settleUntil = nowSeconds + kSettleSeconds;
    }

    Pending pending;
    pending.job.size = size;
    pending.job.texelSize = texel;
    pending.job.originX = origin[0];
    pending.job.originZ = origin[1];
    pending.job.centerX = cameraX;
    pending.job.centerZ = cameraZ;
    pending.job.reach = view.reach;
    pending.job.waterMargin = settings.waterMargin;
    pending.job.revision = ++m_revisionCounter;
    pending.meta.epoch = m_epoch;
    pending.meta.seen = std::move(seen);
    pending.job.surface = std::move(surface);
    pending.job.water = bodies;
    m_lastLaunch = nowSeconds;
    m_lastMainMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    {
        std::lock_guard lock(m_mutex);
        ++m_report.reasons[static_cast<std::size_t>(*reason)];
        m_report.lastMainMs = m_lastMainMs;
        m_busy = true;
        m_pending = std::move(pending);
    }
    m_wake.notify_one();
}

GrassTileSourceReport GrassTileProvider::Report() const
{
    std::lock_guard lock(m_mutex);
    GrassTileSourceReport report = m_report;
    report.building = m_busy || m_pending.has_value();
    return report;
}

GrassProbeReport GrassTileProvider::Probe(double x, double z) const
{
    GrassProbeReport r;
    const GrassTileServices svc = m_services();
    if (svc.terrain == nullptr || !svc.terrain->IsOpen() || svc.grass == nullptr) {
        r.message = "the terrain is not open";
        return r;
    }
    const std::shared_ptr<const GrassHeightTile> tile = svc.grass->HeightTile();
    float tileHeight = 0.0f;
    float tileFlag = 0.0f;
    if (tile == nullptr || !SampleGrassTile(*tile, x, z, tileHeight, tileFlag)) {
        r.message = "the point is outside the height tile";
        return r;
    }
    const terrain::SurfaceSnapshot surface = svc.terrain->SnapshotSurface(x - 1.0, z - 1.0, x + 1.0, z + 1.0);
    const terrain::SurfaceSampler sampler(surface);
    const terrain::NodeMesh* hint = nullptr;
    const terrain::SurfaceHit hit = sampler.Sample(x, z, hint);
    r.tileHeight = static_cast<double>(tileHeight);
    r.tileSubmerged = tileFlag > 0.5f;
    r.meshHeight = static_cast<double>(hit.height);
    r.nodeLevel = hit.level;
    if (const auto noise = svc.terrain->ProbeHeight(x, z)) {
        r.noiseHeight = noise->carvedHeight;
    }
    if (svc.water != nullptr) {
        if (const std::shared_ptr<const water::WaterBodySet> bodies = svc.water->Bodies()) {
            const water::WaterSample w = bodies->SampleAt(x, z);
            r.waterHere = w.hasWater;
            r.waterSurface = static_cast<double>(w.surfaceY);
        }
    }
    r.ok = hit.level >= 0;
    if (!r.ok) {
        r.message = "no resident terrain node at this point";
    }
    return r;
}

} // namespace gte
