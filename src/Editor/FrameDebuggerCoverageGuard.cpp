#include "FrameDebuggerCoverageGuard.h"

#include "../Core/Logging.h"

namespace gte {

FrameDebuggerCoverageGuard& FrameDebuggerCoverageGuard::Instance() noexcept
{
    static FrameDebuggerCoverageGuard s_instance;
    return s_instance;
}

void FrameDebuggerCoverageGuard::ReportCaptureMismatches(const std::vector<std::string>& missingPassNames)
{
    const std::unordered_set<std::string> currentSet(missingPassNames.begin(), missingPassNames.end());

    for (const std::string& name : missingPassNames) {
        if (m_ongoingMismatches.insert(name).second) {
            // .second == true means this name was NOT already ongoing -
            // i.e. this is a genuinely NEW incident, not a continuation of
            // one already logged for an earlier capture. Log exactly once.
            GTE_LOG_ERROR("FrameDebuggerCoverage",
                "Pass '" + name
                    + "' genuinely executed this frame (non-culled, non-FrameDebuggerInternal, non-SceneView) "
                      "but has NO corresponding leaf anywhere in the captured Frame Debugger event tree - the "
                      "render pass ran, but the Frame Debugger shows nothing for it (Clause B violation).");
        }
    }

    // Any previously-ongoing gap NOT present in this capture's own list
    // anymore has cleared - erase it so a FUTURE recurrence is treated as
    // fresh again (re-logged), exactly like
    // RenderPassHonestyGuard::ReportCaptureMismatches()'s own identical rule.
    for (auto it = m_ongoingMismatches.begin(); it != m_ongoingMismatches.end();) {
        if (currentSet.find(*it) == currentSet.end()) {
            it = m_ongoingMismatches.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t FrameDebuggerCoverageGuard::OngoingMismatchCount() const noexcept
{
    return m_ongoingMismatches.size();
}

} // namespace gte
