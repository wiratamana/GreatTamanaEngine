#pragma once

// Cross-thread bridge for network-driven Frame Debugger commands: open the
// window, enable/disable capture, select an event, adjust channel/levels,
// or read current state. See SyncCommandBridge.h for the handoff mechanism.
//
// GET /frame_debugger/state also goes through this bridge (a GetState
// command) rather than a direct read - the Frame Debugger's state is
// mutable, main-thread-owned data with no atomics of its own.

#include "SyncCommandBridge.h"

#include <string>

namespace gte {

enum class FrameDebuggerCommandKind {
    OpenWindow,
    SetEnabled,
    CaptureNow,
    SelectEvent,
    SetChannel,
    SetLevels,
    GetState,
};

struct FrameDebuggerSetEnabledCommand {
    bool enabled = false;
};

struct FrameDebuggerSelectEventCommand {
    int index = -1;
};

struct FrameDebuggerSetChannelCommand {
    // "all"|"r"|"g"|"b"|"a" - already validated before submission.
    std::string channel;
};

struct FrameDebuggerSetLevelsCommand {
    float black = 0.0f;
    float white = 1.0f;
};

// One pending Frame Debugger command, tagged by `kind` - only the field
// matching `kind` is meaningful.
struct FrameDebuggerCommandRequest {
    FrameDebuggerCommandKind kind = FrameDebuggerCommandKind::GetState;
    FrameDebuggerSetEnabledCommand setEnabled;
    FrameDebuggerSelectEventCommand selectEvent;
    FrameDebuggerSetChannelCommand setChannel;
    FrameDebuggerSetLevelsCommand setLevels;
};

// The Frame Debugger's read-only state snapshot. A completely independent,
// Application-owned type - this header never depends on anything under
// src/Editor/.
struct FrameDebuggerStateOutcome {
    bool enabled = false;
    bool windowOpen = false;
    bool hasCapturedFrame = false;
    int totalEventCount = 0;
    int selectedEventIndex = -1;
    std::string channel = "all";
    float levelsBlack = 0.0f;
    float levelsWhite = 1.0f;
};

// Outcome of one FrameDebuggerCommandRequest. `success` is false for
// CaptureNow when capture isn't enabled, or SetChannel given an
// unrecognized string. `state` is always populated with the resulting
// state after the command ran.
struct FrameDebuggerCommandResult {
    FrameDebuggerCommandKind kind = FrameDebuggerCommandKind::GetState;
    bool success = true;
    FrameDebuggerStateOutcome state;
};

class FrameDebuggerCommandBridge
    : public SyncCommandBridge<FrameDebuggerCommandRequest, FrameDebuggerCommandResult> {
};

} // namespace gte
