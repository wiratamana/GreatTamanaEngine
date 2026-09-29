#pragma once

#include "FrameDebuggerCoverageChecker.h"

#include <string>
#include <unordered_set>
#include <vector>

// task_manager/editor-core-separation-22 campaign, PHASE6
// (PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md, Step 3.1) - the real,
// Logger-aware singleton wrapper around the pure FrameDebuggerCoverageChecker.h,
// mirroring RenderPassHonestyGuard.h's exact shape/reasoning (itself
// mirroring ImGuiIdConflictGuard.h) for the identical reason: the one
// production call site (FrameDebuggerPanel::TriggerCapture()) is
// main-thread-only, and there is no single natural owner to thread a
// reference to this through every intervening call frame.

namespace gte {

// Editor-owned, process-global, main-thread-only singleton. Wraps
// DetectPassesMissingFromFrameDebuggerTree() (FrameDebuggerCoverageChecker.h,
// the pure Clause B detector) with the one real-world concern the pure
// detector deliberately knows nothing about: WHEN to actually log a
// coverage gap via the engine's own logging system - "log once per NEW
// incident, not once per capture that still has the exact same stale gap"
// (mirrors RenderPassHonestyGuard::ReportCaptureMismatches()'s own
// identical "log-once-per-new-incident" precedent EXACTLY - this class is a
// deliberate structural twin, kept as its own, separate singleton/log
// category rather than merged into RenderPassHonestyGuard, so the two
// campaigns' own detectors stay independently greppable via
// `GET /get_logs?category=<X>`, per PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md's
// own Step 3.1 instruction). Deliberately untested by any automated test
// (Tier 2 - needs a live installed Logger sink to observe its own logging
// side effect) - exactly the same "pure logic tested, Logger-aware wrapper
// not" split RenderPassHonestyChecker/RenderPassHonestyGuard already
// established.
class FrameDebuggerCoverageGuard {
public:
    static FrameDebuggerCoverageGuard& Instance() noexcept;

    // Called once per Frame Debugger capture
    // (FrameDebuggerPanel::TriggerCapture()) with THIS capture's full
    // "missing from tree" pass-name list, already computed by
    // DetectPassesMissingFromFrameDebuggerTree(). Logs
    // GTE_LOG_ERROR("FrameDebuggerCoverage", ...) exactly once per pass
    // name that is NEWLY missing since the last call - a pass whose
    // coverage gap persists across several consecutive captures is logged
    // only the first time; a pass whose gap cleared for at least one
    // capture and then reappears is treated as a genuinely NEW incident and
    // logged again (mirrors RenderPassHonestyGuard::ReportCaptureMismatches()'s
    // own "erase when no longer mismatched" rule so a future recurrence is
    // always treated as fresh).
    void ReportCaptureMismatches(const std::vector<std::string>& missingPassNames);

    // Exposed purely for tests/diagnostics - how many coverage gaps are
    // currently considered "ongoing" (already logged, not yet cleared). No
    // production call site needs this.
    std::size_t OngoingMismatchCount() const noexcept;

private:
    FrameDebuggerCoverageGuard() = default;

    std::unordered_set<std::string> m_ongoingMismatches;
};

} // namespace gte
