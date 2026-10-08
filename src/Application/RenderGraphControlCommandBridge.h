#pragma once

// Cross-thread bridge for network-driven render-graph control commands:
// toggling a built-in pass or plugin feature, or listing current pass
// states. See SyncCommandBridge.h for the request/result handoff mechanism.

#include "SyncCommandBridge.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

enum class RenderGraphControlCommandKind {
    SetBuiltInPassEnabled,
    ListPassStates,
    SetFeatureEnabled,
    SetFeaturePriority,
    SetDisplayedRegime,
};

struct RenderGraphControlSetPassEnabledCommand {
    std::string name;
    bool enabled = false;
};

struct RenderGraphControlSetFeatureEnabledCommand {
    std::string name;
    bool enabled = false;
};

struct RenderGraphControlSetFeaturePriorityCommand {
    std::string name;
    std::int32_t priority = 0;
};

// Which Render Graph regime the panel should display - true means
// "Present", false means "Offscreen (Game/Scene View)".
struct RenderGraphControlSetDisplayedRegimeCommand {
    bool present = false;
};

// One pending render-graph-control command, tagged by `kind` - only the
// field matching `kind` is meaningful.
struct RenderGraphControlCommandRequest {
    RenderGraphControlCommandKind kind = RenderGraphControlCommandKind::ListPassStates;
    RenderGraphControlSetPassEnabledCommand setPassEnabled;
    RenderGraphControlSetFeatureEnabledCommand setFeatureEnabled;
    RenderGraphControlSetFeaturePriorityCommand setFeaturePriority;
    RenderGraphControlSetDisplayedRegimeCommand setDisplayedRegime;
};

// One reported pass toggle state.
struct RenderGraphControlPassStateOutcome {
    std::string name;
    bool enabled = false;
    bool everDeclaredThisSession = false;
};

// Outcome of one RenderGraphControlCommandRequest. `success` is false for
// SetBuiltInPassEnabled on a deny-listed name, or SetFeatureEnabled/
// SetFeaturePriority on an unrecognized feature name. `passStates` is
// populated only for ListPassStates. `windowOpen` is populated only for
// SetDisplayedRegime - true if the "Render Graph" window was open (and
// therefore actually displaying the new regime) at the moment it was applied.
struct RenderGraphControlCommandResult {
    RenderGraphControlCommandKind kind = RenderGraphControlCommandKind::ListPassStates;
    bool success = true;
    std::string errorMessage;
    std::vector<RenderGraphControlPassStateOutcome> passStates;
    bool windowOpen = false;
};

class RenderGraphControlCommandBridge
    : public SyncCommandBridge<RenderGraphControlCommandRequest, RenderGraphControlCommandResult> {
};

} // namespace gte
