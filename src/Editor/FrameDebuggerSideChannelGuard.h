#pragma once

#include "RenderGraphConsistencyGuard.h"
#include "FrameDebuggerSideChannelChecker.h"

#include <string>

// Logger-aware singleton wrapper around the pure
// DetectDisabledPassBlackboardKeyLeaks() (FrameDebuggerSideChannelChecker.h).
// Logs "FrameDebuggerSideChannel" exactly once per blackboard key newly
// found leaking from a disabled pass's own gating.
namespace gte {

struct FrameDebuggerSideChannelGuardTag {
    static constexpr const char* LogCategory() noexcept { return "FrameDebuggerSideChannel"; }
    static std::string FormatMessage(const std::string& keyDebugName);
};

using FrameDebuggerSideChannelGuard = ConsistencyGuard<FrameDebuggerSideChannelGuardTag>;

} // namespace gte
