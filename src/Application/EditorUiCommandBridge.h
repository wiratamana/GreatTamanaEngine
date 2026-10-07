#pragma once

// Cross-thread bridge for network-driven Editor UI commands: focusing a
// docked tab, or spawning a GPU-driven test batch. See SyncCommandBridge.h
// for the handoff mechanism.

#include "SyncCommandBridge.h"

#include <cstdint>
#include <string>

namespace gte {

enum class EditorUiCommandKind {
    ActivateTab,
    SpawnGpuDrivenTestBatch,
};

// Plain request payload for one ActivateTab command.
struct ActivateTabCommand {
    std::string tabName;
};

// Plain request payload for one SpawnGpuDrivenTestBatch command.
struct SpawnGpuDrivenTestBatchCommand {
    // Must be >= 1 for a real batch to actually be spawned.
    std::uint32_t instanceCount = 6;
};

// One pending Editor UI command, tagged by `kind` - only the field matching
// `kind` is meaningful.
struct EditorUiCommandRequest {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabCommand activateTab;
    SpawnGpuDrivenTestBatchCommand spawnGpuDrivenTestBatch;
};

// Outcome of one ActivateTab command.
// - success == true                        -> tab found and focused
// - success == false && tabExists == false  -> no live ImGui window with
//                                              that exact name existed this
//                                              frame
// A name not in the known tab list is rejected before ever reaching this
// bridge - this outcome type has no field for that case.
struct ActivateTabOutcome {
    bool success = false;
    bool tabExists = false;
};

// Outcome of one SpawnGpuDrivenTestBatch command. `success == false` covers
// both "instanceCount was 0" and "the Editor module is unavailable in this
// build" - `errorMessage` explains which, `editorAvailable` distinguishes
// the two without sniffing error text.
struct SpawnGpuDrivenTestBatchOutcome {
    bool success = false;
    bool editorAvailable = true;
    std::string errorMessage;
    std::uint32_t instanceCount = 0;
};

// The completed result of one EditorUiCommandRequest - `kind` mirrors the
// request's own `kind`.
struct EditorUiCommandResult {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabOutcome activateTab;
    SpawnGpuDrivenTestBatchOutcome spawnGpuDrivenTestBatch;
};

class EditorUiCommandBridge : public SyncCommandBridge<EditorUiCommandRequest, EditorUiCommandResult> {
};

} // namespace gte
