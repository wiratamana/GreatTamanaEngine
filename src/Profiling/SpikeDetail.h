#pragma once

// Readable copy of one frozen frame: its scope tree in time order, for the HTTP reader.

#include "SpikeStore.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte::Profiling {

struct SpikeScopeRow {
    std::string name;
    double startMs = 0.0; // from the start of the frame
    double durationMs = 0.0;
    double selfMs = 0.0;
    std::uint16_t depth = 0;
    ScopeKind kind = ScopeKind::Work;
};

// One job scope that ran on a worker thread during the frame.
struct SpikeWorkerRow {
    std::string name;
    double startMs = 0.0; // from the start of the frame; 0 when the job began earlier
    double durationMs = 0.0;
    std::uint16_t worker = 0;
    bool carried = false;
};

struct SpikeDetail {
    std::uint64_t frameIndex = 0;
    double frameMs = 0.0;
    double medianMs = 0.0;
    bool pinned = false;
    bool gpuAttached = false;
    SpikeClassification classification{};
    std::string cause; // name of the dominant scope
    GpuFrameMetrics gpu{};
    std::uint32_t droppedScopes = 0;
    std::vector<SpikeScopeRow> scopes; // time order, parents before children
    std::vector<SpikeWorkerRow> workers;
};

// Scope tree of any finished frame, in time order. Classification and pin state stay at their defaults.
SpikeDetail BuildFrameDetail(const FrameRecord& record);

// Nothing when the frame was evicted or never frozen.
std::optional<SpikeDetail> BuildSpikeDetail(const SpikeStore& store, std::uint64_t frameIndex);

} // namespace gte::Profiling
