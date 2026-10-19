#pragma once

// Process-wide live status of the project assembly hot reload, readable from the network thread
// even while the main thread is frozen inside a reload (a frame-drained bridge cannot do that).

#include "../EditorCapabilities.h" // IHotReloadDebugCapability::Status.

#include <cstdint>
#include <mutex>
#include <string>

namespace gte {

class ProjectAssemblyHotReloadDebugStatus {
public:
    static ProjectAssemblyHotReloadDebugStatus& Instance();

    // Reserved for a future BIG-STEP 3 campaign - called ONLY from the main
    // thread, ONLY from inside that future campaign's
    // PerformProjectAssemblyHotReload(), one call at each phase transition.
    // Never called by this campaign. Implemented now (rather than left
    // undeclared) so this class's own shape is proven correct and
    // unit-testable today.
    void Set(const std::string& phase, const std::string& projectName);
    void Finish(const std::string& outcome, const std::string& errorMessage);

    // Called from ANY thread (the network thread's route handler) - returns
    // a plain value-type snapshot, locked briefly, copied out, unlocked -
    // never blocks on anything the main thread might currently be doing.
    IHotReloadDebugCapability::Status GetSnapshot() const;

private:
    ProjectAssemblyHotReloadDebugStatus() = default;
    mutable std::mutex m_mutex;
    IHotReloadDebugCapability::Status m_status;
    std::uint64_t m_nextCycleId = 1;
};

} // namespace gte
