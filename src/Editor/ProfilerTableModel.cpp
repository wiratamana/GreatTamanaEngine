#include "ProfilerTableModel.h"

#include "../Profiling/FrameGraphData.h"
#include "../Profiling/NameTable.h"
#include "../Profiling/ProfilingClock.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>

namespace gte {
namespace {

using Profiling::ScopeLane;
using Profiling::ScopeTableRow;

constexpr std::uint32_t kNoParent = 0xFFFFFFFFu;
constexpr double kTopThingMinPercent = 5.0;
constexpr std::size_t kTopThingsPerLane = 3;

std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool Timed(Profiling::SampleStatus status) noexcept
{
    return status == Profiling::SampleStatus::Present || status == Profiling::SampleStatus::Partial;
}

struct Bases {
    double cpuMs = 0.0;
    double gpuMs = 0.0;
    bool gpuKnown = false;
};

Bases BasesOf(const Profiling::FrameSummary& summary) noexcept
{
    Bases b;
    b.cpuMs = summary.cpuMilliseconds;
    b.gpuKnown = summary.gpu.span.status == Profiling::SampleStatus::Present && summary.gpu.span.milliseconds > 0.0f;
    b.gpuMs = b.gpuKnown ? static_cast<double>(summary.gpu.span.milliseconds) : 0.0;
    return b;
}

std::string SourceText(Profiling::NameId name)
{
    const Profiling::ScopeSource source = Profiling::SourceOf(name);
    if (source.fileName == Profiling::kInvalidNameId) {
        return {};
    }
    return std::string(Profiling::ResolveName(source.fileName)) + ":" + std::to_string(source.line);
}

TableRowView MakeView(const ScopeTableRow& row, bool tree, const Bases& bases)
{
    TableRowView view;
    view.row = row;
    view.rawName = Profiling::ResolveName(row.name);
    view.origin = Profiling::SourceOf(row.name).origin;
    view.sourceText = SourceText(row.name);
    view.isGpuFeature = tree && row.lane == ScopeLane::Gpu && row.depth == 0 && row.hasChildren
        && row.feature != Profiling::kInvalidNameId && row.name == row.feature;

    const ScopeInfo info = view.isGpuFeature ? DescribeGpuFeature(view.rawName)
                                             : DescribeScope(view.rawName, row.lane, view.origin);
    view.title = info.title;
    view.explanation = info.explanation;
    view.hint = info.hint;
    view.category = info.category;
    view.waiting = info.waiting || row.kind == Profiling::ScopeKind::Blocked;

    if (row.lane == ScopeLane::Gpu && row.feature != Profiling::kInvalidNameId && !view.isGpuFeature) {
        view.featureTitle = DescribeGpuFeature(Profiling::ResolveName(row.feature)).title;
    }

    view.totalMs = Profiling::TicksToMilliseconds(row.totalTicks);
    view.selfMs = Profiling::TicksToMilliseconds(row.selfTicks);
    view.maxMs = Profiling::TicksToMilliseconds(row.maxTicks);

    if (row.lane == ScopeLane::Gpu) {
        view.percentKnown = bases.gpuKnown && Timed(row.status);
        view.percentOfBase = view.percentKnown ? view.totalMs / bases.gpuMs * 100.0 : 0.0;
    } else {
        view.percentKnown = bases.cpuMs > 0.0;
        view.percentOfBase = view.percentKnown ? view.totalMs / bases.cpuMs * 100.0 : 0.0;
    }
    return view;
}

bool Matches(const TableRowView& view, const TableOptions& options, const std::string& lowerFilter)
{
    if ((options.categoryMask & CategoryBit(view.category)) == 0) {
        return false;
    }
    if (lowerFilter.empty()) {
        return true;
    }
    return Lower(view.title).find(lowerFilter) != std::string::npos
        || Lower(view.rawName).find(lowerFilter) != std::string::npos;
}

bool IsCollapsed(const std::vector<std::uint64_t>& collapsed, std::uint64_t pathKey) noexcept
{
    return std::find(collapsed.begin(), collapsed.end(), pathKey) != collapsed.end();
}

} // namespace

TableModel BuildTableModel(const Profiling::FrameRecord& frame, const TableOptions& options, std::size_t maxRows)
{
    TableModel model;
    const Bases bases = BasesOf(frame.summary);
    model.cpuBaseMs = bases.cpuMs;
    model.gpuBaseMs = bases.gpuMs;
    model.gpuBaseKnown = bases.gpuKnown;

    using Profiling::TicksToMilliseconds;
    model.laneTotalMs[static_cast<std::size_t>(ScopeLane::Main)] = TicksToMilliseconds(frame.summary.topLevelTicks);
    for (const Profiling::ScopeAggregate& agg : frame.aggregates.Rows()) {
        if (agg.lane == ScopeLane::Workers) {
            model.laneTotalMs[static_cast<std::size_t>(ScopeLane::Workers)] += TicksToMilliseconds(agg.totalTicks);
        } else if (agg.lane == ScopeLane::Gpu && Timed(agg.status)) {
            model.laneTotalMs[static_cast<std::size_t>(ScopeLane::Gpu)] += TicksToMilliseconds(agg.totalTicks);
        }
    }

    constexpr std::size_t kNoCap = std::numeric_limits<std::size_t>::max();
    const Profiling::ScopeTable table = options.tree ? Profiling::BuildScopeTree(frame, options.sort, kNoCap)
                                                     : Profiling::BuildScopeFlat(frame, options.sort, kNoCap);
    model.treeTruncated = table.treeTruncated;

    const std::size_t count = table.rows.size();
    std::vector<TableRowView> views;
    views.reserve(count);
    for (const ScopeTableRow& row : table.rows) {
        views.push_back(MakeView(row, options.tree, bases));
    }

    // Parent of each row: the nearest earlier row of the same lane with a smaller depth.
    std::vector<std::uint32_t> parent(count, kNoParent);
    std::vector<std::uint32_t> stack;
    for (std::size_t i = 0; i < count; ++i) {
        const ScopeTableRow& row = views[i].row;
        while (!stack.empty() && (views[stack.back()].row.lane != row.lane || views[stack.back()].row.depth >= row.depth)) {
            stack.pop_back();
        }
        parent[i] = stack.empty() ? kNoParent : stack.back();
        stack.push_back(static_cast<std::uint32_t>(i));
    }

    const std::string lowerFilter = Lower(options.filter);
    std::vector<char> keep(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        keep[i] = Matches(views[i], options, lowerFilter) ? 1 : 0;
    }
    for (std::size_t i = count; i-- > 0;) {
        if (keep[i] != 0 && parent[i] != kNoParent) {
            keep[parent[i]] = 1;
        }
    }

    std::vector<char> hidden(count, 0);
    model.rows.reserve(std::min(count, maxRows));
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t p = parent[i];
        if (p != kNoParent && (hidden[p] != 0 || IsCollapsed(options.collapsed, views[p].row.pathKey))) {
            hidden[i] = 1;
        }
        if (keep[i] == 0 || hidden[i] != 0) {
            continue;
        }
        if (model.rows.size() < maxRows) {
            model.rows.push_back(std::move(views[i]));
        } else {
            ++model.rowsDropped;
        }
    }
    return model;
}

namespace {

struct Candidate {
    ScopeInfo info;
    double ms = 0.0;
};

void AppendTop(std::vector<Candidate>& list, double base, bool gpu, std::vector<TopThing>& out)
{
    std::stable_sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return a.ms > b.ms; });
    for (std::size_t i = 0; i < list.size() && i < kTopThingsPerLane; ++i) {
        out.push_back({ list[i].info.title, list[i].info.hint, list[i].ms, list[i].ms / base * 100.0, gpu });
    }
}

} // namespace

std::vector<TopThing> BuildTopThings(const Profiling::FrameRecord& frame)
{
    std::vector<TopThing> out;
    const Bases bases = BasesOf(frame.summary);

    std::vector<Candidate> cpu;
    std::vector<Candidate> gpu;
    for (const Profiling::ScopeAggregate& agg : frame.aggregates.Rows()) {
        if (agg.kind == Profiling::ScopeKind::Marker) {
            continue;
        }
        if (agg.lane == ScopeLane::Main && agg.kind == Profiling::ScopeKind::Work && bases.cpuMs > 0.0) {
            const double ms = Profiling::TicksToMilliseconds(agg.selfTicks);
            if (ms / bases.cpuMs * 100.0 < kTopThingMinPercent) {
                continue;
            }
            ScopeInfo info = DescribeScope(Profiling::ResolveName(agg.name), agg.lane, Profiling::SourceOf(agg.name).origin);
            if (!info.waiting) {
                cpu.push_back({ std::move(info), ms });
            }
        } else if (agg.lane == ScopeLane::Gpu && bases.gpuKnown && Timed(agg.status)) {
            const double ms = Profiling::TicksToMilliseconds(agg.totalTicks);
            if (ms / bases.gpuMs * 100.0 < kTopThingMinPercent) {
                continue;
            }
            gpu.push_back({ DescribeScope(Profiling::ResolveName(agg.name), agg.lane, Profiling::ScopeOrigin::Unknown), ms });
        }
    }

    if (bases.cpuMs > 0.0) {
        AppendTop(cpu, bases.cpuMs, false, out);
    }
    if (bases.gpuKnown) {
        AppendTop(gpu, bases.gpuMs, true, out);
    }
    return out;
}

PlainSummaryInput MakePlainSummaryInput(const Profiling::FrameSummary& summary)
{
    PlainSummaryInput in;
    in.frameMs = summary.cpuMilliseconds;
    in.cpuBusyMs = Profiling::CpuBusyMilliseconds(summary);
    in.cpuWaitMs = std::max(0.0, in.frameMs - in.cpuBusyMs);
    in.gpuMs = static_cast<double>(summary.gpu.span.milliseconds);
    in.gpuStatus = summary.gpu.span.status;
    return in;
}

} // namespace gte
