#include "ProjectAssemblyHotReloadCommandBridge.h"

#include <chrono>

namespace gte {

ProjectAssemblyHotReloadCommandBridge::SubmitResult ProjectAssemblyHotReloadCommandBridge::SubmitAndWait(
    const std::string& projectName, int timeoutMilliseconds)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    SubmitResult result;
    if (m_requested) {
        result.alreadyPending = true;
        return result;
    }

    m_requested = true;
    m_fulfilled = false;
    m_projectName = projectName;

    const bool fulfilledInTime = m_conditionVariable.wait_for(
        lock, std::chrono::milliseconds(timeoutMilliseconds), [this] { return m_fulfilled; });
    // FulfillPending() already freed the slot; on timeout it stays held until the reload ends.
    result.timedOut = !fulfilledInTime;
    return result;
}

std::optional<std::string> ProjectAssemblyHotReloadCommandBridge::TryPeekPendingProjectName() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_requested) {
        return std::nullopt;
    }
    return m_projectName;
}

void ProjectAssemblyHotReloadCommandBridge::FulfillPending()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_requested) {
            return;
        }
        m_fulfilled = true;
        m_requested = false;
    }
    m_conditionVariable.notify_one();
}

} // namespace gte
