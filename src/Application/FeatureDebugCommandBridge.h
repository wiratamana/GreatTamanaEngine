#pragma once

// Cross-thread bridge for network-driven feature debug commands (list, state, set, invoke).
// See SyncCommandBridge.h for the handoff mechanism.

#include "SyncCommandBridge.h"

#include "../Core/FeatureKit/FeatureDebug.h"

#include <string>
#include <vector>

namespace gte {

enum class FeatureDebugCommandKind {
    List,
    GetState,
    Set,
    Invoke,
};

// One pending command; only the fields that match `kind` are meaningful.
struct FeatureDebugCommandRequest {
    FeatureDebugCommandKind kind = FeatureDebugCommandKind::List;
    std::string featureName;
    std::string action;
    std::vector<fx::FeatureDebugArg> args;
};

// `found` is false when no loaded feature of that name has a debug endpoint.
struct FeatureDebugCommandResult {
    bool found = false;
    fx::FeatureDebugResult result;
    std::vector<std::string> featureNames;
};

class FeatureDebugCommandBridge : public SyncCommandBridge<FeatureDebugCommandRequest, FeatureDebugCommandResult> {
};

} // namespace gte
