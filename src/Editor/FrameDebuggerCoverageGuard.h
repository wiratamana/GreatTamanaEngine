#pragma once

#include "RenderGraphConsistencyGuard.h"
#include "FrameDebuggerCoverageChecker.h"

#include <string>

// Logger-aware singleton wrapper around the pure
// DetectPassesMissingFromFrameDebuggerTree() (FrameDebuggerCoverageChecker.h).
// Logs "FrameDebuggerCoverage" exactly once per pass name newly found
// executed but missing from the Frame Debugger's own event tree.
namespace gte {

struct FrameDebuggerCoverageGuardTag {
    static constexpr const char* LogCategory() noexcept { return "FrameDebuggerCoverage"; }
    static std::string FormatMessage(const std::string& passName);
};

using FrameDebuggerCoverageGuard = ConsistencyGuard<FrameDebuggerCoverageGuardTag>;

} // namespace gte
