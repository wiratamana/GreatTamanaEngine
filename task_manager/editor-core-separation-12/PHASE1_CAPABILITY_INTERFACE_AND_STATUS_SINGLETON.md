# editor-core-separation-12 — PHASE1: Capability Interface, Push-Status
Singleton, and Shared Engine-State Mutex

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first, especially Section 3.1
(the three corrections) before writing any code below.

Depends on: nothing (first implementation phase).
Blocks: PHASE2 (needs every type this phase declares).

---

## Step 1: The Goal

Declare every new pure-data type and every new small, self-contained utility
class this whole campaign needs, with **zero dependency on anything
BIG-STEP 2/3/4 will ever build**, and zero dependency on the Editor module
(this phase's new files are all `gte_core`-tier, added to `CMakeLists.txt`'s
`gte_core` source block). By the end of this phase:

- `IHotReloadDebugCapability` exists in `src/Core/EditorCapabilities.h`,
  fully declared, matching PHASE0's Correction 1/2 (not BIG-STEP 1's literal
  text) for `BuildSceneSnapshotJson`'s signature.
- `ProjectAssemblyHotReloadDebugStatus` exists, a genuine Meyers singleton,
  independently compilable and unit-testable, with its `Set()`/`Finish()`
  call sites left for a FUTURE BIG-STEP 3 to add (this phase adds none).
- `HotReloadEngineStateMutex` exists — the new shared mutex from PHASE0
  Correction 2.
- `EngineCommandKind::GetSceneSnapshot` plus its request/result payload
  structs exist in `Application/EngineCommandBridge.h` and
  `Game/EngineCommandResults.h`, ready for PHASE2 to actually dispatch.

## Step 2: The Situation

Before this phase, `src/Core/EditorCapabilities.h` has exactly
`ISceneIOCapability` and `ILogQueryCapability` (both fully read in PHASE0's
investigation — see that file's Step 2). Both already forward-declare
`class Game;` and `class Renderer;` at the top of the file (confirmed, lines
58-59) — this phase's new interface reuses that same forward declaration,
adds nothing new there.

`Application/EngineCommandBridge.h` already has a 7-value
`EngineCommandKind` enum (`InstantiatePrimitive`, `DeleteEntity`,
`SetEntityTrs`, `InstantiateLight`, `InstantiateMeshAsset`, `SaveScene`,
`LoadScene` — confirmed, current text, lines 37-55) with a fixed,
established pattern for adding a new one: a new enumerator, a new
`XxxCommand` payload struct (or reuse an empty one if no payload is
needed), a new field on `EngineCommandRequest`, a new `XxxOutcome` type in
`Game/EngineCommandResults.h`, and a new field on `EngineCommandResult`.
This phase adds the eighth value, `GetSceneSnapshot`, following this exact,
established shape.

## Step 3: The Plan

### 3.1 — `src/Core/EditorCapabilities.h` — append `IHotReloadDebugCapability`

Insert this new interface AFTER `ILogQueryCapability`'s closing brace (the
file's last interface today), BEFORE the final `} // namespace gte`:

```cpp
// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1) - answers "what is the live, ground-truth state of the
// Project Assembly Hot Reload system RIGHT NOW", and lets an external HTTP
// caller (gte_send_request) trigger a bare compile, or (once a future
// BIG-STEP 3 campaign fills in the real body) a full compile+reload cycle.
// Mirrors ISceneIOCapability/ILogQueryCapability's own "gte_core-tier
// NetworkServer.cpp holds only a nullable pointer" contract exactly -
// nullptr means every route backed by this interface answers 503, never
// crashes.
//
// DEVIATION from this feature's own external design doc
// (HOTRELOAD_BIGSTEP_01_LIVE_DEBUG_AND_COMPILE_RELOAD_TRIGGERS_2026-09-28.txt,
// Section 3): that doc sketches a plain, no-argument
// `virtual std::string GetSceneSnapshotJson() const = 0;`, implying a
// lock-free, direct network-thread read of the live ECS Registry. This is
// UNSAFE - the Registry is mutated every frame by the main thread with no
// synchronization of its own (see AGENTS.md, "Entity-Component-System").
// `BuildSceneSnapshotJson(Game&)` below is instead called ONLY from the
// main thread (via a new EngineCommandBridge command - see
// Application/EngineCommandDispatch.cpp), exactly mirroring how
// ISceneIOCapability::SaveScene/LoadScene are already, correctly, only ever
// called with a live Game& handed to them by that SAME main-thread-only
// dispatch path. See task_manager/editor-core-separation-12/
// PHASE0_MASTER_STRATEGY.md, Section 3.1, Correction 1, for the full
// reasoning.
class IHotReloadDebugCapability {
public:
    virtual ~IHotReloadDebugCapability() = default;

    // "Idle" whenever no cycle is currently running (always "Idle" until a
    // future BIG-STEP 3 campaign starts calling
    // ProjectAssemblyHotReloadDebugStatus::Set()/Finish()). lastOutcome/
    // lastErrorMessage describe the MOST RECENTLY COMPLETED cycle (persist
    // across the transition back to Idle) - both "" until the first cycle
    // ever completes.
    struct Status {
        std::string phase = "Idle";
        std::string projectName;
        std::uint64_t cycleId = 0;
        std::uint64_t phaseElapsedMilliseconds = 0;
        std::string lastOutcome;       // "" | "Success" | "RolledBack" | "CriticalFailure"
        std::string lastErrorMessage;  // "" unless lastOutcome needs explaining
    };
    virtual Status GetHotReloadStatus() const = 0;

    // Placeholder-shaped until a future BIG-STEP 2 campaign builds the real
    // ProjectAssemblyRegistrationLedger class - this campaign's own
    // implementation (PHASE2) always returns every list empty, never an
    // error.
    struct LedgerEntry {
        std::vector<std::string> renderPassNames;
        std::vector<std::string> panelNames;
        std::vector<std::string> componentTypeNames;
    };
    virtual LedgerEntry GetLedgerEntry(const std::string& projectName) const = 0;

    // Placeholder until a future BIG-STEP 2 campaign adds a real
    // GetLoadedAssemblyFileNames() accessor to ProjectAssemblyHost itself -
    // this campaign's own implementation (PHASE2) always returns an empty
    // vector, never an error.
    virtual std::vector<std::string> GetLoadedAssemblyFileNames() const = 0;

    // Genuinely real, live, today - a thin wrapper over
    // ComponentTypeRegistry::Instance().AllSortedByTypeName().
    virtual std::vector<std::string> GetRegisteredComponentTypeNames() const = 0;

    // Genuinely real, live, today - builds the SAME generic SceneDocument
    // JSON File > Save Scene/POST /save_scene already produce, but returns
    // it in-memory (never written to disk). MUST be called only from the
    // main thread with a live Game& (see this interface's own header
    // comment above, and Application/EngineCommandDispatch.cpp's new
    // EngineCommandKind::GetSceneSnapshot case, PHASE2) - never called
    // directly from a NetworkServer.cpp route handler.
    virtual std::string BuildSceneSnapshotJson(Game& game) = 0;

    // Fire-and-forget - calls the EXISTING TriggerProjectAssemblyCompile()
    // directly (already backgrounded, already thread-safe against
    // concurrent triggers for the SAME project). Returns false only if the
    // in-flight guard rejects it (a build for this project is already
    // running) - never blocks waiting for the compile itself to finish;
    // the caller polls GET /get_logs to watch it happen.
    virtual bool TriggerCompileOnly(const std::string& projectName) = 0;

    // THIS campaign's OWN implementation (PHASE2) is a permanent placeholder
    // for this whole campaign's lifetime - always returns false, does
    // nothing else. A future BIG-STEP 3 campaign replaces ONLY this
    // method's body - this interface's own signature never changes for
    // that.
    virtual bool TriggerHotReload(const std::string& projectName) = 0;
};

} // namespace gte
```

(The final `} // namespace gte` above already exists in the file today —
this snippet shows it only so the insertion point is unambiguous; do not
duplicate it.)

Add `#include <cstdint>` if not already present (it already is, confirmed,
line 51) — no new includes needed at all for this interface.

### 3.2 — New file: `src/Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h`

```cpp
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
```

### 3.3 — New file: `src/Core/Plugins/ProjectAssemblyHotReloadDebugStatus.cpp`

```cpp
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
```

Note: `phaseElapsedMilliseconds` is deliberately left at its default (`0`)
by this campaign — computing it correctly needs a `steady_clock` timestamp
recorded in `Set()` and read in `GetSnapshot()`; since NOTHING calls `Set()`
during this campaign, wiring that up now would be untestable dead code. Add
a one-line TODO comment in `Set()` for a future BIG-STEP 3 campaign instead
of guessing at an implementation nobody can verify yet.

### 3.4 — New file: `src/Core/Plugins/HotReloadEngineStateMutex.h`

```cpp
#pragma once

// src/Core/Plugins/HotReloadEngineStateMutex.h
//
// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1), PHASE1. A single, process-wide mutex guarding read/write
// access to every piece of engine state a future BIG-STEP 3 hot-reload
// cycle will mutate OUTSIDE the per-frame ECS Registry itself -
// specifically ComponentTypeRegistry, EditorPanelRegistry, and
// ProjectAssemblyHost's own loaded-assembly list/registration ledger (once
// a future BIG-STEP 2 campaign builds that ledger).
//
// WHY A PLAIN MUTEX IS CORRECT HERE (see PHASE0_MASTER_STRATEGY.md, Section
// 3.1, Correction 2, for the full reasoning): today, and for this whole
// campaign's lifetime, ComponentTypeRegistry/EditorPanelRegistry/
// ProjectAssemblyHost are mutated ONLY once, at EditorHost construction time
// (LoadPlugins()/LoadProjectAssemblies()), strictly BEFORE
// NetworkServer::Start() is ever called - so there is, in fact, no real race
// for THIS campaign to guard against yet. This mutex is added now, proactively,
// so EditorHotReloadDebugCapability::GetLedgerEntry()/
// GetLoadedAssemblyFileNames()/GetRegisteredComponentTypeNames() (PHASE2)
// already take it before every read, and so a future BIG-STEP 2/3 campaign's
// own mutating code has an unambiguous, pre-existing, documented lock to
// take too, rather than discovering the need for one only after a live
// race is reported. Deliberately NOT applied to the ECS Registry itself -
// see HOTRELOAD's own GetSceneSnapshotJson/BuildSceneSnapshotJson reasoning
// (Core/EditorCapabilities.h's own IHotReloadDebugCapability doc comment)
// for why that needs the EngineCommandBridge instead, not this mutex.
//
// A FUTURE BIG-STEP 2/3 CAMPAIGN MUST lock this same mutex around every
// ComponentTypeRegistry::RegisterDescriptor()/UnregisterDescriptor(),
// EditorPanelRegistry::RegisterPluginPanel()/UnregisterPluginPanel(), and
// ProjectAssemblyHost load/unload call it makes during a reload cycle - this
// file's own comment is the permanent, citeable reminder that requirement
// exists.

#include <mutex>

namespace gte {

std::mutex& GetHotReloadEngineStateMutex();

} // namespace gte
```

### 3.5 — New file: `src/Core/Plugins/HotReloadEngineStateMutex.cpp`

```cpp
#include "HotReloadEngineStateMutex.h"

namespace gte {

std::mutex& GetHotReloadEngineStateMutex()
{
    static std::mutex mutex;
    return mutex;
}

} // namespace gte
```

### 3.6 — `src/Application/EngineCommandBridge.h` — add `GetSceneSnapshot`

Add the eighth enumerator, right after `LoadScene,`:

```cpp
    SaveScene,
    LoadScene,
    // editor-core-separation-12 campaign, PHASE1 (Project Assembly Hot
    // Reload plan, BIG-STEP 1) - reuses this SAME single-global-slot bridge
    // for a READ-ONLY ECS world/scene snapshot request. Needed because the
    // live ECS Registry is mutated every frame by the main thread with no
    // synchronization of its own - unlike GetHotReloadStatus/
    // GetLedgerEntry/GetLoadedAssemblyFileNames/
    // GetRegisteredComponentTypeNames (Core/EditorCapabilities.h's new
    // IHotReloadDebugCapability), which correctly bypass this bridge via a
    // dedicated mutex instead (see HotReloadEngineStateMutex.h).
    GetSceneSnapshot,
```

Add a new, empty payload struct right after `LoadSceneCommand`:

```cpp
// editor-core-separation-12 campaign, PHASE1 - no payload needed, the
// snapshot always covers the WHOLE live scene.
struct GetSceneSnapshotCommand {};
```

Add the new field to `EngineCommandRequest` (after `loadScene;`):

```cpp
    // editor-core-separation-12 campaign, PHASE1
    GetSceneSnapshotCommand getSceneSnapshot;
```

Add the new field to `EngineCommandResult` (after `loadScene;`):

```cpp
    // editor-core-separation-12 campaign, PHASE1
    GetSceneSnapshotOutcome getSceneSnapshot;
```

### 3.7 — `src/Game/EngineCommandResults.h` — add `GetSceneSnapshotOutcome`

Insert right after `LoadSceneOutcome`'s closing brace:

```cpp
// editor-core-separation-12 campaign, PHASE1 (Project Assembly Hot Reload
// plan, BIG-STEP 1) - outcome of one
// IHotReloadDebugCapability::BuildSceneSnapshotJson(Game&) call, wrapped for
// EngineCommandBridge/EngineCommandDispatch.cpp, mirroring SaveSceneOutcome/
// LoadSceneOutcome's exact shape immediately above. `editorAvailable ==
// false` means this build has no IHotReloadDebugCapability registered (a
// future Player host, or a build with the Editor module compiled out).
struct GetSceneSnapshotOutcome {
    bool success = false;
    bool editorAvailable = true;
    std::string errorMessage;
    std::string sceneJson; // meaningful only when success == true.
};
```

### 3.8 — CMakeLists.txt additions (do this now, not in PHASE3)

In the `gte_core` source list, immediately after the existing
`src/Core/Plugins/ProjectAssemblyBuildRunner.cpp` line (~351), add:

```
src/Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h
src/Core/Plugins/ProjectAssemblyHotReloadDebugStatus.cpp
src/Core/Plugins/HotReloadEngineStateMutex.h
src/Core/Plugins/HotReloadEngineStateMutex.cpp
```

### 3.9 — New Tier-1 test file: `tests/Core/Plugins/ProjectAssemblyHotReloadDebugStatusTests.cpp`

`ProjectAssemblyHotReloadDebugStatus` is pure logic over plain data + a
mutex — genuinely Tier-1-testable, per `AGENTS.md`'s own testability rule.
Add a small gtest file (mirroring
`tests/Core/Plugins/PluginHostFailurePathTests.cpp`'s own file placement)
covering:

- Fresh `Instance()` reports `phase == "Idle"`, `cycleId == 0`.
- `Set("Compiling", "Foo")` then `GetSnapshot()` reports
  `phase == "Compiling"`, `projectName == "Foo"`, `cycleId == 1`.
- A SECOND `Set("Unloading", "Foo")` (same cycle, no `Idle` in between)
  keeps `cycleId == 1` (does not increment mid-cycle).
- `Finish("Success", "")` resets `phase` to `"Idle"` and sets
  `lastOutcome == "Success"`.
- A subsequent `Set(...)` after `Finish()` increments `cycleId` to `2`.

Register this new file in `tests/CMakeLists.txt`'s own hand-maintained list
(mirroring where `PluginHostFailurePathTests.cpp` is listed).

## Definition of Done — this phase only

- [ ] `IHotReloadDebugCapability` compiles (header-only check: a translation
      unit that `#include`s `Core/EditorCapabilities.h` alone, nothing else,
      compiles).
- [ ] `ProjectAssemblyHotReloadDebugStatus`/`HotReloadEngineStateMutex`
      compile as part of `gte_core`.
- [ ] The new Tier-1 test file passes.
- [ ] `EngineCommandBridge.h`/`EngineCommandResults.h` compile (a full
      `gte_core` incremental build succeeds) — `EngineCommandDispatch.cpp`'s
      `switch` does NOT yet handle `GetSceneSnapshot` (PHASE2's job) —
      **confirm this produces, at most, a harmless non-exhaustive-switch
      compiler warning, never an error** (this codebase enables no
      `-Wswitch`/`-Werror` for its own code, per `AGENTS.md`'s own
      "Render Pass System" section's documented, pre-existing gap — the
      `switch` will simply do nothing for that one case for now, which is
      fine since nothing calls it with that kind yet).
