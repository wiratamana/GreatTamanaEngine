#pragma once

#include <cstdint>

namespace gte {

// Where one capture request is in its life. Core and the panel move it from two points of the same frame.
enum class FrameDebuggerCapturePhase : std::uint8_t {
    Idle,           // No request in flight.
    Requested,      // A trigger fired. The next offscreen build must declare the replay passes.
    ReplayDeclared, // This frame declared the replay passes. The panel captures later in the same frame.
    Failed,         // The request could not finish. Only a new request or a disable leaves this phase.
};

class FrameDebuggerCaptureHandshake {
public:
    FrameDebuggerCapturePhase Phase() const noexcept { return m_phase; }

    // Any trigger: Enable, Capture button, Step, HTTP capture, pass toggle. Idempotent. Leaves Failed.
    void Request() noexcept
    {
        if (m_phase != FrameDebuggerCapturePhase::ReplayDeclared) {
            m_phase = FrameDebuggerCapturePhase::Requested;
        }
    }

    // Core, once per frame, while the offscreen graph is declared. True when this frame must declare replay passes.
    bool ConsumeForReplay() noexcept
    {
        if (m_phase != FrameDebuggerCapturePhase::Requested) {
            return false;
        }
        m_phase = FrameDebuggerCapturePhase::ReplayDeclared;
        return true;
    }

    // Panel, after the offscreen graph executed. True when the capture must run now.
    bool ConsumeForCapture() noexcept
    {
        if (m_phase != FrameDebuggerCapturePhase::ReplayDeclared) {
            return false;
        }
        m_phase = FrameDebuggerCapturePhase::Idle;
        return true;
    }

    // The frame failed. Fails only a request that is in flight. Returns true when the phase changed.
    bool NotifyFrameFailed() noexcept
    {
        if (m_phase != FrameDebuggerCapturePhase::Requested && m_phase != FrameDebuggerCapturePhase::ReplayDeclared) {
            return false;
        }
        m_phase = FrameDebuggerCapturePhase::Failed;
        return true;
    }

    // The capture itself failed after ConsumeForCapture(). Unconditional.
    void Fail() noexcept { m_phase = FrameDebuggerCapturePhase::Failed; }

    // Disable: drops everything, including Failed.
    void Cancel() noexcept { m_phase = FrameDebuggerCapturePhase::Idle; }

private:
    FrameDebuggerCapturePhase m_phase = FrameDebuggerCapturePhase::Idle;
};

// The status word of GET /frame_debugger/state. Idle while enabled without a capture cannot happen; it reads "pending".
inline const char* FrameDebuggerCaptureStatusName(
    bool enabled, FrameDebuggerCapturePhase phase, bool hasCapturedFrame) noexcept
{
    if (!enabled) {
        return "disabled";
    }
    switch (phase) {
    case FrameDebuggerCapturePhase::Failed:
        return "failed";
    case FrameDebuggerCapturePhase::Requested:
    case FrameDebuggerCapturePhase::ReplayDeclared:
        return "pending";
    case FrameDebuggerCapturePhase::Idle:
        return hasCapturedFrame ? "captured" : "pending";
    }
    return "pending";
}

} // namespace gte
