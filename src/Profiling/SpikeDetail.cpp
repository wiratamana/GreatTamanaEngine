#include "SpikeDetail.h"

#include "NameTable.h"
#include "ProfilingClock.h"
#include "ScopeTree.h"

#include <algorithm>
#include <array>
#include <memory>

namespace gte::Profiling {

namespace {

std::string NameOf(NameId id)
{
    return id == kInvalidNameId ? std::string() : std::string(ResolveName(id));
}

} // namespace

SpikeDetail BuildFrameDetail(const FrameRecord& record)
{
    SpikeDetail detail;
    detail.frameIndex = record.summary.frameIndex;
    detail.frameMs = record.summary.cpuMilliseconds;
    detail.gpu = record.summary.gpu;
    detail.droppedScopes = record.scopes.Dropped();

    const std::span<const ScopeEvent> events = record.scopes.Events();
    const auto times = std::make_unique<std::array<ScopeTimes, kMaxScopeEventsPerFrame>>();
    ComputeScopeTimes(events, *times);

    const std::uint64_t frameStart = record.summary.startTicks;
    detail.scopes.reserve(events.size());
    for (std::size_t i = 0; i < events.size() && i < times->size(); ++i) {
        const ScopeEvent& event = events[i];
        SpikeScopeRow row;
        row.name = NameOf(event.name);
        row.startMs = TicksToMilliseconds(event.startTicks >= frameStart ? event.startTicks - frameStart : 0);
        row.durationMs = TicksToMilliseconds(DurationTicks(event));
        row.selfMs = TicksToMilliseconds((*times)[i].selfTicks);
        row.depth = event.depth;
        row.kind = event.kind;
        detail.scopes.push_back(std::move(row));
    }
    std::stable_sort(detail.scopes.begin(), detail.scopes.end(), [](const SpikeScopeRow& a, const SpikeScopeRow& b) {
        return a.startMs != b.startMs ? a.startMs < b.startMs : a.depth < b.depth;
    });

    const std::span<const WorkerEvent> jobs = record.workers.Events();
    detail.workers.reserve(jobs.size());
    for (const WorkerEvent& job : jobs) {
        SpikeWorkerRow row;
        row.name = NameOf(job.name);
        row.startMs = TicksToMilliseconds(job.startTicks >= frameStart ? job.startTicks - frameStart : 0);
        row.durationMs = TicksToMilliseconds(job.endTicks >= job.startTicks ? job.endTicks - job.startTicks : 0);
        row.worker = job.worker;
        row.carried = (job.flags & kWorkerEventCarried) != 0;
        detail.workers.push_back(std::move(row));
    }
    return detail;
}

std::optional<SpikeDetail> BuildSpikeDetail(const SpikeStore& store, std::uint64_t frameIndex)
{
    // A frozen frame is large; keep the copy off the stack.
    const auto record = std::make_unique<FrameRecord>();
    SpikeClassification classification;
    if (!store.CopyFrame(frameIndex, *record, &classification)) {
        return std::nullopt;
    }

    SpikeDetail detail = BuildFrameDetail(*record);
    detail.frameIndex = frameIndex;
    detail.classification = classification;
    detail.cause = NameOf(classification.dominant);

    std::array<SpikeStore::Row, SpikeStore::kSlots> rows{};
    const std::size_t rowCount = store.Rows(rows);
    for (std::size_t i = 0; i < rowCount; ++i) {
        if (rows[i].frameIndex == frameIndex) {
            detail.medianMs = rows[i].medianMs;
            detail.pinned = rows[i].pinned;
            detail.gpuAttached = rows[i].gpuAttached;
            break;
        }
    }
    return detail;
}

} // namespace gte::Profiling
