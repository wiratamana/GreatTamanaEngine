#pragma once

#include "../Core/Logging.h"

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

// Generic "log once per new incident, clear when resolved" singleton shared
// by every Render Graph/Frame Debugger consistency detector wrapper
// (RenderPassHonestyGuard, FrameDebuggerCoverageGuard,
// FrameDebuggerSideChannelGuard). Each detector supplies its own `Tag` type
// with a distinct Logger category and message, via RenderPassHonestyGuard.h/
// FrameDebuggerCoverageGuard.h/FrameDebuggerSideChannelGuard.h - this class
// only owns the shared dedup bookkeeping, never the detection rule itself
// (that stays in each detector's own pure Checker.h function). Main-thread-
// only, process-global, exactly like the single production call site
// (FrameDebuggerPanel::TriggerCapture()) that drives all three.
namespace gte {

template <typename Tag>
class ConsistencyGuard {
public:
    static ConsistencyGuard& Instance() noexcept
    {
        static ConsistencyGuard s_instance;
        return s_instance;
    }

    // Call once per capture with THIS capture's full, current mismatch
    // list. Logs via Tag::LogCategory()/Tag::FormatMessage() exactly once
    // per name newly added since the last call - a mismatch that persists
    // across captures is logged only the first time; one that clears and
    // later reappears is logged again as a fresh incident.
    void ReportCaptureMismatches(const std::vector<std::string>& mismatchedNames)
    {
        const std::unordered_set<std::string> currentSet(mismatchedNames.begin(), mismatchedNames.end());

        for (const std::string& name : mismatchedNames) {
            if (m_ongoingMismatches.insert(name).second) {
                GTE_LOG_ERROR(Tag::LogCategory(), Tag::FormatMessage(name));
            }
        }

        // Anything no longer present has cleared - erase it so a later
        // recurrence is treated as fresh again.
        for (auto it = m_ongoingMismatches.begin(); it != m_ongoingMismatches.end();) {
            if (currentSet.find(*it) == currentSet.end()) {
                it = m_ongoingMismatches.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Diagnostics only - how many mismatches are currently "ongoing"
    // (already logged, not yet cleared). No production call site needs this.
    std::size_t OngoingMismatchCount() const noexcept { return m_ongoingMismatches.size(); }

private:
    ConsistencyGuard() = default;

    std::unordered_set<std::string> m_ongoingMismatches;
};

} // namespace gte
