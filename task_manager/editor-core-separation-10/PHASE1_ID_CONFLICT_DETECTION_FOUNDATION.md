# PHASE1 — ID Conflict Detection Foundation

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first — goal, situation,
Locked Design Decisions, non-goals, file manifest all live there and are not
repeated here).

**Status when you start this phase:** nothing from this campaign exists yet.
This is the first phase. There is no previous phase report to read.

---

## Step 1: The Goal Of This Phase

Build the two new, inert (not yet called by any panel) pieces this whole
campaign is built on:

1. `gte::ImGuiIdConflictTracker` — a pure, tiny, Tier-1-testable class with
   ZERO dependency on `<imgui.h>` or `Logger`. It only knows "have I seen
   this plain 32-bit number before, since the last time I was told to
   forget everything?".
2. `gte::ImGuiIdConflictGuard` — an Editor-owned, process-global singleton
   (mirroring `LoggerLogSink::Instance()`'s exact shape) that wraps #1,
   knows about real `ImGuiID`/`ImGui::GetID()`, and knows how to log a
   conflict exactly once via `GTE_LOG_ERROR`, per Locked Design Decision #2
   ("log once per NEW conflict, not once per frame").

By the end of this phase: both classes exist, compile, are wired into both
CMake file lists, `ImGuiIdConflictGuard::BeginFrame()` is called once per
frame from `ImGuiEditorLayer::NewFrame()`, and a new Tier-1 test file proves
`ImGuiIdConflictTracker`'s logic is correct. **Nothing visible changes yet**
— no panel calls any of this code yet (that starts in PHASE2). This phase is
pure, safe, additive scaffolding.

## Step 2: The Situation Going Into This Phase

Read `PHASE0_MASTER_STRATEGY.md` section 2.5 for the exact conventions to
copy (`LoggerLogSink::Instance()` singleton shape, `tests/Editor/` mirrors
`src/Editor/`, explicit CMake file lists). Read `src/Core/Logging.h` in full
before writing any logging call — `GTE_LOG_ERROR(category, message)` takes
two `std::string`-convertible arguments; `category` should be the literal
string `"ImGuiIdConflict"` everywhere in this campaign (a single, greppable,
consistent category — this lets a future `GET /get_logs?category=
ImGuiIdConflict` call answer "has this ever happened?" in one request).

## Step 3: The Detailed Plan

### 3.1 — Create `src/Editor/ImGuiIdConflictTracker.h`

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>

namespace gte {

// Pure, Tier-1-testable id-collision detector - see AGENTS.md's "ImGui
// Widget ID Uniqueness" section and
// task_manager/editor-core-separation-10/PHASE0_MASTER_STRATEGY.md for the
// full story (the Render Graph Panel duplicate-checkbox bug this exists to
// permanently prevent a recurrence of). Deliberately has ZERO dependency on
// <imgui.h> or Logger - it operates purely on plain std::uint32_t values the
// caller already resolved. See ImGuiIdConflictGuard (ImGuiIdConflictGuard.h)
// for the real ImGui/Logger-aware wrapper built on top of this class - that
// is the one production call sites actually use; this class exists
// separately, with this narrow a contract, specifically so it can be tested
// with no live ImGui context at all (see
// tests/Editor/ImGuiIdConflictTrackerTests.cpp).
class ImGuiIdConflictTracker {
public:
    // Clears every id recorded since the last Reset() call - call exactly
    // once per real UI frame. Production call site: ImGuiIdConflictGuard::
    // BeginFrame(), itself called from ImGuiEditorLayer::NewFrame().
    void Reset();

    // Records `id` as seen since the last Reset() and returns true if it was
    // ALREADY recorded earlier during this same Reset()..Reset() window
    // (i.e. THIS call is a genuine conflict - two different call sites
    // resolved to the exact same id before the next Reset()). Recording
    // always happens regardless of the return value, so a third or fourth
    // occurrence of the same id keeps correctly being reported as a
    // conflict too, not just the second occurrence.
    bool RegisterAndCheckConflict(std::uint32_t id);

    // Number of distinct ids recorded since the last Reset() - exposed
    // purely for tests/diagnostics; no production call site needs this.
    std::size_t DistinctIdCount() const noexcept;

private:
    std::unordered_set<std::uint32_t> m_seenIds;
};

} // namespace gte
```

### 3.2 — Create `src/Editor/ImGuiIdConflictTracker.cpp`

```cpp
#include "ImGuiIdConflictTracker.h"

namespace gte {

void ImGuiIdConflictTracker::Reset()
{
    m_seenIds.clear();
}

bool ImGuiIdConflictTracker::RegisterAndCheckConflict(std::uint32_t id)
{
    const bool alreadySeen = (m_seenIds.find(id) != m_seenIds.end());
    m_seenIds.insert(id);
    return alreadySeen;
}

std::size_t ImGuiIdConflictTracker::DistinctIdCount() const noexcept
{
    return m_seenIds.size();
}

} // namespace gte
```

### 3.3 — Create `tests/Editor/ImGuiIdConflictTrackerTests.cpp`

This is the ONE mandatory Tier-1 test file for this phase (AGENTS.md: "Every
change to Tier 1 code must come with a matching test change"). Write real
GoogleTest cases (match the style of any neighboring file in `tests/Editor/`
— e.g. skim `EditorGpuMemoryNameOverlayTests.cpp` for the exact include/
fixture conventions this codebase already uses) covering at minimum:

- Registering a fresh id for the first time returns `false` (not a
  conflict), and `DistinctIdCount()` becomes `1`.
- Registering the SAME id a second time (no `Reset()` in between) returns
  `true`.
- Registering the SAME id a THIRD time (still no `Reset()`) ALSO returns
  `true` (not just the second occurrence — this directly protects the "a
  table with 3+ duplicate-named rows" shape from the original bug report,
  where "AtmosphereAerialPerspectiveVolumePass" appeared in more than 2
  rows).
- Calling `Reset()` and then registering the same id that conflicted before
  `Reset()` returns `false` again (a fresh frame starts clean).
- Two DIFFERENT ids registered in the same window never conflict with each
  other.
- `DistinctIdCount()` reflects the correct distinct count with a mix of
  repeats and fresh ids in one window.

### 3.4 — Create `src/Editor/ImGuiIdConflictGuard.h`

```cpp
#pragma once

#include "ImGuiIdConflictTracker.h"

#include <cstdint>
#include <unordered_set>

// Forward-declare rather than #include <imgui.h> here - keeps this header
// cheap for any future file that only needs to call CheckCurrentIdScope()
// via ImGuiUniqueId.h (PHASE2), which is the only production call site.
// ImGuiIdConflictGuard.cpp includes <imgui.h> for real.

namespace gte {

// Editor-owned, process-global, main-thread-only singleton - mirrors
// LoggerLogSink::Instance()'s exact shape (src/Editor/Logger.h) for the
// same reason: a call site can be anywhere under src/Editor/, with no
// single natural owner to thread a reference through, and (unlike Logger
// itself) this is legitimately main-thread-only, since Dear ImGui itself
// is main-thread-only in this engine.
//
// Wraps ImGuiIdConflictTracker (the pure, Tier-1-tested detector) with two
// real-world concerns the pure tracker deliberately knows nothing about:
// (1) resolving a REAL Dear ImGui ID via ImGui::GetID(), and (2) deciding
// WHEN to actually log a conflict - see CheckCurrentIdScope()'s own comment
// for the "log once per NEW conflict, not once per frame" rule (Locked
// Design Decision #2, PHASE0_MASTER_STRATEGY.md). Deliberately untested by
// any automated test (Tier 2 - needs a live ImGui context to call
// ImGui::GetID() at all, and a live installed Logger sink to observe its
// own logging side effect) - exactly the same "pure logic tested, ImGui/
// Vulkan-owning wrapper not" split AGENTS.md already documents for
// GpuMemoryTracker vs every Vulkan-owning class.
class ImGuiIdConflictGuard {
public:
    static ImGuiIdConflictGuard& Instance() noexcept;

    // Call exactly once per real UI frame, AFTER ImGui::NewFrame() (see
    // ImGuiEditorLayer::NewFrame()). Resets the per-frame tracker; does
    // NOT reset which conflicts are considered "ongoing" (see
    // CheckCurrentIdScope() - that is what makes "log once per new
    // conflict" work across frames).
    void BeginFrame();

    // Called by ScopedUniqueId's constructor (ImGuiUniqueId.h, PHASE2),
    // exactly once per ID scope entered, immediately AFTER the relevant
    // ImGui::PushID() call(s) for that scope. Resolves the CURRENT ImGui ID
    // stack position via ImGui::GetID("") and checks it against this
    // frame's tracker.
    //
    // `debugContext` is a short, human-readable call site description
    // (e.g. "RenderGraphPanel::BuildPassRow") - always a string literal at
    // the call site, never freed/dangling. `debugKey` is the semantic
    // string this scope's uniqueness argument was ALSO built from (e.g. a
    // pass name) - used ONLY for the log message text if a conflict is
    // found, NEVER relied upon for uniqueness/hashing itself (may be
    // nullptr or empty).
    //
    // Logs via GTE_LOG_ERROR("ImGuiIdConflict", ...) - but only the FIRST
    // time a given real ImGui ID starts conflicting; if the exact same
    // conflict is still happening next frame too, it stays silent (no
    // spam) until the conflict actually clears for at least one frame and
    // then reappears (a genuinely NEW incident, worth a fresh log line) -
    // Locked Design Decision #2.
    void CheckCurrentIdScope(const char* debugContext, const char* debugKey);

private:
    ImGuiIdConflictGuard() = default;

    ImGuiIdConflictTracker m_frameTracker;                // reset every BeginFrame()
    std::unordered_set<std::uint32_t> m_ongoingConflicts; // persists ACROSS frames
};

} // namespace gte
```

### 3.5 — Create `src/Editor/ImGuiIdConflictGuard.cpp`

```cpp
#include "ImGuiIdConflictGuard.h"

#include "../Core/Logging.h"

#include <imgui.h>

#include <string>

namespace gte {

ImGuiIdConflictGuard& ImGuiIdConflictGuard::Instance() noexcept
{
    static ImGuiIdConflictGuard s_instance;
    return s_instance;
}

void ImGuiIdConflictGuard::BeginFrame()
{
    m_frameTracker.Reset();
    // m_ongoingConflicts is deliberately NOT cleared here - see this
    // class's own header comment on CheckCurrentIdScope() for why it must
    // persist across frames (that persistence is what makes "log once per
    // NEW conflict" possible at all).
}

void ImGuiIdConflictGuard::CheckCurrentIdScope(const char* debugContext, const char* debugKey)
{
    const std::uint32_t id = static_cast<std::uint32_t>(ImGui::GetID(""));
    const bool conflictThisFrame = m_frameTracker.RegisterAndCheckConflict(id);

    if (!conflictThisFrame) {
        // Not conflicting THIS frame - if it used to be, it just cleared;
        // erasing it means a FUTURE recurrence is treated as fresh again
        // (re-logged), which is the correct, honest behavior - a resolved
        // and later-reintroduced bug deserves a new alert, not silence
        // forever because it once fired.
        m_ongoingConflicts.erase(id);
        return;
    }

    if (m_ongoingConflicts.insert(id).second) {
        // .second == true means this id was NOT already in the set - i.e.
        // this is a genuinely NEW conflict incident, not a continuation of
        // one already logged last frame. Log exactly once.
        const std::string context = (debugContext != nullptr) ? debugContext : "?";
        const std::string key = (debugKey != nullptr) ? debugKey : "";
        GTE_LOG_ERROR("ImGuiIdConflict",
            "Duplicate ImGui widget ID detected within a single frame - context='" + context + "', key='" + key
                + "'. Two different ScopedUniqueId scopes resolved to the exact same underlying ImGui ID this "
                  "frame. This means the calling loop reused the same index for two different rows, or two "
                  "independent call sites collided - check the index the offending loop passed to ScopedUniqueId. "
                  "(This is caught proactively, before Dear ImGui's own built-in visual red-highlight would ever "
                  "fire for the same collision.)");
    }
}

} // namespace gte
```

### 3.6 — Wire `BeginFrame()` into the real per-frame loop

Edit `src/Editor/ImGuiEditorLayer.cpp`. Find `NewFrame()` (currently around
line 349-365 - re-verify with `read_file`/`read_line`, do not trust this line
number blindly). Add the include near the file's other Editor-local
includes, and add exactly one call right after `ImGui::NewFrame();`:

```cpp
#include "ImGuiIdConflictGuard.h"
```

```cpp
    void NewFrame() override
    {
        m_frameRendered = false;

        ImGui::SetCurrentContext(m_context);
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // task_manager/editor-core-separation-10 campaign, PHASE1 - resets
        // the ID-conflict-detection tracker for this fresh frame. Must run
        // AFTER ImGui::NewFrame() (so ImGui::GetID() calls later this frame
        // are meaningful) and BEFORE any panel builds a single widget.
        ImGuiIdConflictGuard::Instance().BeginFrame();

        // Required by ImGuizmo before any Manipulate() call this frame...
        BeginGizmoFrame();
    }
```

Keep the existing `BeginGizmoFrame()` call and comment exactly where they
are — only insert the new two lines (comment + call) between
`ImGui::NewFrame();` and the blank line before `BeginGizmoFrame()`.

### 3.7 — Register the four new files in `CMakeLists.txt` (root)

Find the `gte_editor` target's source list (the same list containing
`src/Editor/Logger.h`/`src/Editor/Logger.cpp`, around line 988-989 as of
this writing — re-verify before editing). Insert, right after the
`src/Editor/Logger.h`/`.cpp` pair:

```cmake
    # task_manager/editor-core-separation-10 campaign, PHASE1 - the ID-
    # conflict detection foundation (ImGuiIdConflictTracker is the pure,
    # Tier-1-tested logic; ImGuiIdConflictGuard is the ImGui/Logger-aware
    # singleton wrapper around it). See PHASE0_MASTER_STRATEGY.md.
    src/Editor/ImGuiIdConflictTracker.h
    src/Editor/ImGuiIdConflictTracker.cpp
    src/Editor/ImGuiIdConflictGuard.h
    src/Editor/ImGuiIdConflictGuard.cpp
```

### 3.8 — Register the new test file in `tests/CMakeLists.txt`

Find the `Editor/LoggerTests.cpp` entry inside the `GTE_TEST_SOURCES` list
under the Editor `if(TRUE)` block (around line 2309 as of this writing —
re-verify before editing). Insert right after it:

```cmake
        # task_manager/editor-core-separation-10 campaign, PHASE1 - pure,
        # Tier-1 test for ImGuiIdConflictTracker (zero ImGui/Logger
        # dependency by design - see ImGuiIdConflictTracker.h).
        Editor/ImGuiIdConflictTrackerTests.cpp
```

### 3.9 — Verify (incremental only — no full build/ctest this phase)

1. Run an incremental build (`cmake --build build`) targeting whatever the
   existing test/editor targets are named in this repo (check
   `tests/CMakeLists.txt`'s own target name and the root `CMakeLists.txt`'s
   executable target name if unsure) — this should only recompile the
   handful of new/changed translation units, not the whole tree. Fix any
   compile error before moving on.
2. Run just the new test binary/filter (e.g. via ctest with a `-R` regex
   matching `ImGuiIdConflictTracker`, or by running the built test
   executable directly with a GoogleTest `--gtest_filter`) — confirm every
   new case passes. Do NOT run the full `ctest` suite yet (Locked Design
   Decision #8) — that is PHASE4's job.
3. Double-check with `search_in_dir` that `ImGuiIdConflictGuard`/
   `ImGuiIdConflictTracker` are referenced nowhere else yet except the two
   places this phase added (`ImGuiEditorLayer.cpp`'s `NewFrame()`, and the
   test file) — PHASE2 is where real panels start calling this.

### 3.10 — Write `PHASE1_COMPLETION_REPORT.md`

Save it in this same folder
(`task_manager/editor-core-separation-10/PHASE1_COMPLETION_REPORT.md`),
covering: files added/edited, the exact final `NewFrame()` diff, the test
run output (pass count), and anything discovered that the next phase
(PHASE2) needs to know (e.g. if `ImGuiEditorLayer.cpp`'s `NewFrame()` looked
different than expected above, document exactly how). Then `git_add` +
`git_commit` everything from this phase in one commit.

## Step 4: Handoff To PHASE2

PHASE2 will build `gte::ScopedUniqueId` directly on top of
`ImGuiIdConflictGuard::CheckCurrentIdScope()` (already built, already wired
into the per-frame reset by this phase) and retrofit the exact reported bug.
Nothing in this phase needs to be revisited by PHASE2 unless PHASE2's own
compile reveals a signature mismatch — if so, fix it there and note the
discrepancy in `PHASE2_COMPLETION_REPORT.md`.
