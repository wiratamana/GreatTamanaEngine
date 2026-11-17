#pragma once

// Keeps the Grass feature's height tile in step with the live terrain and water.
// Tick() runs once per frame on the main thread; the tile itself is built on one worker thread.

#include "GrassTileBuilder.h"
#include "../GrassFeature.h"
#include "../../Terrain/TerrainControl.h"
#include "../../Water/World/WaterControl.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <vector>

namespace gte {

// Non-owning handles, resolved fresh on every use.
struct GrassTileServices {
    terrain::ITerrainWorld* terrain = nullptr;
    water::IWaterWorld* water = nullptr;
    GrassFeature* grass = nullptr; // Null when the Grass feature is not built in.
};

class GrassTileProvider {
public:
    using ServicesFn = std::function<GrassTileServices()>;

    explicit GrassTileProvider(ServicesFn services);
    ~GrassTileProvider();
    GrassTileProvider(const GrassTileProvider&) = delete;
    GrassTileProvider& operator=(const GrassTileProvider&) = delete;

    // `cameraX/Z` is the primary camera in world metres.
    void Tick(double nowSeconds, double cameraX, double cameraZ);

    GrassTileSourceReport Report() const;
    GrassProbeReport Probe(double x, double z) const;

private:
    struct Finished {
        GrassTileBuild build;
        std::uint64_t epoch = 0;
        GrassTileSeen seen; // What the build saw.
    };
    struct Pending {
        GrassTileJob job;
        Finished meta; // Everything but the build, filled when the job is queued.
    };

    void Connect(GrassFeature& grass);
    void Disconnect();
    void WorkerLoop(const std::stop_token& stop);
    void Accept(const Finished& finished, GrassFeature& grass);

    ServicesFn m_services;
    GrassFeature* m_hooked = nullptr;

    // Main thread state.
    bool m_terrainWasOpen = false;
    std::uint64_t m_epoch = 1;
    std::uint64_t m_revisionCounter = 0;
    std::shared_ptr<const GrassHeightTile> m_submitted;
    double m_lastLaunch = -1.0e9;
    bool m_waitingForNodes = false;
    bool m_lastComplete = false;
    GrassTileSeen m_lastSeen;
    double m_settleUntil = -1.0;
    double m_lastMainMs = 0.0;

    // Shared with the worker.
    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::optional<Pending> m_pending;
    std::optional<Finished> m_finished;
    bool m_busy = false;
    GrassTileSourceReport m_report;

    std::jthread m_worker; // Last member: joined first.
};

} // namespace gte
