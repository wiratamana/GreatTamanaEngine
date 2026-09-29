#pragma once

#include "FrameDebuggerSideChannelChecker.h"

#include <string>
#include <unordered_set>
#include <vector>

// task_manager/editor-core-separation-22 campaign, PHASE6
// (PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md, Step 3.3) - the real,
// Logger-aware singleton wrapper around the pure
// FrameDebuggerSideChannelChecker.h, mirroring RenderPassHonestyGuard.h's
// (and FrameDebuggerCoverageGuard.h's) exact shape/reasoning for the
// identical reason: the one production call site
// (FrameDebuggerPanel::TriggerCapture()) is main-thread-only, and there is
// no single natural owner to thread a reference to this through every
// intervening call frame.

namespace gte {

// Editor-owned, process-global, main-thread-only singleton. Wraps
// DetectDisabledPassBlackboardKeyLeaks() (FrameDebuggerSideChannelChecker.h,
// the pure Clause C detector) with the one real-world concern the pure
// detector deliberately knows nothing about: WHEN to actually log a leak
// via the engine's own logging system - "log once per NEW incident, not
// once per capture that still has the exact same stale leak" (mirrors
// RenderPassHonestyGuard/FrameDebuggerCoverageGuard's own identical
// "log-once-per-new-incident" precedent EXACTLY). Kept as its own,
// separate singleton/log category (rather than merged into either sibling
// guard) so all three detectors' own log output stay independently
// greppable via `GET /get_logs?category=<X>`. Deliberately untested by any
// automated test (Tier 2 - needs a live installed Logger sink to observe
// its own logging side effect) - exactly the same "pure logic tested,
// Logger-aware wrapper not" split every sibling guard in this file already
// established.
class FrameDebuggerSideChannelGuard {
public:
    static FrameDebuggerSideChannelGuard& Instance() noexcept;

    // Called once per Frame Debugger capture
    // (FrameDebuggerPanel::TriggerCapture()) with THIS capture's full leak
    // list, already computed by DetectDisabledPassBlackboardKeyLeaks().
    // Logs GTE_LOG_ERROR("FrameDebuggerSideChannel", ...) exactly once per
    // key debug name that is NEWLY leaking since the last call - a leak
    // that persists across several consecutive captures is logged only the
    // first time; a leak that cleared for at least one capture and then
    // reappears is treated as a genuinely NEW incident and logged again.
    void ReportCaptureMismatches(const std::vector<std::string>& leakingKeyDebugNames);

    // Exposed purely for tests/diagnostics - how many leaks are currently
    // considered "ongoing" (already logged, not yet cleared). No production
    // call site needs this.
    std::size_t OngoingMismatchCount() const noexcept;

private:
    FrameDebuggerSideChannelGuard() = default;

    std::unordered_set<std::string> m_ongoingMismatches;
};

} // namespace gte
