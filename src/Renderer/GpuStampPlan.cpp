#include "GpuStampPlan.h"

#include <algorithm>

namespace gte {
namespace {

struct Delta {
    bool known = false; // Both stamps were available.
    bool bad = false;   // The delta wrapped backwards or is implausibly large.
    double ms = 0.0;
};

class StampReader {
public:
    explicit StampReader(const GpuReadInput& input) noexcept : m_input(input) {}

    Delta Between(std::int32_t from, std::int32_t to) const noexcept
    {
        if (!Has(from) || !Has(to)) {
            return {};
        }
        const double ms = ConvertTimestampDeltaToMilliseconds(m_input.values[static_cast<std::size_t>(from)],
            m_input.values[static_cast<std::size_t>(to)], m_input.capability.timestampPeriodNs,
            m_input.capability.validBits);
        if (!(ms <= kMaxPlausibleGpuMs)) {
            return { true, true, 0.0 };
        }
        return { true, false, ms };
    }

private:
    bool Has(std::int32_t query) const noexcept
    {
        if (query < 0) {
            return false;
        }
        const auto index = static_cast<std::size_t>(query);
        return index < m_input.values.size() && index < m_input.available.size() && m_input.available[index] != 0;
    }

    const GpuReadInput& m_input;
};

// A record whose chain is still open: its work ends where the next recorded pass (or E) starts.
struct OpenRecord {
    bool active = false;
    std::uint32_t record = 0;
    std::int32_t workFrom = -1;
    Delta barrier;
};

void FinishRecord(GpuPassRecord& record, const Delta& barrier, const Delta& work, GpuFrameReport& report) noexcept
{
    if (barrier.bad || work.bad) {
        record.state = GpuReadState::Error;
        record.flags |= kGpuRecordStampOrder;
        ++report.errorRecords;
        return;
    }
    record.barrierMs = barrier.known ? static_cast<float>(barrier.ms) : 0.0f;
    record.workMs = work.known ? static_cast<float>(work.ms) : 0.0f;
    report.summedMs += (barrier.known ? barrier.ms : 0.0) + (work.known ? work.ms : 0.0);
    if (barrier.known && work.known) {
        record.state = GpuReadState::Present;
    } else if (barrier.known || work.known) {
        record.state = GpuReadState::Partial;
        ++report.partialRecords;
    } else {
        record.state = GpuReadState::Absent;
        ++report.absentRecords;
    }
}

void SetAllStates(std::span<GpuPassRecord> records, std::uint32_t count, GpuReadState state) noexcept
{
    for (std::uint32_t i = 0; i < count; ++i) {
        records[i].state = state;
    }
}

} // namespace

GpuStampPlan PlanGpuStamps(std::span<const GpuStampPassInput> passes, std::span<GpuStampAssignment> assignments,
    GpuStampMode mode, std::uint32_t recordBudget) noexcept
{
    GpuStampPlan plan;
    plan.mode = mode;
    plan.queryCount = GpuQueryCount(recordBudget);
    plan.endQuery = static_cast<std::int32_t>(2 * (recordBudget + 1));
    plan.finalQuery = plan.endQuery + 1;

    const std::size_t count = std::min(passes.size(), assignments.size());
    plan.passCount = static_cast<std::uint32_t>(count);
    plan.inputTruncated = passes.size() > assignments.size();

    const bool fine = mode == GpuStampMode::Fine;
    std::uint32_t stamped = 0;
    std::uint32_t bucketRecord = 0;
    for (std::size_t i = 0; i < count; ++i) {
        GpuStampAssignment& slot = assignments[i];
        slot = {};
        if (passes[i].skipped) {
            slot.record = plan.recordCount++;
            slot.flags = kGpuRecordSkipped;
            slot.leader = true;
            continue;
        }
        if (stamped < recordBudget) {
            slot.startQuery = static_cast<std::int32_t>(2 * stamped);
            slot.workQuery = fine ? slot.startQuery + 1 : -1;
            slot.record = plan.recordCount++;
            slot.leader = true;
            ++stamped;
            continue;
        }
        if (!plan.hasBucket) {
            plan.hasBucket = true;
            bucketRecord = plan.recordCount++;
            slot.startQuery = static_cast<std::int32_t>(2 * recordBudget);
            slot.workQuery = fine ? slot.startQuery + 1 : -1;
            slot.leader = true;
        }
        slot.record = bucketRecord;
        slot.flags = kGpuRecordOverflowBucket;
        ++plan.foldedPasses;
    }
    plan.stampedRecords = stamped + (plan.hasBucket ? 1u : 0u);
    return plan;
}

GpuFrameReport ResolveGpuStamps(const GpuStampPlan& plan, std::span<const GpuStampAssignment> assignments,
    const GpuReadInput& input, std::span<GpuPassRecord> records) noexcept
{
    GpuFrameReport report;
    report.recordCount = plan.recordCount;
    report.foldedPasses = plan.foldedPasses;
    report.barrierMeasured = plan.mode == GpuStampMode::Fine;
    if (records.size() < plan.recordCount) {
        report.state = GpuReadState::Error;
        return report;
    }

    const std::size_t passCount = std::min<std::size_t>(plan.passCount, assignments.size());
    for (std::uint32_t r = 0; r < plan.recordCount; ++r) {
        records[r].barrierMs = 0.0f;
        records[r].workMs = 0.0f;
        records[r].flags = 0;
        records[r].foldedPasses = 0;
        records[r].state = GpuReadState::Present;
    }
    for (std::size_t i = 0; i < passCount; ++i) {
        const GpuStampAssignment& slot = assignments[i];
        if (slot.record >= plan.recordCount) {
            continue;
        }
        records[slot.record].flags |= slot.flags;
        if ((slot.flags & kGpuRecordOverflowBucket) != 0) {
            ++records[slot.record].foldedPasses;
        }
    }

    GpuReadState early = GpuReadState::Present;
    bool stop = true;
    if (!input.capability.supported) {
        early = GpuReadState::Unsupported;
    } else if (input.call == GpuReadCall::Failed) {
        early = GpuReadState::Error;
    } else if (!input.captureEnabled) {
        early = GpuReadState::Absent;
    } else {
        stop = false;
    }
    if (stop) {
        report.state = early;
        SetAllStates(records, plan.recordCount, early);
        return report;
    }

    const StampReader reader(input);
    const bool fine = plan.mode == GpuStampMode::Fine;
    OpenRecord open;
    std::int32_t firstStart = -1;
    const auto close = [&](std::int32_t nextStart) noexcept {
        FinishRecord(records[open.record], open.barrier, reader.Between(open.workFrom, nextStart), report);
    };
    for (std::size_t i = 0; i < passCount; ++i) {
        const GpuStampAssignment& slot = assignments[i];
        if (!slot.leader || slot.startQuery < 0 || slot.record >= plan.recordCount) {
            continue;
        }
        if (open.active) {
            close(slot.startQuery);
        }
        open.active = true;
        open.record = slot.record;
        open.workFrom = fine ? slot.workQuery : slot.startQuery;
        open.barrier = fine ? reader.Between(slot.startQuery, slot.workQuery) : Delta{ true, false, 0.0 };
        if (firstStart < 0) {
            firstStart = slot.startQuery;
        }
    }
    if (open.active) {
        close(plan.endQuery);
    }

    const Delta tail = reader.Between(plan.endQuery, plan.finalQuery);
    const Delta span = reader.Between(firstStart >= 0 ? firstStart : plan.endQuery, plan.finalQuery);
    if (tail.known && !tail.bad) {
        report.tailMs = tail.ms;
        report.tailValid = true;
        report.summedMs += tail.ms;
    }
    if (span.known && !span.bad) {
        report.spanMs = span.ms;
        report.spanValid = true;
    }

    const std::uint32_t okRecords = plan.stampedRecords - report.errorRecords - report.absentRecords;
    const bool allMeasured = report.errorRecords == 0 && report.absentRecords == 0 && report.partialRecords == 0
        && report.tailValid && report.spanValid && plan.foldedPasses == 0;
    if (allMeasured) {
        report.state = GpuReadState::Present;
    } else if (report.tailValid || okRecords > 0) {
        report.state = GpuReadState::Partial;
    } else if (report.errorRecords > 0) {
        report.state = GpuReadState::Error;
    } else {
        report.state = GpuReadState::Absent;
    }
    return report;
}

} // namespace gte
