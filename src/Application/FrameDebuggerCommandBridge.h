#pragma once

// Cross-thread bridge for network-driven Frame Debugger commands.
// GET /frame_debugger/state goes through it too: the state is main-thread data.

#include "SyncCommandBridge.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

enum class FrameDebuggerCommandKind {
    OpenWindow,
    SetEnabled,
    CaptureNow,
    SelectEvent,
    SetChannel,
    SetLevels,
    GetState,
    GetEventTexture,
};

struct FrameDebuggerSetEnabledCommand {
    bool enabled = false;
};

struct FrameDebuggerSelectEventCommand {
    int index = -1;
};

struct FrameDebuggerSetChannelCommand {
    // "all", "r", "g", "b" or "a". Validated before submission.
    std::string channel;
};

struct FrameDebuggerSetLevelsCommand {
    float black = 0.0f;
    float white = 1.0f;
};

struct FrameDebuggerGetEventTextureCommand {
    int index = -1;
};

// One pending command. Only the field matching `kind` is meaningful.
struct FrameDebuggerCommandRequest {
    FrameDebuggerCommandKind kind = FrameDebuggerCommandKind::GetState;
    FrameDebuggerSetEnabledCommand setEnabled;
    FrameDebuggerSelectEventCommand selectEvent;
    FrameDebuggerSetChannelCommand setChannel;
    FrameDebuggerSetLevelsCommand setLevels;
    FrameDebuggerGetEventTextureCommand getEventTexture;
};

// Read-only Frame Debugger state. Independent of anything under src/Editor/.
struct FrameDebuggerStateOutcome {
    bool enabled = false;
    bool windowOpen = false;
    bool hasCapturedFrame = false;
    int totalEventCount = 0;
    int selectedEventIndex = -1;
    std::string channel = "all";
    float levelsBlack = 0.0f;
    float levelsWhite = 1.0f;

    // Draw events recorded in the captured frame. 0 until a capture exists.
    int perDrawEventCount = 0;

    // "disabled", "pending", "captured" or "failed".
    std::string captureStatus = "disabled";

    // Reason text. Empty unless captureStatus is "failed".
    std::string captureError;
};

// Result of GetEventTexture. `pixels` is PNG-encoded; `found` is false until that event index is captured.
struct FrameDebuggerEventTextureOutcome {
    bool found = false;
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Outcome of one command. `success` is false for CaptureNow while disabled or SetChannel with a bad string.
// `state` is always the state after the command ran.
struct FrameDebuggerCommandResult {
    FrameDebuggerCommandKind kind = FrameDebuggerCommandKind::GetState;
    bool success = true;
    FrameDebuggerStateOutcome state;
    // Only meaningful when kind == GetEventTexture.
    FrameDebuggerEventTextureOutcome eventTexture;
};

class FrameDebuggerCommandBridge
    : public SyncCommandBridge<FrameDebuggerCommandRequest, FrameDebuggerCommandResult> {
};

} // namespace gte
