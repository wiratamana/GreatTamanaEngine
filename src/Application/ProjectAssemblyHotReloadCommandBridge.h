#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

// Handoff for POST /project_assembly/hot_reload: the network thread submits and blocks, the main thread fulfills.
// A timeout does not free the slot; new submits report alreadyPending until FulfillPending().
class ProjectAssemblyHotReloadCommandBridge {
public:
    ProjectAssemblyHotReloadCommandBridge() = default;
    ~ProjectAssemblyHotReloadCommandBridge() = default;

    ProjectAssemblyHotReloadCommandBridge(const ProjectAssemblyHotReloadCommandBridge&) = delete;
    ProjectAssemblyHotReloadCommandBridge& operator=(const ProjectAssemblyHotReloadCommandBridge&) = delete;

    struct SubmitResult {
        bool alreadyPending = false; // A reload is in flight; returns immediately.
        bool timedOut = false;       // No completion within the timeout.
    };

    // Network thread. The default timeout is long on purpose: a cmake build can take minutes.
    SubmitResult SubmitAndWait(const std::string& projectName, int timeoutMilliseconds = 600000);

    // Main thread: copy of the pending project name, or nullopt.
    std::optional<std::string> TryPeekPendingProjectName() const;

    // Main thread: the only place that frees the slot. No-op when nothing is pending.
    void FulfillPending();

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    std::string m_projectName;
};

} // namespace gte
