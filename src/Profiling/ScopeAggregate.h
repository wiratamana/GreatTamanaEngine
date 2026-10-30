#pragma once

#include "EventBuffer.h"
#include "ProfilingBudget.h"
#include "ScopeEvent.h"
#include "WorkerRing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace gte::Profiling {

// Where a row was measured.
enum class ScopeLane : std::uint8_t { Main, Workers, Gpu };
inline constexpr std::size_t kScopeLaneCount = 3;

// One executed GPU pass of one frame, as the render graph reports it.
struct GpuPassRow {
    NameId pass = kInvalidNameId;
    NameId feature = kInvalidNameId;
    float barrierMs = 0.0f;
    float workMs = 0.0f;
    std::uint32_t drawCalls = 0;
    std::uint32_t foldedPasses = 0; // > 0: the row stands for several folded passes
    ViewSlot view = ViewSlot::Shared;
    SampleStatus status = SampleStatus::Absent;
};

// One name in one lane, summed over its calls in one frame. Ticks are ns.
struct ScopeAggregate {
    std::uint64_t totalTicks = 0; // inclusive time
    std::uint64_t selfTicks = 0;  // total minus direct children
    std::uint64_t maxTicks = 0;   // longest single call
    NameId name = kInvalidNameId;
    NameId feature = kInvalidNameId; // GPU rows: owning feature
    std::uint32_t calls = 0;
    std::uint16_t minDepth = 0; // Main lane: shallowest depth seen
    ScopeLane lane = ScopeLane::Main;
    ScopeKind kind = ScopeKind::Work;
    SampleStatus status = SampleStatus::Present;
};
static_assert(std::is_trivially_copyable_v<ScopeAggregate>);
static_assert(sizeof(ScopeAggregate) <= 48);

// Fixed array of rows. Rows that do not fit are counted.
class ScopeAggregateBuffer {
public:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    // Returns the row index, or kNone when full.
    std::size_t Add(const ScopeAggregate& row) noexcept
    {
        if (m_count >= m_rows.size()) {
            ++m_dropped;
            return kNone;
        }
        m_rows[m_count] = row;
        return m_count++;
    }

    ScopeAggregate& At(std::size_t index) noexcept { return m_rows[index]; }
    std::span<const ScopeAggregate> Rows() const noexcept { return { m_rows.data(), m_count }; }
    std::size_t Size() const noexcept { return m_count; }
    std::uint32_t Dropped() const noexcept { return m_dropped; }

    void Clear() noexcept
    {
        m_count = 0;
        m_dropped = 0;
    }

private:
    std::array<ScopeAggregate, kMaxScopeAggregatesPerFrame> m_rows{};
    std::size_t m_count = 0;
    std::uint32_t m_dropped = 0;
};

using GpuPassRowBuffer = EventBuffer<GpuPassRow, kMaxGpuPassRowsPerFrame>;

// Workers lane: one row per name; self equals total because job scopes do not nest.
void AppendWorkerAggregates(std::span<const WorkerEvent> events, ScopeAggregateBuffer& out) noexcept;

// Gpu lane: one row per pass name. Only Present and Partial rows add time.
void AppendGpuAggregates(std::span<const GpuPassRow> rows, ScopeAggregateBuffer& out) noexcept;

} // namespace gte::Profiling
