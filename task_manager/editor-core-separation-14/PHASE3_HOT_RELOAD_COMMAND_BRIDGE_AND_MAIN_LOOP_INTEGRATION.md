# PHASE3 — Hot Reload Command Bridge, Main-Loop Integration Point, and the Temporary Orchestrator Stub

Parent: `PHASE0_MASTER_STRATEGY.md`.
Depends on: nothing from PHASE1/PHASE2 directly (independent code area),
but all three phases converge in PHASE4.
Blocks: PHASE4 (replaces this phase's temporary `PerformProjectAssemblyHotReload()`
body with the real sequence — the SIGNATURE and every piece of wiring
around it must already exist and compile before that).

---

## STEP 1 — The Goal

A new, purpose-built cross-thread bridge
(`ProjectAssemblyHotReloadCommandBridge`) lets a network-thread HTTP route
hand off "run a hot reload for project X" to the main thread and block
until it is done (mirrors `EngineCommandBridge`'s proven
request/condition-variable shape, but with its own long-timeout, single
-purpose payload — reusing `EngineCommandBridge` itself is explicitly
wrong, per the external plan's own reasoning: that bridge's result type is
tuned for small, same-frame operations). `EditorHost` gains this bridge as
a new member and ONE new `Run()` drain point, placed immediately after the
existing `m_commandBridge.TryPeekPendingCommandRequest()` block. A new file,
`src/Core/Plugins/ProjectAssemblyHotReload.h/.cpp`, declares the REAL,
PERMANENT signature of `PerformProjectAssemblyHotReload()` with a
TEMPORARY, honest, minimal body — so this phase's own wiring compiles and
is smoke-testable end-to-end BEFORE PHASE4's real logic exists (mirrors
this whole codebase's own convention of a phase leaving one clearly-labeled
placeholder for the very next phase to replace, e.g.
`TriggerHotReload()`'s own `editor-core-separation-12` PHASE2 placeholder).

## STEP 2 — The Situation

Confirmed, current, `src/Application/EngineCommandBridge.h` — the
`SubmitAndWait(request, timeoutMilliseconds=3000)` /
`TryPeekPendingCommandRequest()` / `FulfillCommand()` shape this phase
mirrors. Confirmed, current, `src/Editor/EditorHost.h` lines 154-164 — the
bridge-member declaration block, each declared BEFORE `m_networkServer`
(line 170) so its address is available for that constructor. Confirmed,
current, `src/Editor/EditorHost.cpp` lines 433-443 — the exact drain-point
shape (`if (const std::optional<...> request = bridge.TryPeekPendingCommandRequest())
{ ... bridge.FulfillCommand(result); }`) this phase's own new drain point
mirrors. Confirmed, current, `NetworkServer`'s constructor takes 8
parameters ending in `IHotReloadDebugCapability* hotReloadDebugCapability`
(`NetworkServer.cpp` lines 1376-1389) — **this phase does NOT add a 9th
constructor parameter**: the new bridge is reached indirectly, through
`EditorHotReloadDebugCapability` (which already has the route wired to it),
not through a brand-new `NetworkServer` constructor argument — see 3.3
below for exactly how.

## STEP 3 — The Plan

### 3.1 — `ProjectAssemblyHotReloadCommandBridge`

New file `src/Application/ProjectAssemblyHotReloadCommandBridge.h` (+ a
tiny `.cpp` if the implementation needs one — a header-only
`std::mutex`/`std::condition_variable`-based class is also acceptable,
follow whichever this codebase's own `EngineCommandBridge` convention
prefers; `EngineCommandBridge.h`/`.cpp` are split, so mirror that split).
**Add both new files to the root `CMakeLists.txt`'s `gte_core` source list**
(the `add_library(gte_core STATIC ...)` block near `src/Application/
EngineCommandBridge.h/.cpp`'s own existing entries) — this list is
hand-maintained, NOT a `file(GLOB ...)`, a recurring, explicitly-documented
mistake in prior campaigns (see 3.7 below for the identical reminder about
the new TEST file):

```cpp
#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

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
    void FulfillPending();

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    std::string m_projectName;
};

} // namespace gte
```

Implementation (`.cpp`) — **read this carefully: it is NOT a line-for-line
mirror of `EngineCommandBridge`, despite looking almost identical** —
`SubmitAndWait` locks, if `m_requested` is already true returns
`{alreadyPending=true}` immediately; otherwise sets `m_requested=true`,
`m_projectName=projectName`, `m_fulfilled=false`, waits on the condition
variable (with the timeout) for `m_fulfilled`, then:
- **On success** (`m_fulfilled` became true before the timeout): returns
  `{}`. Do **NOT** also clear `m_requested` here — `FulfillPending()` (below)
  already cleared it, under the same lock, before it ever called
  `notify_one()`, so by the time `wait_for()` returns true this thread has
  already re-acquired the lock and observed that fresh state. Clearing it a
  second time here would be harmless in isolation, but is deliberately
  omitted so there is exactly ONE place in this whole class that ever writes
  `m_requested = false` — simpler to reason about, and impossible to get
  half-updated.
- **On timeout**: returns `{timedOut=true}` and, **critically, deliberately
  does NOT clear `m_requested`** — this is the ONE load-bearing difference
  from `EngineCommandBridge::SubmitAndWait()`, which DOES clear its own
  equivalent flag on timeout (confirmed,
  `tests/Application/EngineCommandBridgeTests.cpp`'s own
  `LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest` test
  proves and locks in that exact "timeout means give up entirely" behavior
  for THAT bridge). Copying that behavior here verbatim would be a real,
  easy-to-miss bug: `TryPeekPendingProjectName()` (below) checks
  `m_requested` to decide whether anything is pending — if a timeout
  cleared it, the main thread's own drain point (3.3) would find NOTHING
  pending the very next time it looks, and the hot-reload cycle this
  network request already triggered would NEVER ACTUALLY RUN, silently
  contradicting this whole class's own documented purpose (a timed-out HTTP
  caller has simply stopped waiting; the underlying cycle MUST still run to
  completion on the main thread regardless — `GET
  /project_assembly/hot_reload/status` is the correct way to observe its
  real outcome afterward). `TryPeekPendingProjectName()` therefore keeps
  returning the still-pending project name for as long as it takes the main
  thread to actually reach its drain point, however long after the original
  caller gave up - and a SECOND `SubmitAndWait()` call (for the same or a
  different project) correctly observes `alreadyPending=true` for that
  entire stretch too, which is exactly the right behavior (only one
  hot-reload cycle can genuinely run at a time). `FulfillPending()` is what
  finally clears `m_requested` once the main thread genuinely finishes the
  cycle - a safe no-op if nothing is pending (mirrors
  `EngineCommandBridge::FulfillCommand()`'s own defensive no-op shape for
  that one specific case). `TryPeekPendingProjectName()` itself never
  mutates anything (a pure peek, mirroring
  `EngineCommandBridge::TryPeekPendingCommandRequest()`'s own identical
  "atomically check-and-copy, never a separate IsPending()+fetch pair"
  precedent).

### 3.2 — `EditorHost` wiring

`EditorHost.h`: add `#include "../Application/ProjectAssemblyHotReloadCommandBridge.h"`
near the other bridge includes (line ~21 area); add a new member in the
SAME declared-before-`m_networkServer` group (after
`m_renderGraphControlCommandBridge`, line 164):

```cpp
// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE3 - the cross-thread hand-off for
// PerformProjectAssemblyHotReload(). Same "declared BEFORE
// m_networkServer" placement reasoning as every other bridge above -
// this one, however, is NOT itself handed into NetworkServer's
// constructor (see EditorHotReloadDebugCapability::
// SetHotReloadCommandBridge(), this same phase, for how the route reaches
// it instead).
ProjectAssemblyHotReloadCommandBridge m_hotReloadCommandBridge;
```

### 3.3 — `EditorHotReloadDebugCapability` reaches the bridge

Rather than adding a 9th `NetworkServer` constructor parameter (the route
handler already holds `hotReloadDebugCapability`, and that object is the
one whose `TriggerHotReload()` needs to reach the bridge — adding a second,
parallel pointer into `NetworkServer` for the exact same route would be
pure duplication), give `EditorHotReloadDebugCapability` a new setter,
mirroring its own EXISTING `SetProjectAssemblyHost()` precedent exactly
(`EditorHotReloadDebugCapability.h` line 47):

```cpp
// EditorHotReloadDebugCapability.h, public section, after SetProjectAssemblyHost():
void SetHotReloadCommandBridge(ProjectAssemblyHotReloadCommandBridge& bridge) noexcept;

private:
    ProjectAssemblyHotReloadCommandBridge* m_hotReloadCommandBridge = nullptr;
```

`.cpp`:

```cpp
void EditorHotReloadDebugCapability::SetHotReloadCommandBridge(ProjectAssemblyHotReloadCommandBridge& bridge) noexcept
{
    m_hotReloadCommandBridge = &bridge;
}

bool EditorHotReloadDebugCapability::TriggerHotReload(const std::string& projectName)
{
    if (m_hotReloadCommandBridge == nullptr) {
        return false; // Should never happen in real production wiring - see SetHotReloadCommandBridge()'s own call-ordering guarantee below.
    }
    const ProjectAssemblyHotReloadCommandBridge::SubmitResult submit = m_hotReloadCommandBridge->SubmitAndWait(projectName);
    if (submit.alreadyPending) {
        return false; // Mirrors TriggerCompileOnly()'s own "already in progress -> false" contract.
    }
    if (submit.timedOut) {
        // The cycle is still running on the main thread (see SubmitAndWait()'s
        // own doc comment) - this HTTP caller simply stopped waiting. Report
        // this honestly as "not yet done from this caller's point of view";
        // the route handler (Step 3.5) still queries GetHotReloadStatus()
        // separately to build ITS OWN response either way.
        return false;
    }
    return true;
}
```

Wire the setter call in `EditorHost.cpp`'s constructor body, immediately
after the existing `s_editorHotReloadDebugCapability.SetProjectAssemblyHost(...)`
call (line 220):

```cpp
s_editorHotReloadDebugCapability.SetHotReloadCommandBridge(m_hotReloadCommandBridge);
```

(`EditorHotReloadDebugCapability.h` needs a forward declaration of
`ProjectAssemblyHotReloadCommandBridge` added alongside its existing
`class ProjectAssemblyHost;` forward declaration, line 6.)

### 3.4 — The new `ProjectAssemblyHotReload.h/.cpp` file (temporary body)

New file, `src/Core/Plugins/ProjectAssemblyHotReload.h`. **Add both this
file and its `.cpp` to the root `CMakeLists.txt`'s `gte_core` source list**
(same hand-maintained-list reminder as 3.1 above — this folder,
`src/Core/Plugins/`, is exactly where `ProjectAssemblyHost.h/.cpp`'s own
entries already sit in that same list):

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace gte {

class Core;
class Renderer;
class EditorHost;

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3). THE orchestrator - see PHASE4_ORCHESTRATOR_AND_ROUTE_WIRING.md
// for this function's REAL, permanent body. PHASE3 (this file) only
// declares the PERMANENT signature and gives it a TEMPORARY, honest,
// minimal body so EditorHost::Run()'s new drain point (PHASE3) and every
// piece of wiring around it can compile and be smoke-tested THIS phase,
// before PHASE4's real pause/backup/unload/compile/reload logic exists.
// Called SYNCHRONOUSLY from EditorHost::Run()'s own main loop - blocks the
// calling (main) thread for its entire duration once PHASE4 replaces this
// body (LDD-HR4). Targets exactly one project (LDD-HR5).
// `outputDirectory`/`buildDirectory` are RESOLVED BY THE CALLER
// (EditorHost::Run()'s drain point, gte_editor-tier) and handed in as plain
// values - this function must NEVER call gte::ExecutableDirectory() itself:
// this file lives in src/Core/Plugins/, compiled into gte_core (see every
// OTHER file in this same folder in CMakeLists.txt's gte_core source list),
// and gte::ExecutableDirectory() is gte_editor-tier (defined only in
// src/Editor/ProjectRootPath.cpp, never linked into gte_core alone) -
// mirrors ResolveProjectAssemblyOutputDirectory()/ResolveCMakeBuildDirectory()'s
// own identical "take the resolved directory as an explicit parameter,
// never resolve it internally" precedent (ProjectAssemblyBuildRunner.h),
// for the exact same layering reason.
void PerformProjectAssemblyHotReload(const std::string& projectName, Core& core, Renderer& renderer,
    EditorHost* editorHost, const std::filesystem::path& outputDirectory, const std::filesystem::path& buildDirectory);

} // namespace gte
```

`.cpp`, TEMPORARY body (PHASE4 replaces everything inside this function —
the signature never changes again after this phase):

```cpp
#include "ProjectAssemblyHotReload.h"
#include "../Logging.h"
#include "ProjectAssemblyHotReloadDebugStatus.h"

namespace gte {

void PerformProjectAssemblyHotReload(const std::string& projectName, Core& /*core*/, Renderer& /*renderer*/,
    EditorHost* /*editorHost*/, const std::filesystem::path& /*outputDirectory*/,
    const std::filesystem::path& /*buildDirectory*/)
{
    // editor-core-separation-14 campaign, PHASE3 - TEMPORARY body, replaced
    // in full by PHASE4. Proves the whole cross-thread wiring chain
    // (bridge -> EditorHost::Run() drain point -> this function -> status
    // singleton -> HTTP response) end-to-end THIS phase, without yet
    // touching backup/unload/compile/reload. outputDirectory/buildDirectory
    // are unused this phase (PHASE4 is the first body to actually need
    // them) - already threaded through the signature now so PHASE4 never
    // has to touch any CALL SITE, only this function's own body.
    GTE_LOG_WARNING("ProjectAssemblyHotReload",
        "PerformProjectAssemblyHotReload('" + projectName + "') called - PHASE4 has not replaced this temporary body yet; no real reload occurred.");
    ProjectAssemblyHotReloadDebugStatus::Instance().Set("CapturingState", projectName);
    ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure", "PHASE3 temporary stub - PHASE4 not yet implemented");
}

} // namespace gte
```

### 3.5 — `EditorHost::Run()`'s new drain point

Insert IMMEDIATELY AFTER the existing
`m_commandBridge.TryPeekPendingCommandRequest()` block (`EditorHost.cpp`,
right after line 443, before `const Uint64 nowTicksNs = ...` at line 445):

```cpp
// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE3 - drained at most once per frame, exactly like the
// existing EngineCommandBridge block immediately above. Unlike that
// bridge, servicing this request BLOCKS this same thread for the ENTIRE
// duration of PerformProjectAssemblyHotReload() once PHASE4 replaces its
// temporary body (LDD-HR4) - this is intentional: the whole point of this
// feature is a hard, synchronous freeze. No other per-frame work below
// this point runs until it returns.
if (const std::optional<std::string> requestedProject = m_hotReloadCommandBridge.TryPeekPendingProjectName()) {
    GTE_PROFILE_SCOPE("EditorHost::PerformProjectAssemblyHotReload");
    // Resolved HERE, on the main thread (gte_editor-tier - this file already
    // includes ProjectRootPath.h/ProjectAssemblyBuildRunner.h for its own
    // LoadProjectAssemblies() call above), and passed into the orchestrator
    // as plain std::filesystem::path VALUES - PerformProjectAssemblyHotReload()
    // itself lives in src/Core/Plugins/ (gte_core-tier, see that header's own
    // doc comment) and must NEVER call gte::ExecutableDirectory() itself
    // (gte_editor-tier, defined only in ProjectRootPath.cpp) - see
    // PHASE0_MASTER_STRATEGY.md, Section 2.2 item 6, for the full layering
    // hazard this avoids.
    const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
    const std::filesystem::path buildDirectory = ResolveCMakeBuildDirectory(gte::ExecutableDirectory());
    PerformProjectAssemblyHotReload(*requestedProject, m_core, m_renderer, this, outputDirectory, buildDirectory);
    m_hotReloadCommandBridge.FulfillPending();
}
```

Add `#include "../Core/Plugins/ProjectAssemblyHotReload.h"` to
`EditorHost.cpp`'s include block.

### 3.6 — Smoke test this phase

Live check (background-launched `GreatTamanaEditor.exe`):
`POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` should now
return in a reasonable time (not `501` anymore — `TriggerHotReload()` now
genuinely calls into the bridge and waits) with whatever the ROUTE
HANDLER'S CURRENT (still-PHASE1-of-BIG-STEP1, unchanged this phase) body
produces. **Spelled out exactly, so this isn't mistaken for a real failure**:
walking through the OLD, still-unmodified route handler this phase leaves in
place, a fully-serviced cycle (bridge submitted -> drain point ran the
TEMPORARY body -> `FulfillPending()` called -> `TriggerHotReload()` returns
`true`) falls through that handler's OWN pre-existing, until-now-unreachable
`else` branch — **HTTP `500`, body `"unexpected: TriggerHotReload() reported
success in a build with no real orchestrator"`** — this looks alarming but
is the CORRECT, EXPECTED response for this phase specifically (PHASE4, not
this phase, replaces this handler with the real one). Confirm via
`GET /project_assembly/hot_reload/status` immediately after that the
reported `lastOutcome` is `"CriticalFailure"` with `lastErrorMessage`
containing `"PHASE3 temporary stub"` — this exact, deliberately-ugly result
(both the `500` and the `CriticalFailure`) IS the correct, expected proof
that the whole chain (bridge -> drain point -> orchestrator function ->
status singleton) is wired correctly end-to-end, with zero risk of silently
looking like a false-positive "real" success.

### 3.7 — Tier-1 tests for `ProjectAssemblyHotReloadCommandBridge`

A brand-new, concurrency-sensitive class needs its own direct test coverage
(`AGENTS.md`'s Testability rule) — mirroring
`tests/Application/EngineCommandBridgeTests.cpp`'s own proven shape exactly,
but this bridge's tests must specifically prove the ONE behavior that is
DIFFERENT from that file's own already-locked-in precedent (see 3.1's own
"read this carefully" note above). New file,
`tests/Application/ProjectAssemblyHotReloadCommandBridgeTests.cpp`:

- `RequestWithNoServicerTimesOutButStaysPending` — `SubmitAndWait("Foo", 100)`
  with nothing ever peeking/fulfilling it; assert `timedOut == true`, then
  (this is the important, DIFFERENT-from-`EngineCommandBridge` assertion)
  call `TryPeekPendingProjectName()` afterward and assert it still returns
  `"Foo"` — proving the timeout did NOT silently drop the request.
- `FulfillPendingAfterATimeoutClearsTheSlotForAFreshRequest` — repeats the
  above, then calls `FulfillPending()` (simulating the main thread finally
  reaching its drain point late), then asserts `TryPeekPendingProjectName()`
  now returns `std::nullopt`, and that a brand-new `SubmitAndWait()` call for
  a different project name afterward is serviced normally (mirrors
  `EngineCommandBridgeTest.LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest`'s
  own shape, adapted to this bridge's own different timeout semantics).
- `FulfilledBeforeTimeoutReturnsCleanlyWithNeitherFlagSet` — a background
  `std::thread` waits for `TryPeekPendingProjectName()` to become non-empty,
  asserts the peeked name is correct, then calls `FulfillPending()`; the
  main test thread's own `SubmitAndWait(name, 5000)` call must return with
  both `alreadyPending`/`timedOut` false, well before the 5-second timeout
  (mirrors `EngineCommandBridgeTest.FulfilledRequestReturnsExactResultQuickly`).
- `SecondConcurrentRequestReturnsAlreadyPendingImmediately` — mirrors
  `EngineCommandBridgeTest.SecondConcurrentRequestReturnsAlreadyPendingImmediately`
  exactly, adapted to this bridge's single `std::string` payload.
- `FulfillPendingWithNothingPendingIsASafeNoOp` — calling `FulfillPending()`
  on a freshly-constructed bridge (nothing ever submitted) must not crash or
  assert.

Add this new file to `tests/CMakeLists.txt`'s own hand-maintained source
list (this codebase's own test suite is NOT a `file(GLOB ...)` — forgetting
this exact step has been a recurring, explicitly-documented mistake across
multiple prior campaigns, e.g. `editor-core-separation-11`'s own PHASE5/
PHASE6 completion reports).

## Definition of Done — this phase only

- [ ] `ProjectAssemblyHotReloadCommandBridge` exists, compiles, mirrors
      `EngineCommandBridge`'s proven shape; the new 3.7 Tier-1 tests all
      pass, in particular confirming a timed-out `SubmitAndWait()` leaves
      the request observable via `TryPeekPendingProjectName()` afterward
      (the one deliberate behavior difference from `EngineCommandBridge`).
- [ ] `EditorHost` owns it as a new member, in the correct declared-before-
      `m_networkServer` position; `EditorHotReloadDebugCapability` reaches
      it via the new `SetHotReloadCommandBridge()` setter, wired once from
      `EditorHost`'s constructor body.
- [ ] `src/Core/Plugins/ProjectAssemblyHotReload.h/.cpp` exist with the
      PERMANENT signature and the TEMPORARY body above.
- [ ] `EditorHost::Run()`'s new drain point exists, placed exactly where
      specified.
- [ ] The live smoke test (3.6) passes: `POST /project_assembly/hot_reload`
      no longer answers a bare `501`; `GET .../status` shows the temporary
      `"CriticalFailure"`/"PHASE3 temporary stub" outcome, proving the
      whole chain is wired.
- [ ] Incremental build succeeds (`gte_core`, `gte_editor`,
      `GreatTamanaEditor`, `GreatTamanaEngineTests`), zero new warnings.
