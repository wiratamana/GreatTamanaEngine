#include "RenderPassHonestyGuard.h"

#include "../Core/Logging.h"

namespace gte {

RenderPassHonestyGuard& RenderPassHonestyGuard::Instance() noexcept
{
    static RenderPassHonestyGuard s_instance;
    return s_instance;
}

void RenderPassHonestyGuard::ReportCaptureMismatches(const std::vector<std::string>& mismatchedNames)
{
    const std::unordered_set<std::string> currentSet(mismatchedNames.begin(), mismatchedNames.end());

    for (const std::string& name : mismatchedNames) {
        if (m_ongoingMismatches.insert(name).second) {
            // .second == true means this name was NOT already ongoing -
            // i.e. this is a genuinely NEW incident, not a continuation of
            // one already logged for an earlier capture. Log exactly once.
            GTE_LOG_ERROR("RenderPassHonesty",
                "Pass '" + name
                    + "' is marked DISABLED in RenderPassToggleRegistry but still executed and appears in this "
                      "frame's captured Render Graph snapshot - the render pass and the Frame Debugger disagree.");
        }
    }

    // Any previously-ongoing mismatch NOT present in this capture's own
    // list anymore has cleared - erase it so a FUTURE recurrence is treated
    // as fresh again (re-logged), exactly like
    // ImGuiIdConflictGuard::CheckCurrentIdScope()'s own "erase when no
    // longer conflicting" rule.
    for (auto it = m_ongoingMismatches.begin(); it != m_ongoingMismatches.end();) {
        if (currentSet.find(*it) == currentSet.end()) {
            it = m_ongoingMismatches.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t RenderPassHonestyGuard::OngoingMismatchCount() const noexcept
{
    return m_ongoingMismatches.size();
}

} // namespace gte
