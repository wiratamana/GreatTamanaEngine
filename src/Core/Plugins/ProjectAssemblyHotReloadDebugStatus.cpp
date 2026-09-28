#include "ProjectAssemblyHotReloadDebugStatus.h"

#include <chrono>

namespace gte {

ProjectAssemblyHotReloadDebugStatus& ProjectAssemblyHotReloadDebugStatus::Instance()
{
    static ProjectAssemblyHotReloadDebugStatus instance;
    return instance;
}

void ProjectAssemblyHotReloadDebugStatus::Set(const std::string& phase, const std::string& projectName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_status.phase == "Idle" && phase != "Idle") {
        // A brand-new cycle is starting.
        m_status.cycleId = m_nextCycleId++;
    }
    m_status.phase = phase;
    m_status.projectName = projectName;
    // TODO(future BIG-STEP 3 campaign): compute phaseElapsedMilliseconds from
    // a steady_clock timestamp recorded here and read in GetSnapshot() -
    // deliberately left at its default (0) by this campaign since nothing
    // calls Set() yet, so wiring it up now would be untestable dead code.
}

void ProjectAssemblyHotReloadDebugStatus::Finish(const std::string& outcome, const std::string& errorMessage)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.phase = "Idle";
    m_status.lastOutcome = outcome;
    m_status.lastErrorMessage = errorMessage;
}

IHotReloadDebugCapability::Status ProjectAssemblyHotReloadDebugStatus::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

} // namespace gte
