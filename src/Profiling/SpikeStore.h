#pragma once

// Frames that were much slower than their neighbours, frozen with their full detail so a stall can be
// explained after the fact. Fixed memory: slots are reused, nothing is allocated after construction.

#include "ProfilingBudget.h"
#include "ProfilingTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>

namespace gte::Profiling {

inline constexpr std::size_t kMaxPinnedSpikes = 8;

// A frame is a spike when it is longer than this multiple of the recent median.
inline constexpr double kSpikeMedianFactor = 2.0;
// Frames needed before a median means anything.
inline constexpr std::size_t kSpikeMinHistory = 8;
// A spike is explained when its dominant scope covers this share of the excess time.
inline constexpr double kSpikeExplainedShare = 0.5;
// A GPU report may arrive this many frames after the frame it belongs to.
inline constexpr std::uint64_t kSpikeLateGpuFrames = 2;

enum class SpikeClass : std::uint8_t { Unexplained, Explained };

const char* ToString(SpikeClass kind) noexcept;

// What the classifier found for one frame.
struct SpikeClassification {
    SpikeClass kind = SpikeClass::Unexplained;
    NameId dominant = kInvalidNameId; // scope or wait with the largest self time
    double dominantSelfMs = 0.0;
    double excessMs = 0.0;
    bool markerPresent = false;
};

// True when the frame time is a spike. `userThresholdMs` > 0 replaces the median rule.
bool IsSpikeFrame(double frameMs, double medianMs, double userThresholdMs) noexcept;

// Classification of one finished frame against the recent median.
SpikeClassification ClassifySpike(const FrameRecord& record, double medianMs) noexcept;

// Median of the newest `count` values of `recent` (oldest first). 0 when empty.
double MedianOf(std::span<const double> recent) noexcept;

struct FrozenSpike {
    bool used = false;
    bool pinned = false;
    bool gpuAttached = false; // the GPU numbers came from a late report
    std::uint64_t sequence = 0; // freeze order, larger = newer
    double medianMs = 0.0;
    SpikeClassification classification{};
    FrameRecord record{};
};

enum class PinResult : std::uint8_t { Ok, NotFound, LimitReached };

// Thread safe: the main thread freezes, network readers pin and read. Each call holds the lock only for a copy.
// A store is large (it holds full frames): keep it off the stack.
class SpikeStore {
public:
    static constexpr std::size_t kSlots = kSpikeStoreFrames;

    // Stores a copy of the frame. Evicts the oldest unpinned slot when full. Returns false when every slot is pinned.
    bool Freeze(const FrameRecord& record, double medianMs, const SpikeClassification& classification) noexcept;

    // Adds a late GPU span to a frozen frame at most kSpikeLateGpuFrames behind `newestFrame`. A span that is not
    // Present is refused.
    bool AttachGpuSpan(std::uint64_t frameIndex, const GpuViewMetric& span, std::uint64_t newestFrame) noexcept;

    PinResult Pin(std::uint64_t frameIndex) noexcept;
    bool Unpin(std::uint64_t frameIndex) noexcept;

    std::size_t Count() const noexcept;
    std::size_t PinnedCount() const noexcept;
    std::uint64_t TotalFrozen() const noexcept; // spikes frozen since start, evicted ones included

    // Copies the summary of the slot holding `frameIndex` (without the event arrays).
    struct Row {
        std::uint64_t frameIndex = 0;
        double frameMs = 0.0;
        double medianMs = 0.0;
        bool pinned = false;
        bool gpuAttached = false;
        SpikeClassification classification{};
        GpuFrameMetrics gpu{};
    };

    // Rows newest first. Returns the number written.
    std::size_t Rows(std::span<Row> out) const noexcept;

    // Full copy of one frozen frame, or nothing when it was evicted.
    bool CopyFrame(std::uint64_t frameIndex, FrameRecord& out, SpikeClassification* classification = nullptr) const noexcept;

    void Clear() noexcept;

private:
    int FindLocked(std::uint64_t frameIndex) const noexcept;
    std::size_t PinnedLocked() const noexcept;

    mutable std::mutex m_mutex;
    std::array<FrozenSpike, kSlots> m_slots{};
    std::uint64_t m_sequence = 0;
};

static_assert(sizeof(SpikeStore) <= SpikeStore::kSlots * sizeof(FrozenSpike) + 256, "SpikeStore grew past its slots");

} // namespace gte::Profiling
