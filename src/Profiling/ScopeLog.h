#pragma once

#include "EventBuffer.h"
#include "FrameGate.h"
#include "ScopeEvent.h"

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
    // Starts a frame and discards the previous frame's events.
    void BeginFrame(bool captureOn) noexcept
    {
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
        m_buffer.Append(ScopeEvent{ start.startTicks, nowTicks, name, start.depth, kind, view });
    }

    // Instant event at the current depth.
    void Marker(NameId name, ViewSlot view, std::uint64_t nowTicks) noexcept
    {
        const ScopeStart start = Begin(nowTicks);
        End(start, name, ScopeKind::Marker, view, nowTicks);
    }

    const ScopeEventBuffer& Buffer() const noexcept { return m_buffer; }
    FrameGate& Gate() noexcept { return m_gate; }

private:
    FrameGate m_gate;
    ScopeEventBuffer m_buffer;
    std::uint16_t m_depth = 0;
};

} // namespace gte::Profiling
