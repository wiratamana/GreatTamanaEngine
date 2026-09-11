# PHASE1 — Editor UI Command Bridge + Shared Panel Catalog

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first.

## Step 1: The Goal

Build the two pieces of pure, dependency-light infrastructure everything
else in this campaign is built on top of:

1. `EditorUiCommandBridge` — a brand-new, small, single-purpose cross-thread
   bridge (mirrors `EngineCommandBridge` exactly), the ONE thing a future
   network route handler for an Editor-UI-mutating action is allowed to
   touch.
2. `EditorPanelCatalog.h` — the ONE shared, canonical list of known Editor
   panel/tab names, usable from BOTH `src/Editor/DockLayout.cpp` (Editor-
   only) and `src/Network/` (always compiled, regardless of
   `GTE_ENABLE_EDITOR`), so the two can never silently drift apart.

Neither piece touches ImGui, Vulkan, or httplib at all — both are Tier-1
testable by construction, exactly like `EngineCommandBridge`/
`FrameCaptureBridge` already are.

## Step 2: The Situation / The Problem

- `src/Application/EngineCommandBridge.h/.cpp` already proves the exact
  shape needed: a single-global-slot, mutex + `condition_variable`-guarded
  request/response bridge, with `SubmitAndWait()` (network thread),
  `TryPeekPendingCommandRequest()`/`FulfillCommand()` (main thread). Read
  that file in full before starting — this phase's new bridge is
  deliberately a close structural copy of it, not a reinvention.
- `src/Editor/DockLayout.cpp` already has a private `kAllPanelNames` array
  (see its own top-of-file comment) that is the de facto canonical panel
  list — but it's `static`/anonymous-namespace-local to that one
  `GTE_ENABLE_EDITOR`-only file, so nothing in `src/Network/` can see it
  today, and duplicating it by hand in two places would risk the two lists
  silently drifting apart the next time a panel is added/removed.
- `src/Editor/EditorLayer.h` and `src/Editor/NullEditorLayer.cpp` are
  **already** a documented, deliberate exception to "everything under
  `src/Editor/` is compiled only under `GTE_ENABLE_EDITOR`" (see AGENTS.md,
  "Editor Module Structure": "Only `EditorLayer.h` ... and
  `NullEditorLayer.cpp` ... must stay completely free of ImGui/SDL/Vulkan-
  beyond-forward-declares"). This phase adds a SECOND such exception,
  `EditorPanelCatalog.h` — a plain, header-only, zero-include (beyond
  `<cstddef>`/`<string>`) data file, physically living under `src/Editor/`
  but included from `src/Network/` regardless of `GTE_ENABLE_EDITOR`. This
  is safe because CMake's `if(GTE_ENABLE_EDITOR)` block only controls which
  `.cpp` FILES get added to `target_sources()` — a header with no matching
  `.cpp` is never itself "gated"; it simply compiles wherever it's
  `#include`d, exactly like `EditorLayer.h` already does.

## Step 3: The Plan

### 3.1 — `src/Editor/EditorPanelCatalog.h` (NEW FILE, header-only, no `.cpp`)

Create this file with the following exact shape:

```cpp
#pragma once

#include <cstddef>
#include <string>

// Canonical, exhaustive list of every named Editor panel this engine's
// default dock layout creates (see DockLayout.cpp's own
// BuildDefaultDockLayout()/kAllPanelNames) - the SAME literal strings,
// factored out here so DockLayout.cpp and the Network layer's
// GET /activate_tab / GET /list_tabs routes (see NetworkRoutes.cpp,
// network-impl-7 campaign) can never silently drift apart from each other.
//
// Deliberately ImGui/SDL/Vulkan-free, and physically living under
// src/Editor/ despite that - a SECOND explicit, documented exception to
// "everything under src/Editor/ compiles only under GTE_ENABLE_EDITOR"
// alongside EditorLayer.h/NullEditorLayer.cpp (see AGENTS.md, "Editor
// Module Structure"). This is safe because a HEADER with no matching .cpp
// is never itself gated by CMakeLists.txt's `if(GTE_ENABLE_EDITOR)` block -
// it simply compiles wherever it is #included, including from
// src/Network/ (which must build regardless of GTE_ENABLE_EDITOR).
//
// GTE_ENABLE_PROJECT_PANEL is a PUBLIC compile definition on the gte_core
// target (see CMakeLists.txt's own target_compile_definitions() call), so
// it is visible here exactly as it already is inside DockLayout.cpp.
namespace gte {

inline constexpr const char* kKnownEditorPanelNames[] = {
    "Hierarchy",
    "Inspector",
    "Scene",
    "Game",
    "Memory",
    "Profiler",
    "Render Graph",
    "Jobs",
    "Atmosphere",
#if GTE_ENABLE_PROJECT_PANEL
    "Project",
#endif
};

inline constexpr std::size_t kKnownEditorPanelNameCount =
    sizeof(kKnownEditorPanelNames) / sizeof(kKnownEditorPanelNames[0]);

// Pure, case-sensitive, exact-string-match lookup - the ONE shared
// definition of "is this name one GET /activate_tab / GET /list_tabs are
// allowed to talk about" (see NetworkRoutes.cpp's ParseActivateTabQuery(),
// Phase 4). Deliberately case-sensitive, matching every existing exact-
// match convention in NetworkRoutes.cpp (see e.g. ParseGetTextureQuery()'s
// own "color"/"depth" exact-lowercase-only rule and its accompanying
// comment on why this codebase is consistently exact-case throughout this
// one file).
inline bool IsKnownEditorPanelName(const std::string& name) noexcept
{
    for (const char* candidate : kKnownEditorPanelNames) {
        if (name == candidate) {
            return true;
        }
    }
    return false;
}

} // namespace gte
```

**IMPORTANT — do not touch `DockLayout.cpp`'s own `kAllPanelNames` array in
this phase.** Wiring `DockLayout.cpp` to actually USE this new shared header
(replacing its own private array) is Phase 2's job, not this one — Phase 1
only ever ADDS a new, unused-so-far header. This keeps this phase's own
diff minimal and independently compile-checkable (a header with no
consumer yet still compiles cleanly on its own).

### 3.2 — `src/Application/EditorUiCommandBridge.h` (NEW FILE)

Structurally copy `src/Application/EngineCommandBridge.h` almost verbatim,
with these substitutions. Read `EngineCommandBridge.h` in full first — every
comment in that file about WHY the design is shaped this way (single global
slot, `SubmitResult`/`alreadyPending`/`timedOut`, the "second-iteration fix"
about `TryPeekPendingCommandRequest()` collapsing check-then-use into one
locked operation) applies identically here and should be preserved/adapted,
not re-derived from scratch or simplified away.

```cpp
#pragma once

// The ONE reviewed, thread-safe bridge a Network route handler (background
// thread) is allowed to touch to make an EDITOR-UI-mutating request happen
// on the main thread - network-impl-7 campaign. Structurally mirrors
// EngineCommandBridge.h (ECS-mutating requests) and FrameCaptureBridge.h
// (read-only pixel requests) - see either file's own header comment for the
// shared cross-thread design rationale - but is deliberately its OWN,
// separate, narrow bridge type, per AGENTS.md's own explicit rule: a
// genuinely new KIND of request gets its own bridge, never a new enum value
// bolted onto an existing bridge built for an unrelated purpose (see
// AGENTS.md, "Networking", FrameCaptureBridge bullet).
//
// Deliberately free of ImGui/Editor #includes - this header only ever moves
// a plain tab-name string and a plain bool/string outcome between "the
// network thread wants this tab focused" and "the main thread focused it (or
// couldn't)". The actual ImGui work happens inside IEditorLayer::ActivateTab()
// (Phase 2) - Application::Run() (Phase 3) is what bridges the two.
//
// Owned by Application (the composition root), constructed alongside
// m_captureBridge/m_commandBridge, BEFORE NetworkServer (so its address can
// be handed into NetworkServer's constructor) - see Application.h (Phase 3).

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

// Only one kind exists today, but this follows EngineCommandKind's own
// precedent of being an enum (not a single hardcoded request shape) so a
// FUTURE Editor-UI command (e.g. "close tab X", "set panel Y visible") can
// be added to this SAME bridge later without inventing a third one - see
// EngineCommandKind's own history (started with 2 values in network-impl-3,
// grew to 4 in network-impl-5, reusing the SAME bridge both times).
enum class EditorUiCommandKind {
    ActivateTab,
};

// Plain request payload for one ActivateTab command.
struct ActivateTabCommand {
    std::string tabName;
};

// One pending Editor UI command, tagged by `kind` - only `activateTab` is
// meaningful today (EXACTLY one field meaningful, selected by `kind` - same
// tagged-struct convention as EngineCommandRequest, not std::variant).
struct EditorUiCommandRequest {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabCommand activateTab;
};

// Outcome of one ActivateTab command. `success` is the single field a
// caller needs for the happy path; `tabExists` distinguishes WHY a failure
// happened (see NetworkRoutes.cpp's own 404-vs-409 status mapping, Phase 4):
// - success == true                         -> tab found and focused (200)
// - success == false && tabExists == false   -> no live ImGui window with
//                                                that exact name existed
//                                                this frame (409 - the name
//                                                itself is fine, nothing to
//                                                focus RIGHT NOW)
// A name that isn't even in EditorPanelCatalog.h's known list is rejected
// BEFORE ever reaching this bridge at all (see NetworkRoutes.cpp's own
// pre-validation, Phase 4) - this outcome type has no field for that case
// because it is structurally unreachable here.
struct ActivateTabOutcome {
    bool success = false;
    bool tabExists = false;
};

// The completed result of one EditorUiCommandRequest - `kind` mirrors the
// request's own `kind`.
struct EditorUiCommandResult {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabOutcome activateTab;
};

class EditorUiCommandBridge {
public:
    EditorUiCommandBridge() = default;
    ~EditorUiCommandBridge() = default;

    EditorUiCommandBridge(const EditorUiCommandBridge&) = delete;
    EditorUiCommandBridge& operator=(const EditorUiCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        std::optional<EditorUiCommandResult> result;
        bool alreadyPending = false;
        bool timedOut = false;
    };
    SubmitResult SubmitAndWait(EditorUiCommandRequest request, int timeoutMilliseconds = 3000);

    // --- Called from the MAIN thread (Application::Run()) only ----------

    bool IsCommandPending() const;

    // Same "collapse check-then-use into one locked operation" fix
    // EngineCommandBridge::TryPeekPendingCommandRequest() already documents
    // in full - copy that exact reasoning/behavior here, do not regress to
    // a separate IsCommandPending() + PeekPendingCommandRequest() pair.
    std::optional<EditorUiCommandRequest> TryPeekPendingCommandRequest() const;

    void FulfillCommand(EditorUiCommandResult result);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    EditorUiCommandRequest m_request;
    EditorUiCommandResult m_result;
};

} // namespace gte
```

### 3.3 — `src/Application/EditorUiCommandBridge.cpp` (NEW FILE)

Copy `EngineCommandBridge.cpp`'s implementation verbatim, substituting type
names (`EditorUiCommandBridge`/`EditorUiCommandRequest`/
`EditorUiCommandResult` in place of `EngineCommandBridge`/
`EngineCommandRequest`/`EngineCommandResult`). Every line of logic
(`SubmitAndWait()`'s already-pending fast return, the
`m_conditionVariable.wait_for()` call, the "reset back to idle on timeout so
a late `FulfillCommand()` is a safe no-op" comment, `TryPeekPendingCommandRequest()`'s
single locked read, `FulfillCommand()`'s "nobody is actually waiting" no-op
guard) applies identically and must be preserved, not simplified away.

### 3.4 — Register the two new files in `CMakeLists.txt`

Add both new lines directly after the existing `EngineCommandBridge.h/.cpp`
lines inside `gte_core`'s MAIN, unconditional `add_library(gte_core STATIC
...)` source list (see the existing block containing
`src/Application/EngineCommandBridge.h` /
`src/Application/EngineCommandBridge.cpp` around line 359-360 of
`CMakeLists.txt` — search for that exact text to find the right spot; this
whole list starts at `add_library(gte_core STATIC` around line 235 and has
NO `if()` guard around it at all):

```
src/Application/EditorUiCommandBridge.h
src/Application/EditorUiCommandBridge.cpp
```

**CORRECTION (double-checked against the real, on-disk `CMakeLists.txt`
during this campaign's own double-check pass): `EditorLayer.h` is NOT
actually listed ANYWHERE in `CMakeLists.txt`'s `target_sources()`/
`add_library()` calls today.** Searching the whole file for
`src/Editor/EditorLayer.h` only finds it inside two PROSE COMMENTS (around
lines 22 and 552) — never as an actual listed source line. Do not cite it as
a precedent for "every header, even a header with no matching `.cpp`, gets
listed for IDE visibility" — that precedent does not actually hold for this
one file in this codebase, so do not go looking for it again.
What DOES matter, and is true independently of that (non-existent)
precedent, is WHERE `EditorPanelCatalog.h` must be added if you do add it:
since `src/Network/NetworkRoutes.cpp` (Phase 4) needs to `#include` it in
EVERY build configuration (editor or not), it must go in `gte_core`'s MAIN,
unconditional `add_library(gte_core STATIC ...)` list — the exact same list
`src/Application/EngineCommandBridge.h` above already sits in — and it must
NOT go inside the `if(GTE_ENABLE_EDITOR)` `target_sources()` block that lists
`src/Editor/EditorContext.h`/`src/Editor/DockLayout.h` (that whole block is
simply never compiled at all in a `GTE_ENABLE_EDITOR=OFF` configure — see
`tests/CMakeLists.txt`'s own directly-analogous gating trap flagged in
Section 3.5 below for a second, real, confirmed example of exactly this
mistake). Add `src/Editor/EditorPanelCatalog.h` as one more line in that
MAIN, unconditional list — e.g. directly after the two new
`EditorUiCommandBridge.h/.cpp` lines above is a natural spot — never inside
either the `if(GTE_ENABLE_EDITOR)` or `if(GTE_ENABLE_PROJECT_PANEL)` block
further down.

### 3.5 — Tests (Tier 1, add in this SAME phase)

Create `tests/Application/EditorUiCommandBridgeTests.cpp`, mirroring
`tests/Application/EngineCommandBridgeTests.cpp`'s own test structure and
naming style exactly (read that file first). Cover, at minimum:

- `SubmitAndWaitReturnsFulfilledResult` — a background `std::thread` calls
  `SubmitAndWait()`; the main "thread" (the test itself) polls
  `TryPeekPendingCommandRequest()` until it sees the request, then calls
  `FulfillCommand()` with a known `ActivateTabOutcome{ true, true }`; assert
  the background thread's `SubmitResult::result` matches.
- `SubmitAndWaitReturnsAlreadyPendingWhenAnotherRequestIsInFlight` — mirrors
  `EngineCommandBridgeTests.cpp`'s own equivalent test exactly.
- `SubmitAndWaitTimesOutWhenNeverFulfilled` — a short `timeoutMilliseconds`
  (e.g. 50ms), nobody ever calls `FulfillCommand()`, assert
  `SubmitResult::timedOut == true`.
- `FulfillCommandIsANoOpWhenNothingIsPending` — calling `FulfillCommand()`
  with nothing ever submitted must not crash/assert.
- `TryPeekPendingCommandRequestReturnsNulloptWhenIdle` — a fresh bridge with
  nothing submitted returns `std::nullopt`.
- `IsKnownEditorPanelNameMatchesEveryEntryAndRejectsUnknownNames` (a SEPARATE
  test file/section is fine — could live in the same file or a new
  `tests/Editor/EditorPanelCatalogTests.cpp`) — assert
  `IsKnownEditorPanelName("Profiler") == true`,
  `IsKnownEditorPanelName("profiler") == false` (case-sensitive),
  `IsKnownEditorPanelName("NotARealTab") == false`, and that every literal in
  `kKnownEditorPanelNames` round-trips through `IsKnownEditorPanelName()` as
  `true` (a loop-driven test, so adding a new panel name later automatically
  gets covered with no test-file edit needed).

**CORRECTION/IMPORTANT (double-checked against the real, on-disk
`tests/CMakeLists.txt` during this campaign's own double-check pass) — do
NOT follow `EditorCameraTests.cpp` as a REGISTRATION placement precedent,
even though it is a genuine, real `tests/Editor/*.cpp` file tested the same
"pure logic, no live ImGui needed" way this one is:** `EditorCameraTests.cpp`,
and every single other `tests/Editor/*.cpp` entry, is registered EXCLUSIVELY
inside `tests/CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` block (search that
file for `Editor/EditorCameraTests.cpp` — it is inside a
`list(APPEND GTE_TEST_SOURCES ...)` call that itself sits inside
`if(GTE_ENABLE_EDITOR)`, around line 1861) — meaning NONE of those test files
build or run at all in a `GTE_ENABLE_EDITOR=OFF` configure. That is exactly
the WRONG bucket for the new panel-catalog test: `EditorPanelCatalog.h`/
`IsKnownEditorPanelName()` must compile and be tested in EVERY configuration,
`GTE_ENABLE_EDITOR` ON or OFF (mirroring `EditorUiCommandBridge`'s own
unconditional nature — see Sections 3.1/3.2 above), so its test can NEVER go
inside that `if(GTE_ENABLE_EDITOR)` block, regardless of which physical
folder the `.cpp` file itself lives in.

Register the new test file(s) in `tests/CMakeLists.txt`'s MAIN, unconditional
`GTE_TEST_SOURCES` list — directly alongside
`Application/EngineCommandBridgeTests.cpp` (search for that exact string;
this is the same always-built list, with no `if()` guard at all, that
`Network/NetworkRoutesTests.cpp` etc. also sit in) — even if the test file's
own path is `Editor/EditorPanelCatalogTests.cpp`. A source file's on-disk
folder name has no bearing on which `GTE_TEST_SOURCES` list block it must be
registered in; do NOT add it to the `if(GTE_ENABLE_EDITOR)` block that every
pre-existing `Editor/*Tests.cpp` entry currently sits in. **Neither new test
file needs `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` gating** — both
`EditorUiCommandBridge` and `EditorPanelCatalog.h` compile unconditionally
(see Sections 3.1/3.2 above), so their tests belong in the SAME always-built
bucket `EngineCommandBridgeTests.cpp` is already in (explicitly NOT the
`if(GTE_ENABLE_EDITOR)`-gated bucket `EditorCameraTests.cpp` is in, despite
the superficially similar `Editor/` folder name).

## Step 4: Verification for this phase

- `cmake --build build` succeeds (fast compile check only — no full
  `ctest` run required yet, per PHASE0's own workflow rule).
- Build and run just the new test file(s) (or the full
  `GreatTamanaEngineTests` binary, filtered by test name if the runner
  supports it) and confirm every new test passes.
- Write `PHASE1_COMPLETION_REPORT.md` in this same folder, `git add`/
  `git commit` the new files + report together.
