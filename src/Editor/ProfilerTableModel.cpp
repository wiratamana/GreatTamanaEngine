#include "ProfilerTableModel.h"

#include "../Profiling/FrameGraphData.h"
#include "../Profiling/NameTable.h"
#include "../Profiling/ProfilingBudget.h"
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
constexpr std::size_t kNameSlots = Profiling::kMaxNames + 1;

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

std::unique_ptr<ScopeText> MakeText(ScopeLane lane, Profiling::NameId name, bool gpuFeature)
{
    auto text = std::make_unique<ScopeText>();
    text->rawName = Profiling::ResolveName(name);
    text->origin = Profiling::SourceOf(name).origin;
    text->sourceText = SourceText(name);
    text->info = gpuFeature ? DescribeGpuFeature(text->rawName) : DescribeScope(text->rawName, lane, text->origin);
    text->lowerTitle = Lower(text->info.title);
    text->lowerRaw = Lower(text->rawName);
    return text;
}

TableRowView MakeView(const ScopeTableRow& row, bool tree, const Bases& bases, ScopeTextCache& cache)
{
    TableRowView view;
    view.row = row;
    view.isGpuFeature = tree && row.lane == ScopeLane::Gpu && row.depth == 0 && row.hasChildren
        && row.feature != Profiling::kInvalidNameId && row.name == row.feature;
    view.text = &cache.Get(row.lane, row.name, view.isGpuFeature);
    view.waiting = view.text->info.waiting || row.kind == Profiling::ScopeKind::Blocked;
    if (row.lane == ScopeLane::Gpu && row.feature != Profiling::kInvalidNameId && !view.isGpuFeature) {
        view.feature = &cache.Get(ScopeLane::Gpu, row.feature, true);
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
    if ((options.categoryMask & CategoryBit(view.text->info.category)) == 0) {
        return false;
    }
    if (lowerFilter.empty()) {
        return true;
    }
    return view.text->lowerTitle.find(lowerFilter) != std::string::npos
        || view.text->lowerRaw.find(lowerFilter) != std::string::npos;
}

bool IsCollapsed(const std::vector<std::uint64_t>& collapsed, std::uint64_t pathKey) noexcept
{
    return std::find(collapsed.begin(), collapsed.end(), pathKey) != collapsed.end();
}

} // namespace

const ScopeText& ScopeTextCache::Get(ScopeLane lane, Profiling::NameId name, bool gpuFeature)
{
    if (m_slots.empty()) {
        m_slots.resize(Profiling::kScopeLaneCount * 2 * kNameSlots);
    }
    // A name past the table size can only be the shared overflow name.
    const std::size_t id = name < kNameSlots ? name : Profiling::kOverflowNameId;
    const std::size_t laneIndex = std::min(static_cast<std::size_t>(lane), Profiling::kScopeLaneCount - 1);
    std::unique_ptr<ScopeText>& slot = m_slots[(laneIndex * 2 + (gpuFeature ? 1 : 0)) * kNameSlots + id];
    if (!slot) {
        slot = MakeText(lane, static_cast<Profiling::NameId>(id), gpuFeature);
    }
    return *slot;
}

void BuildTableModel(const Profiling::FrameRecord& frame, const TableOptions& options, const Profiling::LaneRowCaps& caps,
    ScopeTextCache& cache, TableModel& model)
{
    model.rows.clear();
    model.laneTotalMs = {};
    model.rowsDropped = 0;

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
    constexpr Profiling::LaneRowCaps kUncapped{ kNoCap, kNoCap, kNoCap };
    const Profiling::ScopeTable table = options.tree ? Profiling::BuildScopeTree(frame, options.sort, kUncapped)
                                                     : Profiling::BuildScopeFlat(frame, options.sort, kUncapped);
    model.treeTruncated = table.treeTruncated;
    model.rowsDropped = table.TotalDropped();

    // Every row gets a view first; filtering then compacts the same vector.
    std::vector<TableRowView>& views = model.rows;
    const std::size_t count = table.rows.size();
    views.reserve(count);
    for (const ScopeTableRow& row : table.rows) {
        views.push_back(MakeView(row, options.tree, bases, cache));
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

    // Visible rows are decided on the intact vector; the compaction below overwrites parents.
    std::vector<char> hidden(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t p = parent[i];
        if (p != kNoParent && (hidden[p] != 0 || IsCollapsed(options.collapsed, views[p].row.pathKey))) {
            hidden[i] = 1;
        }
    }

    std::array<std::size_t, Profiling::kScopeLaneCount> laneRows{};
    std::size_t written = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (keep[i] == 0 || hidden[i] != 0) {
            continue;
        }
        const std::size_t lane = std::min(static_cast<std::size_t>(views[i].row.lane), Profiling::kScopeLaneCount - 1);
        if (laneRows[lane] >= caps[lane]) {
            ++model.rowsDropped;
            continue;
        }
        ++laneRows[lane];
        if (written != i) {
            views[written] = std::move(views[i]);
        }
        ++written;
    }
    views.resize(written);
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
