#pragma once

#include "RenderGraphConsistencyGuard.h"
#include "FrameDebuggerCoverageChecker.h"

#include <string>

// Logs "FrameDebuggerCoverage" once per pass that ran but is missing from the Frame Debugger event tree.
// Wraps DetectPassesMissingFromFrameDebuggerTree() (FrameDebuggerCoverageChecker.h).
namespace gte {

struct FrameDebuggerCoverageGuardTag {
    static constexpr const char* LogCategory() noexcept { return "FrameDebuggerCoverage"; }
    static std::string FormatMessage(const std::string& passName);
};

using FrameDebuggerCoverageGuard = ConsistencyGuard<FrameDebuggerCoverageGuardTag>;

} // namespace gte
