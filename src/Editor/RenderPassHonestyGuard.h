#pragma once

#include "RenderPassHonestyChecker.h"

#include <string>
#include <unordered_set>
#include <vector>

// task_manager/editor-core-separation-21 campaign, PHASE5
// (PHASE5_IRON_RULE_PERMANENT_MISMATCH_DETECTOR.md) - the real, Logger-aware
// singleton wrapper around the pure RenderPassHonestyChecker.h - mirrors
// ImGuiIdConflictGuard.h's exact shape/reasoning for the same reason: the
// one production call site (FrameDebuggerPanel::TriggerCapture()) is
// main-thread-only, and there is no single natural owner to thread a
// reference to this through every intervening call frame.

namespace gte {

// Editor-owned, process-global, main-thread-only singleton. Wraps
// DetectRenderPassHonestyMismatches() (RenderPassHonestyChecker.h, the pure
// detector) with the one real-world concern the pure detector deliberately
// knows nothing about: WHEN to actually log a mismatch via the engine's own
// logging system - "log once per NEW incident, not once per capture that
// still has the exact same stale mismatch" (mirrors
// ImGuiIdConflictGuard::CheckCurrentIdScope()'s own "Locked Design Decision
// #2" precedent precisely, reusing its actual pattern/shape rather than
// inventing a new one). Deliberately untested by any automated test (Tier
// 2 - needs a live installed Logger sink to observe its own logging side
// effect) - exactly the same "pure logic tested, Logger-aware wrapper not"
// split ImGuiIdConflictTracker/ImGuiIdConflictGuard already established.
class RenderPassHonestyGuard {
public:
    static RenderPassHonestyGuard& Instance() noexcept;

    // Called once per Frame Debugger capture
    // (FrameDebuggerPanel::TriggerCapture()) with THIS capture's full
    // mismatch list, already computed by DetectRenderPassHonestyMismatches().
    // Logs GTE_LOG_ERROR("RenderPassHonesty", ...) exactly once per pass
    // name that is NEWLY mismatched since the last call - a pass whose
    // mismatch persists across several consecutive captures is logged only
    // the first time; a pass whose mismatch cleared for at least one
    // capture and then reappears is treated as a genuinely NEW incident and
    // logged again (mirrors ImGuiIdConflictGuard's own "erase when no
    // longer conflicting" rule so a future recurrence is always treated as
    // fresh).
    void ReportCaptureMismatches(const std::vector<std::string>& mismatchedNames);

    // Exposed purely for tests/diagnostics - how many mismatches are
    // currently considered "ongoing" (already logged, not yet cleared). No
    // production call site needs this.
    std::size_t OngoingMismatchCount() const noexcept;

private:
    RenderPassHonestyGuard() = default;

    std::unordered_set<std::string> m_ongoingMismatches;
};

} // namespace gte
