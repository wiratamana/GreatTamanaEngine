#include "ProfilerPanelData.h"

#include "../Profiling/Metric.h"
#include "../Profiling/NameTable.h"
#include "../Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"
#include "../Profiling/ProfilerSnapshotBuilder.h"
#include "../Profiling/ProfilingClock.h"
#include "../Profiling/ScopeTree.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace gte {

ProfilerFrameView BuildProfilerFrameView(const Profiling::FrameRecord& frame, bool captureEnabled)
{
    ProfilerFrameView view;
    view.summary = frame.summary;
    view.totals = Profiling::ComputeFrameTotals(frame);
    view.checks = Profiling::EvaluateChecks(frame, view.totals, captureEnabled);
    return view;
}

std::string FormatCheckLine(const Profiling::FrameCheck& check)
{
    const char* tag = check.status == Profiling::CheckStatus::Ok ? "OK"
        : check.status == Profiling::CheckStatus::Warn          ? "WARN"
                                                                : "FAIL";
    char buffer[192];
    if (check.value != 0) {
        std::snprintf(buffer, sizeof(buffer), "[%s] %s (%llu)", tag, check.text,
            static_cast<unsigned long long>(check.value));
    } else {
        std::snprintf(buffer, sizeof(buffer), "[%s] %s", tag, check.text);
    }
    return std::string(buffer);
}

std::string FormatBlockedLine(const Profiling::FrameTotals& totals)
{
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "Blocked %.2f ms (GPU %.2f, workers %.2f, other %.2f)",
        Profiling::TicksToMilliseconds(totals.blockedTicks), Profiling::TicksToMilliseconds(totals.blockedGpuTicks),
        Profiling::TicksToMilliseconds(totals.blockedWorkerTicks),
        Profiling::TicksToMilliseconds(totals.blockedOtherTicks));
    return std::string(buffer);
}

std::string FormatDuration(double milliseconds)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f ms", milliseconds);
    return std::string(buffer);
}

std::string FormatGpuTimingLine(Profiling::SampleStatus status, double milliseconds)
{
    switch (status) {
    case Profiling::SampleStatus::Present:
        return FormatDuration(milliseconds);
    case Profiling::SampleStatus::Stale:
        return "stale";
    case Profiling::SampleStatus::Unsupported:
        return "unsupported";
    case Profiling::SampleStatus::Partial:
        return "partial";
    case Profiling::SampleStatus::Error:
        return "error";
    case Profiling::SampleStatus::Inactive:
        return "inactive";
    case Profiling::SampleStatus::Absent:
        break;
    }
    return "absent";
}

std::string FormatGpuMetric(const Profiling::GpuViewMetric& metric)
{
    if (metric.status != Profiling::SampleStatus::Partial) {
        return FormatGpuTimingLine(metric.status, metric.milliseconds);
    }

    // A partial value is what was measured; it is a lower bound, so it always says so.
    char buffer[96];
    if (metric.foldedPasses != 0) {
        std::snprintf(buffer, sizeof(buffer), "%.2f ms partial (%u passes folded)",
            static_cast<double>(metric.milliseconds), metric.foldedPasses);
    } else if (metric.milliseconds > 0.0f) {
        std::snprintf(buffer, sizeof(buffer), "%.2f ms partial (some passes not measured)",
            static_cast<double>(metric.milliseconds));
    } else {
        return "partial";
    }
    return std::string(buffer);
}

std::string FormatGpuCoverage(const Profiling::GpuFrameMetrics& gpu)
{
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "executed %u of declared %u, recorded %u, disabled by toggle %u",
        gpu.executedPasses, gpu.declaredPasses, gpu.recordedPasses, gpu.disabledByToggle);
    return std::string(buffer);
}

namespace {

struct ViewSum {
    double milliseconds = 0.0;
    std::uint32_t passes = 0;
    std::uint32_t measured = 0;
    std::uint32_t folded = 0;
    std::uint32_t errors = 0;
    std::uint32_t unsupported = 0;
    std::uint32_t inactive = 0;
};

Profiling::GpuViewMetric ToMetric(const ViewSum& sum)
{
    using Profiling::SampleStatus;
    Profiling::GpuViewMetric metric;
    if (sum.passes == 0) {
        metric.status = SampleStatus::Inactive;
        return metric;
    }

    metric.foldedPasses = sum.folded;
    if (sum.measured == sum.passes && sum.folded == 0) {
        metric.status = SampleStatus::Present;
    } else if (sum.measured != 0) {
        metric.status = SampleStatus::Partial;
    } else if (sum.errors == sum.passes) {
        metric.status = SampleStatus::Error;
    } else if (sum.unsupported == sum.passes) {
        metric.status = SampleStatus::Unsupported;
    } else if (sum.inactive == sum.passes) {
        metric.status = SampleStatus::Inactive;
    } else {
        metric.status = SampleStatus::Absent;
    }
    if (metric.status == SampleStatus::Present || metric.status == SampleStatus::Partial) {
        metric.milliseconds = static_cast<float>(sum.milliseconds);
    }
    return metric;
}

} // namespace

RegimeGpuByView SumRegimeGpuByView(const std::vector<rg::RenderGraphPassMetadata>& passes)
{
    ViewSum game;
    ViewSum scene;
    ViewSum shared;
    for (const rg::RenderGraphPassMetadata& pass : passes) {
        if (pass.isCulled) {
            continue;
        }
        ViewSum& sum = pass.viewScope == rg::ViewScope::Primary    ? game
            : pass.viewScope == rg::ViewScope::Secondary ? scene
                                                         : shared;
        ++sum.passes;
        sum.folded += pass.gpuFoldedPasses;
        if (pass.gpuTimingMilliseconds.has_value()) {
            ++sum.measured;
            sum.milliseconds += *pass.gpuTimingMilliseconds;
        } else if (pass.gpuTimingState == GpuReadState::Error) {
            ++sum.errors;
        } else if (pass.gpuTimingState == GpuReadState::Unsupported) {
            ++sum.unsupported;
        } else if (pass.gpuTimingState == GpuReadState::Inactive) {
            ++sum.inactive;
        }
    }

    RegimeGpuByView result;
    result.game = ToMetric(game);
    result.scene = ToMetric(scene);
    result.shared = ToMetric(shared);
    return result;
}

const char* CpuScopeTableEmptyMessage()
{
    if (!kCpuScopeInstrumentationCompiledIn) {
        return "CPU scope instrumentation is compiled out (GTE_ENABLE_PROFILER=OFF) - "
               "no scope data will ever appear here in this build.";
    }
    return "No CPU scopes recorded yet.";
}

ProfilerVerdictLine BuildVerdictLine(const Profiling::FrameProfiler& profiler, const Profiling::ProfilerChecks& checks)
{
    bool checkFails = false;
    for (const Profiling::FrameCheck& check : checks.items) {
        checkFails = checkFails || check.status == Profiling::CheckStatus::Fail;
    }
    const Profiling::Verdict verdict = Profiling::VerdictFromHistory(profiler, profiler.IsCaptureEnabled(), checkFails);

    std::array<char, 192> text{};
    const std::size_t length = Profiling::FormatVerdict(verdict, text);
    return ProfilerVerdictLine{ std::string(text.data(), length), verdict.kind };
}

std::string FormatOverheadLine(const Profiling::OverheadEstimate& overhead)
{
    if (!overhead.known) {
        return "Profiler overhead: not measured";
    }
    const std::string estimate = Profiling::FormatMetric(
        Profiling::PresentMetric(overhead.estimatedMs, Profiling::Unit::Milliseconds, Profiling::MetricKind::Estimated));
    const std::string share = Profiling::FormatMetric(
        Profiling::PresentMetric(overhead.frameShare * 100.0, Profiling::Unit::Percent, Profiling::MetricKind::Estimated));
    std::string line = overhead.warn ? "[WARN] " : "";
    line += "Profiler overhead: estimated " + estimate + " (" + share + " of the frame)";
    if (overhead.warn) {
        line += ", above the " + Profiling::FormatMetric(Profiling::PresentMetric(
                                     Profiling::kOverheadWarnShare * 100.0, Profiling::Unit::Percent))
            + " limit";
    }
    return line;
}

std::vector<SpikeListRow> BuildSpikeListRows(std::span<const Profiling::SpikeStore::Row> spikes)
{
    std::vector<SpikeListRow> rows;
    rows.reserve(spikes.size());
    for (const Profiling::SpikeStore::Row& spike : spikes) {
        SpikeListRow row;
        row.frameIndex = spike.frameIndex;
        row.frameTime = Profiling::FormatMetric(Profiling::PresentMetric(spike.frameMs, Profiling::Unit::Milliseconds));
        row.overMedian = Profiling::FormatMetric(Profiling::PresentMetric(
                             spike.classification.excessMs, Profiling::Unit::Milliseconds, Profiling::MetricKind::Derived))
            + " over the "
            + Profiling::FormatMetric(Profiling::PresentMetric(
                spike.medianMs, Profiling::Unit::Milliseconds, Profiling::MetricKind::Derived))
            + " median";
        row.explained = spike.classification.kind == Profiling::SpikeClass::Explained;
        row.classification = Profiling::ToString(spike.classification.kind);
        if (spike.classification.dominant != Profiling::kInvalidNameId) {
            row.cause = Profiling::ResolveName(spike.classification.dominant);
        } else {
            row.cause = spike.classification.markerPresent ? "marker" : "-";
        }
        row.gpu = FormatGpuMetric(spike.gpu.span);
        row.pinned = spike.pinned;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string FormatSpikeSummary(std::size_t frozen, std::size_t pinned)
{
    const auto count = [](std::size_t value) {
        return Profiling::FormatMetric(Profiling::PresentMetric(static_cast<double>(value), Profiling::Unit::Count));
    };
    return "Spikes: " + count(frozen) + " frozen, " + count(pinned) + " of " + count(Profiling::kMaxPinnedSpikes) + " pinned";
}

} // namespace gte
