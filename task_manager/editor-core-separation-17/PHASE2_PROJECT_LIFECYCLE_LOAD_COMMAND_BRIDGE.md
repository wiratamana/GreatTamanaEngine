# PHASE2 — ProjectLifecycleLoadCommandBridge
## editor-core-separation-17 (On-Engine Project Workflow, BIG-STEP 3 "Open Project")

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first, especially Section 2.2
(the main-thread-deadlock finding this bridge exists to solve correctly).
Depends on: nothing structurally (independent of PHASE1's enum), sequenced
after it for a smaller, cleaner per-phase diff/report.
Blocks: PHASE3 (the capability's HTTP-facing method needs this bridge to
submit into).

---
## Step 1: The Goal

A new, dedicated, reviewed, thread-safe bridge that lets the
**network thread** (the future `POST /project_assembly/open_project` route
handler) ask the **main thread** to perform a real Project Assembly `.dll`
load, then blocks until that load genuinely finishes (or a short timeout
elapses), mirroring `AssetImportCommandBridge`'s own exact, proven shape —
per BIG-STEP 1's Finding 6 and the source document's own STEP 5.

## Step 2: The Situation

This codebase's own written house rule (`AssetImportCommandBridge.h`'s own
header comment, confirmed real, quoted verbatim in `PHASE0_MASTER_STRATEGY.md`
of `editor-core-separation-16`): a genuinely new cross-thread capability
gets **its own, new, dedicated bridge type**, never bolted onto
`EngineCommandBridge`'s existing enum. `AssetImportCommandBridge.h/.cpp`
(read in full during this campaign's own investigation) is the exact
template: single global slot guarded by one `std::mutex` +
`std::condition_variable`, `SubmitAndWait()` (network thread) returning a
`SubmitResult{ std::optional<Result> result; bool alreadyPending; bool
timedOut; }`, and `IsCommandPending()`/`TryPeekPendingCommandRequest()`/
`FulfillCommand()` (main thread only).

Unlike `AssetImportCommandBridge` (120000ms default timeout, since a real
asset import can be slow), this campaign's own load is a single
`LoadLibraryW()` + one project's own `GTE_RegisterProject` call — the
source document's own STEP 5 already settled this at **5000ms**, a
generous, safe default for an operation that should complete in well under
a second.

**Critical, load-bearing scope note** (this is what makes PHASE3's own
deadlock fix possible): this bridge is used **only** by the
network-thread-facing capability method
(`IProjectLifecycleCapability::OpenProjectAssembly()`). The ImGui-facing
method (`OpenProjectAssemblyOnMainThread()`, PHASE3) never touches this
bridge at all — it calls the shared load logic directly, since it is
already running on the same thread this bridge's own drain point runs on.
Do not "simplify" this bridge into something the ImGui path also uses —
that would silently reintroduce the exact deadlock `PHASE0_MASTER_STRATEGY.md`
Section 2.2 documents.

## Step 3: The Plan

### 3.1 — New header, `src/Application/ProjectLifecycleLoadCommandBridge.h`

```cpp
#pragma once

// src/Application/ProjectLifecycleLoadCommandBridge.h
//
// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE2. A new, dedicated cross-thread bridge - structurally
// IDENTICAL to AssetImportCommandBridge.h's own proven shape (single
// global slot, mutex + condition_variable, SubmitAndWait()/
// IsCommandPending()/TryPeekPendingCommandRequest()/FulfillCommand()) -
// mirrors that header's own explicit, written justification for why a
// genuinely new capability gets its own bridge type rather than a new
// EngineCommandKind bolted onto an existing one.
//
// USED ONLY by the network-thread-facing
// IProjectLifecycleCapability::OpenProjectAssembly() call path
// (Core/EditorCapabilities.h) - the ImGui-facing
// OpenProjectAssemblyOnMainThread() path NEVER submits into this bridge;
// it is already running on the same thread this bridge's own drain point
// (EditorHost::Run()) runs on, and calling SubmitAndWait() from there would
// deadlock the whole Editor (see PHASE0_MASTER_STRATEGY.md, Section 2.2,
// for the full reasoning). Owned by EditorHost (a plain member, mirrors
// m_hotReloadCommandBridge/m_assetImportCommandBridge's own placement),
// wired into EditorProjectLifecycleCapability via a setter (PHASE3),
// mirroring EditorHotReloadDebugCapability::SetHotReloadCommandBridge()'s
// own precedent exactly.

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

// Plain request payload - only ever needs the project's own name; the
// main-thread drain point resolves the real .dll paths itself
// (mirrors ProjectAssemblyHotReloadCommandBridge's own "only carries a
// project name, the main thread resolves paths" convention).
struct LoadProjectAssemblyCommandRequest {
    std::string projectName;
};

// Outcome of one load attempt.
struct LoadProjectAssemblyCommandResult {
    // True only if LoadOneProjectAssemblyFromExactPath() (the _Game.dll)
    // AND LoadOneProjectAssemblyFromExactPathIfExists() (the optional
    // _Editor.dll) both returned true - mirrors
    // ProjectAssemblyHotReload.cpp's own existing `&&`-chained success
    // check (lines ~176-177/204-205) exactly.
    bool loadSucceeded = false;
};

class ProjectLifecycleLoadCommandBridge {
public:
    ProjectLifecycleLoadCommandBridge() = default;
    ~ProjectLifecycleLoadCommandBridge() = default;

    ProjectLifecycleLoadCommandBridge(const ProjectLifecycleLoadCommandBridge&) = delete;
    ProjectLifecycleLoadCommandBridge& operator=(const ProjectLifecycleLoadCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        std::optional<LoadProjectAssemblyCommandResult> result;
        bool alreadyPending = false;
        bool timedOut = false;
    };
    // 5000ms default - a real Project Assembly load is a single
    // LoadLibraryW() + one GTE_RegisterProject call, never a multi-minute
    // operation (unlike the hot-reload bridge's own deliberately long
    // 600000ms default) - see this campaign's own PHASE0 file, Step 2.
    SubmitResult SubmitAndWait(LoadProjectAssemblyCommandRequest request, int timeoutMilliseconds = 5000);

    // --- Called from the MAIN thread (EditorHost::Run()) only -----------

    bool IsCommandPending() const;
    std::optional<LoadProjectAssemblyCommandRequest> TryPeekPendingCommandRequest() const;
    void FulfillCommand(LoadProjectAssemblyCommandResult result);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    LoadProjectAssemblyCommandRequest m_request;
    LoadProjectAssemblyCommandResult m_result;
};

} // namespace gte
```

### 3.2 — New source, `ProjectLifecycleLoadCommandBridge.cpp`

Byte-for-byte structural copy of `AssetImportCommandBridge.cpp`'s own 4
functions, substituting the new types
(`LoadProjectAssemblyCommandRequest`/`Result` for
`AssetImportCommandRequest`/`AssetImportCommandResult`,
`ProjectLifecycleLoadCommandBridge` for `AssetImportCommandBridge`) — no
behavioral difference at all, including:
  - `SubmitAndWait()`'s "already pending" immediate-return-without-waiting
    branch.
  - The `wait_for()` + `m_requested = false` reset on **both** the
    fulfilled and the timed-out path (this bridge mirrors
    `AssetImportCommandBridge`'s reset-on-both-paths behavior, **not**
    `ProjectAssemblyHotReloadCommandBridge`'s deliberately different
    "only `FulfillPending()` ever clears it" behavior — the two existing
    bridges genuinely differ here on purpose, per each one's own header
    comment; this new bridge needs a synchronous `Result` value returned to
    its own caller, exactly like `AssetImportCommandBridge`, so it must
    copy THAT one's semantics, not the hot-reload one's).
  - `FulfillCommand()`'s "nobody is actually waiting" inert no-op guard.

### 3.3 — New Tier-1 test file

`tests/Application/ProjectLifecycleLoadCommandBridgeTests.cpp`, mirroring
`AssetImportCommandBridgeTests.cpp`'s own exact test list structurally
(open that file first, copy its own test names/shapes 1:1 with the new
types substituted). At minimum:

1. A basic submit-from-one-thread + fulfill-from-another-thread round trip
   returns the exact `loadSucceeded` value the fulfiller supplied.
2. `TryPeekPendingCommandRequest()` returns `std::nullopt` when nothing is
   pending, and the exact submitted request while one is.
3. A second `SubmitAndWait()` call while one is already in flight returns
   `alreadyPending = true` immediately, without blocking.
4. A `SubmitAndWait()` call that times out (nothing ever calls
   `FulfillCommand()`) returns `timedOut = true` within roughly the
   requested `timeoutMilliseconds` (use a short, e.g. 50ms, timeout in the
   test itself — never the real 5000ms default, to keep the test suite
   fast).
5. A late `FulfillCommand()` call arriving after a timeout is a safe,
   inert no-op (does not corrupt a subsequent, fresh `SubmitAndWait()` for
   a new request) — mirrors `AssetImportCommandBridgeTests.cpp`'s own
   identical test if present, or `EngineCommandBridgeTests.cpp`'s
   `LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest` test if
   `AssetImportCommandBridgeTests.cpp` does not have its own copy — check
   both files' real contents before writing this test's own name/body.

Register the new file in `tests/CMakeLists.txt`'s `Application/...` block,
immediately alongside `AssetImportCommandBridgeTests.cpp`'s own real,
existing entry.

### 3.4 — Definition of done

- [ ] `ProjectLifecycleLoadCommandBridge.h/.cpp` compile as a new,
      standalone `gte_application`-tier pair (same tier/library target as
      `AssetImportCommandBridge.cpp` — confirm which CMake target/source
      list that file belongs to and add the new file to the exact same
      list).
- [ ] All 5 new Tier-1 tests pass.
- [ ] Fast incremental compile check of `GreatTamanaEditor` succeeds (this
      new file is not yet consumed by any production code — a genuinely
      unused-but-compiling new class is expected and fine at the end of
      this phase; PHASE3 is what wires it in).
- [ ] `git_add` + `git_commit` + `PHASE2_COMPLETION_REPORT.md`.

### 3.5 — Non-goals for this phase specifically

- Does NOT wire this bridge into `EditorHost`/`EditorProjectLifecycleCapability`
  yet (PHASE3).
- Does NOT add any HTTP route yet (PHASE4).
