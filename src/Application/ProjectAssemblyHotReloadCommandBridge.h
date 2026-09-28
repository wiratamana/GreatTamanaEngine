#pragma once

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE3. The ONE reviewed, thread-safe bridge a
// POST /project_assembly/hot_reload route handler (network thread) uses to
// make a full hot-reload cycle happen on the main thread, then BLOCKS
// until that cycle genuinely finishes (success, rollback, or critical
// failure - see ProjectAssemblyHotReloadDebugStatus for which). Mirrors
// EngineCommandBridge.h's own single-global-slot shape, but is
// DELIBERATELY its own, separate class - see PHASE0_MASTER_STRATEGY.md,
// Section 2.2 item 3, and the external design doc's own STEP 1 reasoning,
// for why reusing EngineCommandBridge's own result type would be a worse
// fit for a multi-second-to-multi-minute, blocking operation.

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

class ProjectAssemblyHotReloadCommandBridge {
public:
    ProjectAssemblyHotReloadCommandBridge() = default;
    ~ProjectAssemblyHotReloadCommandBridge() = default;

    ProjectAssemblyHotReloadCommandBridge(const ProjectAssemblyHotReloadCommandBridge&) = delete;
    ProjectAssemblyHotReloadCommandBridge& operator=(const ProjectAssemblyHotReloadCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        bool alreadyPending = false; // another hot-reload request is already in flight - returns immediately, no blocking.
        bool timedOut = false;       // timeoutMilliseconds elapsed with no completion signal from the main thread.
    };
    // A deliberately LONG default timeout (10 minutes) - a real cmake
    // build can legitimately take several minutes; this is NOT the
    // EngineCommandBridge's own 3000ms convention, on purpose.
    SubmitResult SubmitAndWait(const std::string& projectName, int timeoutMilliseconds = 600000);

    // --- Called from the MAIN thread (EditorHost::Run()) only -----------

    // Returns a COPY of the currently-pending project name, or
    // std::nullopt if nothing is pending - mirrors
    // EngineCommandBridge::TryPeekPendingCommandRequest()'s own exact
    // "atomically check-and-copy, never a separate IsPending()+fetch pair"
    // fix (that class's own header comment explains the check-then-use
    // race this avoids - identical reasoning applies here).
    std::optional<std::string> TryPeekPendingProjectName() const;

    // Signals completion back to whichever network thread is waiting (a
    // safe no-op if nothing is currently pending) - AND is the ONE AND ONLY
    // place that clears the pending slot (`m_requested = false`), including
    // after a client-side timeout (see SubmitAndWait()'s own doc comment
    // below for why this is a DELIBERATE, NECESSARY divergence from
    // EngineCommandBridge::FulfillCommand(), which never needs to clear
    // anything itself because SubmitAndWait() already did, on ITS OWN
    // timeout path, before FulfillCommand() is ever reached).
    //
    // read this carefully - it is NOT a line-for-line mirror of
    // EngineCommandBridge: EngineCommandBridge::SubmitAndWait() clears its
    // own equivalent flag on BOTH the success AND the timeout path (see
    // tests/Application/EngineCommandBridgeTests.cpp's own
    // LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest test,
    // which proves and locks in "a timeout means give up entirely" for THAT
    // bridge). This bridge's own SubmitAndWait() deliberately does NOT clear
    // m_requested on either path - m_requested is cleared in EXACTLY ONE
    // place in this whole class: HERE, in FulfillPending(). Copying
    // EngineCommandBridge's behavior verbatim would be a real, easy-to-miss
    // bug: TryPeekPendingProjectName() (below) checks m_requested to decide
    // whether anything is pending - if a timeout cleared it, the main
    // thread's own drain point would find NOTHING pending the very next
    // time it looks, and the hot-reload cycle this network request already
    // triggered would NEVER ACTUALLY RUN, silently contradicting this whole
    // class's own documented purpose (a timed-out HTTP caller has simply
    // stopped waiting; the underlying cycle MUST still run to completion on
    // the main thread regardless - GET /project_assembly/hot_reload/status
    // is the correct way to observe its real outcome afterward).
    void FulfillPending();

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    std::string m_projectName;
};

} // namespace gte
