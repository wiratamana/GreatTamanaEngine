#pragma once

// Immutable copy of the profiler state that other threads may read. Plain data, no pointers into live state.

#include "FrameChecks.h"
#include "LiveBlockTable.h"
#include "Metric.h"
#include "ProfilingTypes.h"
#include "Verdict.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gte::Profiling {

inline constexpr std::uint32_t kProfilerSchemaVersion = 1;

inline constexpr std::size_t kSnapshotNameBytes = 64;
inline constexpr std::size_t kSnapshotScopeRows = 32;
inline constexpr std::size_t kSnapshotNestedRows = 64;
inline constexpr std::size_t kSnapshotFrameRows = 120;
inline constexpr std::size_t kSnapshotSpikeRows = 16;
inline constexpr std::size_t kSnapshotSiteBytes = 24;
inline constexpr std::size_t kSnapshotDetailBytes = 48;
inline constexpr std::size_t kSnapshotVerdictBytes = 192;

// One top-level CPU scope, summed over its calls in the frame.
struct SnapshotScopeRow {
    std::array<char, kSnapshotNameBytes> name{};
    Metric total{};
    Metric self{};
    Metric calls{};
};

// One name at one nested depth (1 or deeper), summed over its calls in the frame.
struct SnapshotNestedRow {
    std::array<char, kSnapshotNameBytes> name{};
    std::uint32_t depth = 0;
    Metric total{};
    Metric self{};
    Metric calls{};
};

// One finished frame of the history.
struct SnapshotFrameRow {
    std::uint64_t frameIndex = 0;
    Metric cpu{};
    Metric gpuSpan{};
    Metric gpuPrimary{};
    Metric gpuSecondary{};
};

struct SnapshotLiveBlock {
    std::uint32_t threadSlot = 0;
    std::array<char, kSnapshotSiteBytes> site{};
    std::array<char, kSnapshotDetailBytes> detail{};
    Metric elapsed{};
};

// A frame frozen because it was much slower than its neighbours.
struct SnapshotSpikeRow {
    std::uint64_t frameIndex = 0;
    Metric cpu{};    // frame time
    Metric median{}; // recent median it was compared with
    Metric excess{}; // cpu - median
    Metric dominantSelf{}; // self time of the dominant scope
    Metric gpuSpan{};
    std::array<char, 24> classification{}; // "explained" or "unexplained"
    std::array<char, kSnapshotNameBytes> cause{}; // dominant scope name
    bool markerPresent = false;
    bool pinned = false;
    bool gpuAttached = false; // GPU numbers came from a late report
};

// The numbers the verdict was decided from, medians over the verdict window.
struct SnapshotVerdictDetail {
    VerdictKind kind = VerdictKind::Unknown;
    Metric frames{}; // frames the medians cover
    Metric frame{};
    Metric cpuWork{};
    Metric gpuWait{};
    Metric presentWait{};
    Metric otherWait{};
    Metric untracked{};
    Metric stallShare{}; // otherWait share of the frame, percent
    std::array<char, kSnapshotNameBytes> stallSite{};
};

// Estimated cost of the profiler itself.
struct SnapshotOverhead {
    Metric costPerEvent{}; // ns per recorded scope, measured once
    Metric estimated{};    // ms per frame
    Metric share{};        // percent of the frame
    bool warn = false;     // above the warning share
};

struct SnapshotCheck {
    CheckId id = CheckId::CaptureOn;
    CheckStatus status = CheckStatus::Ok;
    Metric count{};
    const char* text = ""; // static text
};

struct SnapshotDrops {
    Metric lateScopes{};
    Metric foreignScopes{};
    Metric lostWorkerEvents{};
    Metric droppedScopes{};
    Metric droppedGpuFrames{};
};

struct SnapshotDeviceState {
    bool deviceLost = false;
    bool presentUncapped = false; // started without a vsync cap
    Metric consecutiveFrameTimeouts{};
    Metric abandonedObjects{};
};

struct SnapshotGpuCoverage {
    Metric declared{};
    Metric executed{};
    Metric recorded{};
    Metric disabledByToggle{};
};

// Whether GPU bars can be placed on the CPU time axis. When not, `reason` says why.
struct SnapshotTimeline {
    bool aligned = false;
    std::array<char, kSnapshotNameBytes> reason{};
};

struct ProfilerSnapshot {
    std::uint32_t schemaVersion = kProfilerSchemaVersion;
    std::uint64_t frameIndex = 0;
    bool hasFrame = false; // false until one frame was recorded
    bool captureOn = true;
    bool displayPaused = false;

    Metric snapshotAge{}; // filled by the reader; the publisher cannot know it

    Metric cpuFrame{};
    Metric untracked{};
    Metric blockedGpu{};
    Metric blockedWorkers{};
    Metric blockedOther{};

    Metric gpuSpan{};
    std::array<Metric, kViewSlotCount> gpuView{};
    std::array<Metric, kViewSlotCount> indirectDraws{};
    SnapshotGpuCoverage gpuCoverage{};
    Metric memoryTotal{};

    std::array<SnapshotCheck, kCheckCount> checks{};
    SnapshotDrops drops{};
    SnapshotDeviceState device{};

    std::array<SnapshotLiveBlock, kMaxLiveBlockThreads> liveBlocks{};
    std::uint32_t liveBlockCount = 0;

    std::array<char, kSnapshotVerdictBytes> verdict{}; // one line, numbers included
    SnapshotVerdictDetail verdictDetail{};
    SnapshotOverhead overhead{};
    SnapshotTimeline timeline{};

    std::array<SnapshotScopeRow, kSnapshotScopeRows> scopes{};
    std::uint32_t scopeCount = 0;
    std::uint32_t scopeRowsDropped = 0; // distinct top-level scopes that did not fit

    std::array<SnapshotNestedRow, kSnapshotNestedRows> nested{}; // largest total first
    std::uint32_t nestedCount = 0;
    std::uint32_t nestedRowsDropped = 0; // distinct (name, depth) pairs that did not fit

    std::array<SnapshotFrameRow, kSnapshotFrameRows> frames{}; // oldest first
    std::uint32_t frameCount = 0;

    bool spikesAvailable = false;
    std::array<SnapshotSpikeRow, kSnapshotSpikeRows> spikes{}; // newest first
    std::uint32_t spikeCount = 0;
    std::uint32_t spikePinned = 0;
    std::uint64_t spikeTotal = 0; // frozen since start, evicted ones included
};

} // namespace gte::Profiling
