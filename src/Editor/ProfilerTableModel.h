#pragma once

// Pure model of the profiler table: rows with plain text, milliseconds and percent. No UI types.

#include "../Application/ProfilerScopeCatalog.h"
#include "../Profiling/ProfilingTypes.h"
#include "../Profiling/ScopeSource.h"
#include "../Profiling/ScopeTable.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gte {

inline constexpr std::uint32_t kAllCategoriesMask = (1u << kScopeCategoryCount) - 1u;

constexpr std::uint32_t CategoryBit(ScopeCategory category) noexcept
{
    return 1u << static_cast<unsigned>(category);
}

// All text of one scope name. Never changes after it is built.
struct ScopeText {
    ScopeInfo info;
    std::string rawName;
    std::string sourceText;  // "File.cpp:123"; empty when the site is unknown
    std::string lowerTitle;  // for the case-insensitive filter
    std::string lowerRaw;
    Profiling::ScopeOrigin origin = Profiling::ScopeOrigin::Unknown;
};

// Builds the text of each (lane, name) once. Entries keep their address for the life of the cache.
class ScopeTextCache {
public:
    // `gpuFeature` selects the text of a render feature sum row instead of a pass row.
    const ScopeText& Get(Profiling::ScopeLane lane, Profiling::NameId name, bool gpuFeature = false);

private:
    std::vector<std::unique_ptr<ScopeText>> m_slots; // sized on first use
};

struct TableRowView {
    Profiling::ScopeTableRow row;
    const ScopeText* text = nullptr;    // owned by the cache; never null in a built model
    const ScopeText* feature = nullptr; // GPU pass rows: text of the owning feature
    bool waiting = false;               // pure wait: the CPU is idle
    bool isGpuFeature = false;          // tree mode: sum row of one render feature
    double totalMs = 0.0;
    double selfMs = 0.0;
    double maxMs = 0.0;
    double percentOfBase = 0.0;
    bool percentKnown = false;
};

struct TableOptions {
    bool tree = true;
    Profiling::ScopeSort sort = Profiling::ScopeSort::TotalDesc;
    std::uint32_t categoryMask = kAllCategoriesMask;
    std::string filter;                     // case-insensitive substring of title or raw name
    std::vector<std::uint64_t> collapsed;   // pathKeys whose descendants are hidden (tree mode)
    bool showTechnicalNames = false;        // read by the panel, not by the model
};

struct TableModel {
    std::vector<TableRowView> rows; // lanes Main, Workers, Gpu; pre-order in tree mode
    std::array<double, Profiling::kScopeLaneCount> laneTotalMs{};
    double cpuBaseMs = 0.0;  // frame time: base of Main and Workers percent
    double gpuBaseMs = 0.0;  // GPU span: base of Gpu percent
    bool gpuBaseKnown = false;
    std::uint32_t rowsDropped = 0; // rows cut by the lane caps, after filtering
    bool treeTruncated = false;
};

// Refills `out` with the visible rows of `frame` (capacity is reused). `caps` limits the rows of each lane after filtering.
void BuildTableModel(const Profiling::FrameRecord& frame, const TableOptions& options, const Profiling::LaneRowCaps& caps,
    ScopeTextCache& cache, TableModel& out);

// One "what to look at" line.
struct TopThing {
    const ScopeText* text = nullptr; // owned by the cache
    double ms = 0.0;
    double percent = 0.0;
    bool gpu = false;
};

// Refills `out` with up to 3 CPU rows by self time and 3 GPU rows by time, each at least 5% of its base.
void BuildTopThings(const Profiling::FrameRecord& frame, ScopeTextCache& cache, std::vector<TopThing>& out);

// Numbers for BuildPlainSummary taken from one frame summary.
PlainSummaryInput MakePlainSummaryInput(const Profiling::FrameSummary& summary);

} // namespace gte
