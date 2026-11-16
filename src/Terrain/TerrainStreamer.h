#pragma once

// Node cache and background generation. Main thread only; the jobs it starts never touch the cache.

#include "TerrainHeightField.h"
#include "TerrainLod.h"
#include "TerrainNode.h"
#include "TerrainSettings.h"
#include "TerrainTypes.h"

#include "../Jobs/JobTypes.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace gte::terrain {

// Where node jobs run. The default uses the engine job system; tests may run them inline.
class ITerrainExecutor {
public:
    virtual ~ITerrainExecutor() = default;
    virtual void Run(Jobs::JobFunction fn, void* payload, Jobs::JobHandle& handle) = 0;
};

// Runs every job immediately on the calling thread.
class InlineTerrainExecutor final : public ITerrainExecutor {
public:
    void Run(Jobs::JobFunction fn, void* payload, Jobs::JobHandle&) override { fn(payload); }
};

// Optional source of ready-made nodes (for example baked data). Runs on worker threads, so it must be
// thread safe. Returning nullopt makes the streamer generate the node itself.
using NodeProvider = std::function<std::optional<NodeMesh>(const TerrainSettings& current, const NodeKey& key)>;

struct StreamerConfig {
    std::size_t memoryBudgetBytes = 256u * 1024u * 1024u; // Resident node data; wanted nodes are never evicted.
    int maxInFlight = 4;             // Node jobs running at once.
    int unloadDelayFrames = 90;      // Frames a node may stay unwanted before it can be evicted.
};

struct StreamerStats {
    int resident = 0;          // Ready nodes in the cache.
    int pending = 0;           // Queued plus running.
    int running = 0;
    std::size_t bytesResident = 0;
    double generatedPerSecond = 0.0;
    double buildMsAverage = 0.0; // Over the last 64 builds.
    double buildMsMax = 0.0;
    std::uint64_t builtTotal = 0;
    std::uint64_t evictedTotal = 0;
    int rebuilding = 0;                       // Nodes still showing their old mesh while a carve rebuild runs.
    int carveLastDirtyNodes = 0;              // Nodes the last carve change invalidated.
    std::uint64_t carveInvalidatedTotal = 0;
    double carveRebuildMs = 0.0;              // Wall time of the last finished carve rebuild.
};

struct StreamerUpdate {
    std::vector<std::shared_ptr<const NodeMesh>> ready; // New nodes, or recoloured copies of an existing key.
    std::vector<std::shared_ptr<const NodeMesh>> rebuilt; // New geometry for a key whose old mesh was already published.
    std::vector<NodeKey> evicted;                       // Nodes the render side must release.
};

struct DrawNode {
    NodeKey key;
    std::shared_ptr<const NodeMesh> mesh;
};

class TerrainStreamer {
public:
    explicit TerrainStreamer(const TerrainSettings& settings, const StreamerConfig& config = {},
        ITerrainExecutor* executor = nullptr);
    ~TerrainStreamer();
    TerrainStreamer(const TerrainStreamer&) = delete;
    TerrainStreamer& operator=(const TerrainStreamer&) = delete;

    const TerrainSettings& Settings() const noexcept { return m_field->Settings(); }
    const TerrainHeightField& Field() const noexcept { return *m_field; }
    void SetConfig(const StreamerConfig& config) noexcept { m_config = config; }

    // Applies new settings; returns what kind of rebuild it caused. Evictions land in the next Update().
    SettingsChange SetSettings(const TerrainSettings& settings);

    // Swaps the terrain carve. Nodes touching a `dirty` rectangle are rebuilt; their old mesh keeps drawing until the
    // new one is ready (reported in StreamerUpdate::rebuilt). Returns how many nodes were invalidated.
    int SetCarve(std::shared_ptr<const ITerrainCarve> carve, std::span<const Rect2> dirty);
    std::uint64_t CarveHash() const noexcept { return m_field->CarveHash(); }

    // Sets (or with an empty function removes) the node provider. Resident nodes are dropped and rebuilt.
    void SetNodeProvider(NodeProvider provider);

    // One call per frame. `wanted` comes from LodSelector. `nowSeconds` is any monotonic clock.
    // New nodes and evictions are appended to `out`; the caller clears it after consuming them.
    void Update(const std::vector<WantedNode>& wanted, std::uint64_t frame, double nowSeconds, StreamerUpdate& out);

    // Nodes to draw this frame: wanted and in view; a ready parent or children fill in for nodes still loading.
    void BuildDrawList(const std::vector<WantedNode>& wanted, std::vector<DrawNode>& out) const;

    std::shared_ptr<const NodeMesh> Find(const NodeKey& key) const;
    StreamerStats Stats() const noexcept { return m_stats; }

    // Drops every node (reported by the next Update) and cancels running jobs.
    void Clear();

private:
    struct Job;
    struct Entry;
    static void RunNodeJob(void* payload);
    void StartJobs(std::uint64_t frame);
    void CollectFinished(double nowSeconds);
    void Evict(std::uint64_t frame);
    void EvictEntry(const NodeKey& key);
    void DropMesh(Entry& entry, const NodeKey& key);
    void CollectDescendants(const NodeKey& key, std::vector<DrawNode>& out) const;

    StreamerConfig m_config;
    ITerrainExecutor* m_executor = nullptr;
    std::shared_ptr<const TerrainHeightField> m_field;
    std::shared_ptr<const NodeProvider> m_provider;
    std::uint64_t m_generation = 1;
    std::unordered_map<NodeKey, std::unique_ptr<Entry>, NodeKeyHash> m_entries;
    std::size_t m_bytes = 0;
    std::vector<NodeKey> m_pendingEvictions;
    std::vector<std::shared_ptr<const NodeMesh>> m_pendingReady;
    std::vector<std::shared_ptr<const NodeMesh>> m_pendingRebuilt;
    std::chrono::steady_clock::time_point m_carveStart{};
    bool m_carveTiming = false;
    std::deque<double> m_completionTimes;
    std::deque<double> m_recentBuildMs;
    StreamerStats m_stats;
    std::uint64_t m_lastFrame = 0;
};

} // namespace gte::terrain
