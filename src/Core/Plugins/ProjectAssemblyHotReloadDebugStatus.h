#pragma once

// src/Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h
//
// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1), PHASE1. A plain, thread-safe, process-wide singleton
// (Meyers singleton, mirrors ComponentTypeRegistry::Instance()'s own
// precedent) - the ONE place a future BIG-STEP 3 campaign's
// PerformProjectAssemblyHotReload() will report its own live progress, and
// the ONE place EditorHotReloadDebugCapability::GetHotReloadStatus() (PHASE2)
// reads it back from. Deliberately NOT a bridge/
// TryPeekPendingCommandRequest()-shaped class - this must stay readable from
// the network thread even while, in a future campaign, the main thread is
// mid-freeze running a whole hot-reload cycle, which a frame-drain-dependent
// bridge structurally cannot support (see PHASE0_MASTER_STRATEGY.md, Section
// 3.1, Correction 2, for the general reasoning this mirrors).
//
// THIS campaign (editor-core-separation-12) adds ZERO Set()/Finish() call
// sites anywhere - GetSnapshot() will report "Idle" (the default-constructed
// Status) for this whole campaign's lifetime. That is expected and correct:
// this class exists now so its OWN shape, and GetHotReloadStatus()'s own
// route contract, are locked in and stable before a future BIG-STEP 3
// campaign ever needs them.

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
