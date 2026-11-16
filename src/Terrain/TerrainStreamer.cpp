#include "TerrainStreamer.h"

#include "../Jobs/JobSystem.h"
#include "../Profiling/JobScopeTimer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <unordered_set>

namespace gte::terrain {

namespace {

constexpr std::size_t kRecentBuildWindow = 64;
constexpr double kPrefetchPenalty = 1.0e9; // Out-of-view nodes load after every visible one.
constexpr int kMaxFallbackDepth = 8;

class JobSystemExecutor final : public ITerrainExecutor {
public:
    void Run(Jobs::JobFunction fn, void* payload, Jobs::JobHandle& handle) override
    {
        Jobs::JobSystem::Instance().Schedule(fn, payload, handle);
    }
};

ITerrainExecutor& DefaultExecutor()
{
    static JobSystemExecutor executor;
    return executor;
}

Rect2 NodeRect(const TerrainLayout& layout, const NodeKey& key) noexcept
{
    const NodeGeometry g = NodeGeometryOf(layout, key);
    return { g.originX - g.spacing, g.originZ - g.spacing, g.originX + g.size + g.spacing, g.originZ + g.size + g.spacing };
}

// Baked nodes know nothing about the carve, so a node it can change is generated instead.
bool CarveTouchesNode(const TerrainHeightField& field, const NodeKey& key) noexcept
{
    const ITerrainCarve* carve = field.Carve();
    return carve != nullptr && carve->Affects(NodeRect(field.Layout(), key), NodeGeometryOf(field.Layout(), key).spacing);
}

} // namespace

// Owned by the cache entry until the job is collected, so the job never outlives its data.
struct TerrainStreamer::Job {
    std::shared_ptr<const TerrainHeightField> field;
    std::shared_ptr<const NodeProvider> provider;
    NodeKey key;
    std::uint64_t generation = 0;
    std::atomic<bool> cancel{ false };
    std::shared_ptr<NodeMesh> result; // Written only by the job.
    double ms = 0.0;
    bool failed = false;
    Jobs::JobHandle handle;
};

struct TerrainStreamer::Entry {
    enum class State : std::uint8_t { Queued, Running, Ready, Failed };
    State state = State::Queued;
    std::shared_ptr<const NodeMesh> mesh; // Published mesh; kept while a carve rebuild runs.
    bool rebuilding = false;
    std::unique_ptr<Job> job;
    std::uint64_t lastWanted = 0;
    double distance = 0.0;
    bool inView = true;
};

// Job body: no exception leaves it; an error becomes a failed job.
void TerrainStreamer::RunNodeJob(void* raw)
{
    auto& job = *static_cast<Job*>(raw);
    GTE_PROFILE_JOB_SCOPE("Terrain.BuildNode");
    try {
        if (!job.cancel.load(std::memory_order_relaxed)) {
            const auto start = std::chrono::steady_clock::now();
            std::optional<NodeMesh> provided;
            if (job.provider && !CarveTouchesNode(*job.field, job.key)) {
                provided = (*job.provider)(job.field->Settings(), job.key);
            }
            job.result = provided ? std::make_shared<NodeMesh>(std::move(*provided))
                                  : std::make_shared<NodeMesh>(BuildNode(*job.field, job.key));
            job.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
    } catch (...) {
        job.failed = true;
        job.result.reset();
    }
}

TerrainStreamer::TerrainStreamer(const TerrainSettings& settings, const StreamerConfig& config, ITerrainExecutor* executor)
    : m_config(config)
    , m_executor(executor != nullptr ? executor : &DefaultExecutor())
    , m_field(std::make_shared<const TerrainHeightField>(Sanitize(settings)))
{
}

TerrainStreamer::~TerrainStreamer()
{
    for (auto& [key, entry] : m_entries) {
        if (entry->job) {
            entry->job->cancel.store(true, std::memory_order_relaxed);
            Jobs::JobSystem::Instance().WaitForJobs(entry->job->handle);
        }
    }
}

SettingsChange TerrainStreamer::SetSettings(const TerrainSettings& requested)
{
    const TerrainSettings next = Sanitize(requested);
    const SettingsChange change = ClassifyChange(m_field->Settings(), next);
    if (change == SettingsChange::None) {
        return change;
    }
    m_field = std::make_shared<const TerrainHeightField>(next, m_field->CarveShared());

    if (HasChange(change, SettingsChange::Height)) {
        ++m_generation;
        std::vector<NodeKey> doomed;
        for (auto& [key, entry] : m_entries) {
            if (entry->state == Entry::State::Running) {
                entry->job->cancel.store(true, std::memory_order_relaxed);
                DropMesh(*entry, key);
            } else {
                doomed.push_back(key);
            }
        }
        for (const NodeKey& key : doomed) {
            EvictEntry(key);
        }
    } else if (HasChange(change, SettingsChange::Material)) {
        for (auto& [key, entry] : m_entries) {
            if (entry->mesh) {
                auto copy = std::make_shared<NodeMesh>(*entry->mesh);
                RecolorNode(*copy, next);
                entry->mesh = copy;
                m_pendingReady.push_back(std::move(copy));
            }
        }
    }
    return change;
}

void TerrainStreamer::Clear()
{
    ++m_generation;
    std::vector<NodeKey> doomed;
    for (auto& [key, entry] : m_entries) {
        if (entry->state == Entry::State::Running) {
            entry->job->cancel.store(true, std::memory_order_relaxed);
            DropMesh(*entry, key);
        } else {
            doomed.push_back(key);
        }
    }
    for (const NodeKey& key : doomed) {
        EvictEntry(key);
    }
}

int TerrainStreamer::SetCarve(std::shared_ptr<const ITerrainCarve> carve, std::span<const Rect2> dirty)
{
    const std::uint64_t nextHash = carve ? carve->Hash() : 0;
    if (nextHash == m_field->CarveHash() && static_cast<bool>(carve) == (m_field->Carve() != nullptr)) {
        return 0;
    }
    const std::shared_ptr<const ITerrainCarve> previous = m_field->CarveShared();
    m_field = std::make_shared<const TerrainHeightField>(m_field->Settings(), std::move(carve));
    const ITerrainCarve* next = m_field->Carve();
    int invalidated = 0;
    for (auto& [key, entryPtr] : m_entries) {
        Entry& entry = *entryPtr;
        if (entry.state == Entry::State::Queued && !entry.mesh) {
            continue; // Not started yet: it will use the new field.
        }
        const NodeGeometry geometry = NodeGeometryOf(m_field->Layout(), key);
        const Rect2 rect = NodeRect(m_field->Layout(), key);
        if (std::none_of(dirty.begin(), dirty.end(), [&rect](const Rect2& d) { return d.Overlaps(rect); })) {
            continue;
        }
        // Either carve must be able to move a vertex of this node, else its mesh is already right.
        const bool touched = (previous && previous->Affects(rect, geometry.spacing)) || (next && next->Affects(rect, geometry.spacing));
        if (!touched) {
            continue;
        }
        ++invalidated;
        if (entry.state == Entry::State::Running) {
            entry.job->cancel.store(true, std::memory_order_relaxed);
        } else {
            entry.state = Entry::State::Queued;
        }
        entry.rebuilding = entry.mesh != nullptr;
    }
    m_stats.carveLastDirtyNodes = invalidated;
    m_stats.carveInvalidatedTotal += static_cast<std::uint64_t>(invalidated);
    if (invalidated > 0) {
        m_carveStart = std::chrono::steady_clock::now();
        m_carveTiming = true;
    }
    return invalidated;
}

void TerrainStreamer::DropMesh(Entry& entry, const NodeKey& key)
{
    if (!entry.mesh) {
        return;
    }
    m_bytes -= std::min(m_bytes, entry.mesh->ByteSize());
    m_pendingEvictions.push_back(key);
    ++m_stats.evictedTotal;
    entry.mesh.reset();
    entry.rebuilding = false;
}

void TerrainStreamer::SetNodeProvider(NodeProvider provider)
{
    m_provider = provider ? std::make_shared<const NodeProvider>(std::move(provider)) : nullptr;
    Clear();
}

void TerrainStreamer::EvictEntry(const NodeKey& key)
{
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return;
    }
    if (it->second->mesh) {
        m_bytes -= std::min(m_bytes, it->second->mesh->ByteSize());
        m_pendingEvictions.push_back(key);
        ++m_stats.evictedTotal;
    }
    m_entries.erase(it);
}

void TerrainStreamer::CollectFinished(double nowSeconds)
{
    for (auto& [key, entryPtr] : m_entries) {
        Entry& entry = *entryPtr;
        if (entry.state != Entry::State::Running || !entry.job->handle.IsComplete()) {
            continue;
        }
        Job& job = *entry.job;
        const bool stale = job.generation != m_generation || job.cancel.load(std::memory_order_relaxed);
        if (job.failed) {
            entry.state = entry.mesh ? Entry::State::Ready : Entry::State::Failed;
            entry.rebuilding = false;
            entry.job.reset();
        } else if (stale || !job.result) {
            entry.state = Entry::State::Queued;
            entry.job.reset();
        } else {
            if (job.field.get() != m_field.get() && HasChange(ClassifyChange(job.field->Settings(), m_field->Settings()), SettingsChange::Material)) {
                RecolorNode(*job.result, m_field->Settings());
            }
            m_recentBuildMs.push_back(job.ms);
            if (m_recentBuildMs.size() > kRecentBuildWindow) {
                m_recentBuildMs.pop_front();
            }
            m_completionTimes.push_back(nowSeconds);
            ++m_stats.builtTotal;
            const bool replacing = entry.mesh != nullptr;
            if (replacing) {
                m_bytes -= std::min(m_bytes, entry.mesh->ByteSize());
            }
            entry.mesh = std::move(job.result);
            entry.state = Entry::State::Ready;
            entry.rebuilding = false;
            entry.job.reset();
            m_bytes += entry.mesh->ByteSize();
            (replacing ? m_pendingRebuilt : m_pendingReady).push_back(entry.mesh);
        }
    }
}

void TerrainStreamer::StartJobs(std::uint64_t frame)
{
    struct Candidate {
        double priority;
        NodeKey key;
        Entry* entry;
    };
    int running = 0;
    std::vector<Candidate> candidates;
    std::vector<NodeKey> unwantedQueued;
    for (auto& [key, entryPtr] : m_entries) {
        Entry& entry = *entryPtr;
        if (entry.state == Entry::State::Running) {
            ++running;
        } else if (entry.state == Entry::State::Queued) {
            if (entry.lastWanted != frame) {
                unwantedQueued.push_back(key);
            } else {
                candidates.push_back({ entry.distance + (entry.inView ? 0.0 : kPrefetchPenalty), key, &entry });
            }
        }
    }
    for (const NodeKey& key : unwantedQueued) {
        EvictEntry(key);
    }
    const int free = m_config.maxInFlight - running;
    if (free <= 0 || candidates.empty()) {
        return;
    }
    const std::size_t take = std::min(candidates.size(), static_cast<std::size_t>(free));
    std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(take), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });
    for (std::size_t i = 0; i < take; ++i) {
        Entry& entry = *candidates[i].entry;
        entry.job = std::make_unique<Job>();
        entry.job->field = m_field;
        entry.job->provider = m_provider;
        entry.job->key = candidates[i].key;
        entry.job->generation = m_generation;
        entry.state = Entry::State::Running;
        m_executor->Run(&TerrainStreamer::RunNodeJob, entry.job.get(), entry.job->handle);
    }
}

void TerrainStreamer::Evict(std::uint64_t frame)
{
    const std::uint64_t delay = static_cast<std::uint64_t>(std::max(0, m_config.unloadDelayFrames));
    std::vector<NodeKey> doomed;
    for (auto& [key, entryPtr] : m_entries) {
        Entry& entry = *entryPtr;
        const std::uint64_t unwantedFor = frame > entry.lastWanted ? frame - entry.lastWanted : 0;
        if (unwantedFor <= delay) {
            continue;
        }
        if (entry.state == Entry::State::Running) {
            entry.job->cancel.store(true, std::memory_order_relaxed);
        } else {
            doomed.push_back(key);
        }
    }
    for (const NodeKey& key : doomed) {
        EvictEntry(key);
    }

    if (m_bytes <= m_config.memoryBudgetBytes) {
        return;
    }
    // Over budget: drop the nodes unwanted the longest. Wanted nodes stay.
    std::vector<std::pair<std::uint64_t, NodeKey>> order;
    for (auto& [key, entryPtr] : m_entries) {
        if (entryPtr->state != Entry::State::Running && entryPtr->mesh && entryPtr->lastWanted != frame) {
            order.emplace_back(entryPtr->lastWanted, key);
        }
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [lastWanted, key] : order) {
        if (m_bytes <= m_config.memoryBudgetBytes) {
            break;
        }
        EvictEntry(key);
    }
}

void TerrainStreamer::Update(const std::vector<WantedNode>& wanted, std::uint64_t frame, double nowSeconds, StreamerUpdate& out)
{
    m_lastFrame = frame;
    for (const WantedNode& w : wanted) {
        auto [it, inserted] = m_entries.try_emplace(w.key);
        if (inserted) {
            it->second = std::make_unique<Entry>();
        }
        Entry& entry = *it->second;
        entry.lastWanted = frame;
        entry.distance = w.distance;
        entry.inView = w.inView;
    }
    CollectFinished(nowSeconds);
    StartJobs(frame);
    Evict(frame);

    out.ready.insert(out.ready.end(), std::make_move_iterator(m_pendingReady.begin()), std::make_move_iterator(m_pendingReady.end()));
    m_pendingReady.clear();
    out.rebuilt.insert(out.rebuilt.end(), std::make_move_iterator(m_pendingRebuilt.begin()), std::make_move_iterator(m_pendingRebuilt.end()));
    m_pendingRebuilt.clear();
    out.evicted.insert(out.evicted.end(), m_pendingEvictions.begin(), m_pendingEvictions.end());
    m_pendingEvictions.clear();

    while (!m_completionTimes.empty() && nowSeconds - m_completionTimes.front() > 1.0) {
        m_completionTimes.pop_front();
    }
    int resident = 0;
    int pending = 0;
    int running = 0;
    int rebuilding = 0;
    for (const auto& [key, entry] : m_entries) {
        resident += entry->mesh ? 1 : 0;
        rebuilding += entry->rebuilding ? 1 : 0;
        pending += (entry->state == Entry::State::Queued || entry->state == Entry::State::Running) ? 1 : 0;
        running += entry->state == Entry::State::Running ? 1 : 0;
    }
    m_stats.resident = resident;
    m_stats.rebuilding = rebuilding;
    if (m_carveTiming && rebuilding == 0) {
        m_stats.carveRebuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_carveStart).count();
        m_carveTiming = false;
    }
    m_stats.pending = pending;
    m_stats.running = running;
    m_stats.bytesResident = m_bytes;
    m_stats.generatedPerSecond = static_cast<double>(m_completionTimes.size());
    double sum = 0.0;
    double mx = 0.0;
    for (const double ms : m_recentBuildMs) {
        sum += ms;
        mx = std::max(mx, ms);
    }
    m_stats.buildMsAverage = m_recentBuildMs.empty() ? 0.0 : sum / static_cast<double>(m_recentBuildMs.size());
    m_stats.buildMsMax = mx;
}

std::shared_ptr<const NodeMesh> TerrainStreamer::Find(const NodeKey& key) const
{
    const auto it = m_entries.find(key);
    return it == m_entries.end() ? nullptr : it->second->mesh;
}

void TerrainStreamer::CollectDescendants(const NodeKey& key, std::vector<DrawNode>& out) const
{
    if (const auto mesh = Find(key)) {
        out.push_back({ key, mesh });
        return;
    }
    if (key.level == 0) {
        return;
    }
    for (int c = 0; c < 4; ++c) {
        CollectDescendants(key.Child(c), out);
    }
}

void TerrainStreamer::BuildDrawList(const std::vector<WantedNode>& wanted, std::vector<DrawNode>& out) const
{
    out.clear();
    std::vector<DrawNode> chosen;
    chosen.reserve(wanted.size());
    for (const WantedNode& w : wanted) {
        if (!w.inView) {
            continue;
        }
        if (const auto mesh = Find(w.key)) {
            chosen.push_back({ w.key, mesh });
            continue;
        }
        bool found = false;
        NodeKey ancestor = w.key;
        for (int i = 0; i < kMaxFallbackDepth && !found; ++i) {
            ancestor = ancestor.Parent();
            if (const auto mesh = Find(ancestor)) {
                chosen.push_back({ ancestor, mesh });
                found = true;
            }
        }
        if (!found) {
            CollectDescendants(w.key, chosen);
        }
    }

    // Drop duplicates, and any node that sits under another chosen node (no overlapping surfaces).
    std::unordered_set<NodeKey, NodeKeyHash> keys;
    std::unordered_set<NodeKey, NodeKeyHash> emitted;
    keys.reserve(chosen.size() * 2);
    for (const DrawNode& d : chosen) {
        keys.insert(d.key);
    }
    for (const DrawNode& d : chosen) {
        bool covered = false;
        NodeKey up = d.key;
        for (int i = 0; i < kMaxFallbackDepth && !covered && up.level < 255; ++i) {
            up = up.Parent();
            covered = keys.contains(up);
        }
        if (!covered && emitted.insert(d.key).second) {
            out.push_back(d);
        }
    }
}

} // namespace gte::terrain
