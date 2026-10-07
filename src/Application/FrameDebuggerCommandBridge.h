#pragma once

// Cross-thread bridge for network-driven Frame Debugger commands: open the
// window, enable/disable capture, select an event, adjust channel/levels,
// read current state, or fetch one real per-draw event's own captured
// image. See SyncCommandBridge.h for the handoff mechanism.
//
// GET /frame_debugger/state also goes through this bridge (a GetState
// command) rather than a direct read - the Frame Debugger's state is
// mutable, main-thread-owned data with no atomics of its own.

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
    GetEventTexture, // Per-draw event capture - GET /frame_debugger/get_event_texture.
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

struct FrameDebuggerGetEventTextureCommand {
    int index = -1;
};

// One pending Frame Debugger command, tagged by `kind` - only the field
// matching `kind` is meaningful.
struct FrameDebuggerCommandRequest {
    FrameDebuggerCommandKind kind = FrameDebuggerCommandKind::GetState;
    FrameDebuggerSetEnabledCommand setEnabled;
    FrameDebuggerSelectEventCommand selectEvent;
    FrameDebuggerSetChannelCommand setChannel;
    FrameDebuggerSetLevelsCommand setLevels;
    FrameDebuggerGetEventTextureCommand getEventTexture;
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

    // Per-draw event capture - the live NextEventIndex() high-water mark for
    // whichever frame is currently captured. A NEW, additive field,
    // defaulting to 0 until a real capture has happened at least once -
    // every existing field's own name/type/meaning is unchanged.
    int perDrawEventCount = 0;
};

// Per-draw event capture outcome - meaningful only for
// FrameDebuggerCommandKind::GetEventTexture. `pixels` is already-PNG-encoded
// bytes, the same encoding GET /get_texture already returns. `found` is
// false whenever that exact index has never been captured yet (including
// the normal "just armed, wait for the next captured frame" case).
struct FrameDebuggerEventTextureOutcome {
    bool found = false;
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Outcome of one FrameDebuggerCommandRequest. `success` is false for
// CaptureNow when capture isn't enabled, or SetChannel given an
// unrecognized string. `state` is always populated with the resulting
// state after the command ran.
struct FrameDebuggerCommandResult {
    FrameDebuggerCommandKind kind = FrameDebuggerCommandKind::GetState;
    bool success = true;
    FrameDebuggerStateOutcome state;
    // Meaningful only when kind == FrameDebuggerCommandKind::GetEventTexture.
    FrameDebuggerEventTextureOutcome eventTexture;
};

class FrameDebuggerCommandBridge
    : public SyncCommandBridge<FrameDebuggerCommandRequest, FrameDebuggerCommandResult> {
};

} // namespace gte
