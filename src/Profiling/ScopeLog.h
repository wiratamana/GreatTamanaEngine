#pragma once

#include "EventBuffer.h"
#include "FrameGate.h"
#include "ProfilingBudget.h"
#include "ScopeAggregate.h"
#include "ScopeEvent.h"
#include "ScopeTree.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace gte::Profiling {

// Handle returned by Begin() and handed back to End().
struct ScopeStart {
    std::uint64_t startTicks = 0;
    std::uint64_t token = 0;
    std::uint16_t depth = 0;
    bool tracked = false; // counted in the depth of the main thread
};

// The open frame's scope log. Only the main thread appends; other threads are counted and dropped.
// Time is passed in by the caller, so the log works with any clock.
class ScopeLog {
public:
    // Starts a frame and discards the previous frame's events and aggregates.
    void BeginFrame(bool captureOn) noexcept
    {
        for (const ScopeAggregate& row : m_aggregates.Rows()) {
            m_aggregateSlot[row.name] = 0;
        }
        m_aggregates.Clear();
        m_childTicks.fill(0);
        m_buffer.Clear();
        m_depth = 0;
        m_gate.BeginFrame(captureOn);
    }

    void EndFrame() noexcept { m_gate.EndFrame(); }

    ScopeStart Begin(std::uint64_t nowTicks) noexcept
    {
        ScopeStart start;
        start.startTicks = nowTicks;
        start.token = m_gate.ScopeToken();
        if (start.token != 0 && FrameGate::IsMainThread()) {
            start.tracked = true;
            start.depth = m_depth++;
        }
        return start;
    }

    // Appends the finished scope when it still belongs to the open frame.
    void End(const ScopeStart& start, NameId name, ScopeKind kind, ViewSlot view, std::uint64_t nowTicks) noexcept
    {
        if (m_gate.Admit(start.token) != ScopeAdmit::Record) {
            return;
        }
        if (m_depth > 0) {
            --m_depth;
        }
        const ScopeEvent event{ start.startTicks, nowTicks, name, start.depth, kind, view };
        Accumulate(event); // runs even when the event buffer is full
        m_buffer.Append(event);
    }

    // Instant event at the current depth.
    void Marker(NameId name, ViewSlot view, std::uint64_t nowTicks) noexcept
    {
        const ScopeStart start = Begin(nowTicks);
        End(start, name, ScopeKind::Marker, view, nowTicks);
    }

    const ScopeEventBuffer& Buffer() const noexcept { return m_buffer; }
    const ScopeAggregateBuffer& Aggregates() const noexcept { return m_aggregates; }
    std::uint32_t AggregateNameOutOfRange() const noexcept { return m_aggregateNameOutOfRange; }
    FrameGate& Gate() noexcept { return m_gate; }

private:
    // Same total and self as ComputeScopeTimes. A name nested in itself counts its time twice.
    void Accumulate(const ScopeEvent& e) noexcept
    {
        const std::size_t depth = std::min<std::size_t>(e.depth, kMaxScopeDepth - 1);
        const std::uint64_t total = DurationTicks(e);
        const std::uint64_t children = m_childTicks[depth + 1];
        const std::uint64_t self = children > total ? 0 : total - children;
        m_childTicks[depth + 1] = 0;
        m_childTicks[depth] += total;
        if (e.kind == ScopeKind::Marker) {
            return;
        }
        if (e.name >= m_aggregateSlot.size()) {
            ++m_aggregateNameOutOfRange;
            return;
        }
        std::uint16_t& slot = m_aggregateSlot[e.name];
        if (slot == 0) {
            ScopeAggregate row;
            row.name = e.name;
            row.minDepth = e.depth;
            row.kind = e.kind;
            const std::size_t index = m_aggregates.Add(row);
            if (index == ScopeAggregateBuffer::kNone) {
                return;
            }
            slot = static_cast<std::uint16_t>(index + 1);
        }
        ScopeAggregate& row = m_aggregates.At(slot - 1u);
        row.totalTicks += total;
        row.selfTicks += self;
        row.maxTicks = std::max(row.maxTicks, total);
        ++row.calls;
        row.minDepth = std::min(row.minDepth, e.depth);
    }

    FrameGate m_gate;
    ScopeEventBuffer m_buffer;
    std::array<std::uint64_t, kMaxScopeDepth + 1> m_childTicks{};
    std::array<std::uint16_t, kMaxNames + 1> m_aggregateSlot{}; // 0 = none, else row index + 1
    ScopeAggregateBuffer m_aggregates;
    std::uint32_t m_aggregateNameOutOfRange = 0;
    std::uint16_t m_depth = 0;
};

} // namespace gte::Profiling
