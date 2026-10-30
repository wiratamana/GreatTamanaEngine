#pragma once

#include "../Profiling/ScopeAggregate.h"
#include "../Profiling/ScopeEvent.h"
#include "../Profiling/ScopeSource.h"
#include "../Profiling/Verdict.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gte {

enum class ScopeCategory : std::uint8_t {
    Simulation,
    Input,
    RenderPrepare,
    RenderRecord,
    RenderFeature,
    GpuWork,
    Waiting,
    EditorUi,
    LoadingAndTools,
    ProjectCode,
    Other,
};
inline constexpr std::size_t kScopeCategoryCount = 11;

// Short plain label of a category.
const char* CategoryLabel(ScopeCategory category);

// Plain-English text of one profiler row.
struct ScopeInfo {
    std::string title;       // 2-6 plain words
    std::string explanation; // 1-2 plain sentences
    std::string hint;        // advice when the row is big; may be empty
    ScopeCategory category = ScopeCategory::Other;
    bool waiting = false;    // pure wait: the CPU is idle
};

// Describes a raw scope name. Exact name first, then prefix rule, then origin fallback. Never throws.
ScopeInfo DescribeScope(std::string_view name, Profiling::ScopeLane lane, Profiling::ScopeOrigin origin);

// Describes a GPU feature row (the sum of its passes).
ScopeInfo DescribeGpuFeature(std::string_view feature);

// True when DescribeScope found no real description and returned the generic origin text.
bool IsFallbackInfo(const ScopeInfo& info);

// Plain advice for a verdict kind. Never empty.
const char* DescribeVerdict(Profiling::VerdictKind kind);

struct PlainSummaryInput {
    double frameMs = 0.0;
    double cpuBusyMs = 0.0;
    double cpuWaitMs = 0.0;
    double gpuMs = 0.0;
    Profiling::SampleStatus gpuStatus = Profiling::SampleStatus::Absent;
};

// One short paragraph: frame time, CPU busy and wait, GPU time.
std::string BuildPlainSummary(const PlainSummaryInput& input);

} // namespace gte
