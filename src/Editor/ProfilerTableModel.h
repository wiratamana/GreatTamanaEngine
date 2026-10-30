#pragma once

// Pure model of the profiler table: rows with plain text, milliseconds and percent. No UI types.

#include "../Application/ProfilerScopeCatalog.h"
#include "../Profiling/ProfilingTypes.h"
#include "../Profiling/ScopeSource.h"
#include "../Profiling/ScopeTable.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gte {

inline constexpr std::uint32_t kAllCategoriesMask = (1u << kScopeCategoryCount) - 1u;

constexpr std::uint32_t CategoryBit(ScopeCategory category) noexcept
{
    return 1u << static_cast<unsigned>(category);
}

struct TableRowView {
    Profiling::ScopeTableRow row;
    std::string title;
    std::string explanation;
    std::string hint;
    std::string rawName;
    std::string sourceText;   // "File.cpp:123"; empty when the site is unknown
    std::string featureTitle; // GPU rows: title of the owning feature; empty otherwise
    ScopeCategory category = ScopeCategory::Other;
    Profiling::ScopeOrigin origin = Profiling::ScopeOrigin::Unknown;
    bool waiting = false;     // pure wait: the CPU is idle
    bool isGpuFeature = false; // tree mode: sum row of one render feature
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
    std::uint32_t rowsDropped = 0;
    bool treeTruncated = false;
};

// Builds the visible rows of the newest frame. maxRows caps the rows after filtering.
TableModel BuildTableModel(const Profiling::FrameRecord& frame, const TableOptions& options, std::size_t maxRows);

// One "what to look at" line.
struct TopThing {
    std::string title;
    std::string hint;
    double ms = 0.0;
    double percent = 0.0;
    bool gpu = false;
};

// Up to 3 CPU rows by self time and 3 GPU rows by time, each at least 5% of its base.
std::vector<TopThing> BuildTopThings(const Profiling::FrameRecord& frame);

// Numbers for BuildPlainSummary taken from one frame summary.
PlainSummaryInput MakePlainSummaryInput(const Profiling::FrameSummary& summary);

} // namespace gte
