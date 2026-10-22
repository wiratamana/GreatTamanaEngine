#pragma once

// Cross-thread bridge for network-driven Editor UI commands: focusing a
// docked tab, or spawning an instance grid. See SyncCommandBridge.h
// for the handoff mechanism.

#include "SyncCommandBridge.h"
#include "../Core/InstanceGridLimits.h"

#include <cstdint>
#include <string>

namespace gte {

enum class EditorUiCommandKind {
    ActivateTab,
    SpawnInstanceGrid,
};

// Plain request payload for one ActivateTab command.
struct ActivateTabCommand {
    std::string tabName;
};

// Plain request payload for one SpawnInstanceGrid command.
struct SpawnInstanceGridCommand {
    std::uint32_t instanceCount = kDefaultSpawnGridInstances; // 1..kMaxSpawnGridInstances.
    float spacing = kDefaultSpawnGridSpacing;                 // Centre distance between instances.
};

// One pending Editor UI command, tagged by `kind` - only the field matching
// `kind` is meaningful.
struct EditorUiCommandRequest {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabCommand activateTab;
    SpawnInstanceGridCommand spawnInstanceGrid;
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

// Outcome of one SpawnInstanceGrid command. `success == false` covers a refused grid
// and an Editor module that is unavailable in this build; `errorMessage` explains which,
// `editorAvailable` distinguishes the two without sniffing error text.
struct SpawnInstanceGridOutcome {
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
    SpawnInstanceGridOutcome spawnInstanceGrid;
};

class EditorUiCommandBridge : public SyncCommandBridge<EditorUiCommandRequest, EditorUiCommandResult> {
};

} // namespace gte
