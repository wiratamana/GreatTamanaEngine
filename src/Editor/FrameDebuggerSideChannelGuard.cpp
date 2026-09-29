#include "FrameDebuggerSideChannelGuard.h"

#include "../Core/Logging.h"

namespace gte {

FrameDebuggerSideChannelGuard& FrameDebuggerSideChannelGuard::Instance() noexcept
{
    static FrameDebuggerSideChannelGuard s_instance;
    return s_instance;
}

void FrameDebuggerSideChannelGuard::ReportCaptureMismatches(const std::vector<std::string>& leakingKeyDebugNames)
{
    const std::unordered_set<std::string> currentSet(leakingKeyDebugNames.begin(), leakingKeyDebugNames.end());

    for (const std::string& name : leakingKeyDebugNames) {
        if (m_ongoingMismatches.insert(name).second) {
            GTE_LOG_ERROR("FrameDebuggerSideChannel",
                "RenderPassBlackboard key '" + name
                    + "' was published this frame even though its own gating pass is DISABLED - a disabled "
                      "pass's own side effect is still visible to whichever other pass reads this key "
                      "(Clause C violation).");
        }
    }

    for (auto it = m_ongoingMismatches.begin(); it != m_ongoingMismatches.end();) {
        if (currentSet.find(*it) == currentSet.end()) {
            it = m_ongoingMismatches.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t FrameDebuggerSideChannelGuard::OngoingMismatchCount() const noexcept
{
    return m_ongoingMismatches.size();
}

} // namespace gte
