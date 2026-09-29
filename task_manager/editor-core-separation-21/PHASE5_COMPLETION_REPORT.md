# PHASE5 COMPLETION REPORT — The Iron Rule, Made Permanent And Self-Enforcing In Code

Campaign: `editor-core-separation-21` ("The Engine Is Lying" — Render Pass / Frame Debugger Honesty Campaign)
Phase: PHASE5 (`PHASE5_IRON_RULE_PERMANENT_MISMATCH_DETECTOR.md`)
Branch: `feature/editor-core-separation` (unchanged, as required)

---

## Summary

The campaign's "iron rule" — *a render pass's declared/enabled state and the Frame Debugger's own
displayed event tree must NEVER disagree* — is now a real, permanent, automatic runtime check, not just
a promise in a `.md` file. A new pure detector (`DetectRenderPassHonestyMismatches()`,
`src/Editor/RenderPassHonestyChecker.h/.cpp`) plus a Logger-aware, log-once-per-new-incident singleton
wrapper (`RenderPassHonestyGuard`, `src/Editor/RenderPassHonestyGuard.h/.cpp`) are wired into
`FrameDebuggerPanel::TriggerCapture()` — the exact chokepoint PHASE1's own temporary diagnostic logging
used, and the exact spot PHASE1 left a `TODO(editor-core-separation-21 PHASE5)` comment marking for this
work. The detector was proven live: it fires a real `GTE_LOG_ERROR("RenderPassHonesty", ...)` the instant
the original bug is deliberately, temporarily reintroduced, stays completely silent against the real,
already-fixed production code, and a genuine Tier-1 test suite (6 tests) locks in every documented
behavior. The temporary hack used for live-fire verification was fully reverted — confirmed via
`git status`/`git diff` showing zero changes to the hacked file.

---

## Step 3.1 — The pure, Tier-1-testable core

New files `src/Editor/RenderPassHonestyChecker.h`/`.cpp`:

```cpp
std::vector<std::string> DetectRenderPassHonestyMismatches(
    const std::vector<rg::RenderGraphPassSnapshot>& passes,
    const std::function<bool(const std::string&)>& isEnabledLookup);
```

Per the phase document's own Step 3.1 guidance ("reuse that type, do not invent a parallel one"), this
reuses `rg::RenderGraphPassSnapshot` directly (already plain, ImGui-free, Vulkan-free data — see
`RenderGraphSnapshot.h`) instead of inventing a parallel struct. A pass name is reported as a mismatch
if and only if it is BOTH:
- present, **non-culled**, in `passes` (genuinely executed this frame / would be shown as a real leaf
  in the Frame Debugger's own tree), AND
- reports `enabled == false` via `isEnabledLookup`.

A **culled** pass (`isCulled == true` — a real, legitimate, unrelated reason a pass didn't run,
`RenderGraphCompiler::Compile()`'s own dead-code elimination) is never reported, regardless of what its
own toggle entry says. A pass name the lookup has never heard of at all defaults to `true` (mirroring
`RenderPassToggleRegistry::IsEnabled()`'s own documented "never seen → true" default), so it is never
reported either — no special-case branch was needed for that condition; it falls out of the logic
naturally.

Zero dependency on `<imgui.h>` or `Logger` — mirrors `ImGuiIdConflictTracker.h`'s own precedent exactly.

## Step 3.1b — The Logger-aware wrapper (log once per new incident)

New files `src/Editor/RenderPassHonestyGuard.h`/`.cpp` — a process-global, main-thread-only singleton
mirroring `ImGuiIdConflictGuard`'s exact shape:

```cpp
class RenderPassHonestyGuard {
public:
    static RenderPassHonestyGuard& Instance() noexcept;
    void ReportCaptureMismatches(const std::vector<std::string>& mismatchedNames);
    std::size_t OngoingMismatchCount() const noexcept;
private:
    std::unordered_set<std::string> m_ongoingMismatches; // persists ACROSS captures
};
```

`ReportCaptureMismatches()` logs `GTE_LOG_ERROR("RenderPassHonesty", ...)` exactly once per pass name
that is **newly** mismatched since the previous call (`m_ongoingMismatches.insert(name).second == true`),
then erases any previously-ongoing name that is no longer present in this capture's list — mirroring
`ImGuiIdConflictGuard::CheckCurrentIdScope()`'s own "erase when no longer conflicting, so a future
recurrence is treated as fresh" rule precisely, reusing its actual pattern rather than inventing a new
one.

## Step 3.2 — Wired into the real per-capture pipeline

`FrameDebuggerPanel::TriggerCapture()` (`src/Editor/Panels/FrameDebuggerPanel.cpp`) — PHASE1's own
`TODO(editor-core-separation-21 PHASE5)` comment marked this exact spot as the natural chokepoint, and
this phase fills it in:

```cpp
const rg::RenderGraphSnapshot graphSnapshot =
    m_frameRenderGraph->LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback);

if (m_frameToggleRegistry != nullptr) {
    const std::vector<std::string> mismatches = DetectRenderPassHonestyMismatches(
        graphSnapshot.passesInExecutionOrder,
        [this](const std::string& name) { return m_frameToggleRegistry->IsEnabled(name); });
    RenderPassHonestyGuard::Instance().ReportCaptureMismatches(mismatches);
}
```

This runs once per real Frame Debugger capture — the natural "once per capture" cadence the phase
document asked for (mirroring `ImGuiIdConflictGuard`'s own "once per frame" cadence, scoped instead to
this feature's own natural unit).

Threading `m_frameToggleRegistry` through required a small, mechanical, additive change (same shape as
every other `toggleRegistry` parameter this whole campaign has already threaded through 6+ call sites in
PHASE4):
- `FrameDebuggerPanel.h` — new forward declaration `class RenderPassToggleRegistry;` (inside `namespace
  rg`), `Build()` gained a trailing, defaulted `rg::RenderPassToggleRegistry* toggleRegistry = nullptr`
  parameter, and a new non-owning member `rg::RenderPassToggleRegistry* m_frameToggleRegistry = nullptr`
  cached the exact same way as the pre-existing `m_frameRenderer`/`m_frameRenderGraph`/`m_frameGameView`.
- `FrameDebuggerPanel.cpp` — `Build()` now assigns `m_frameToggleRegistry = toggleRegistry;` alongside
  the other `m_frame*` assignments (unconditionally, every call, before the early-return for a closed
  window — matching those fields' own existing "set unconditionally" discipline, since `TriggerCapture()`
  can be invoked from the very TOP of `Build()`, using the PREVIOUS call's cached values, before this
  call's own assignment would otherwise run).
- `ImGuiEditorLayer.cpp` — the one real call site, `m_frameDebuggerPanel.Build(...)`, now passes
  `&renderPassToggleRegistry` — the exact same registry instance `RenderGraphPanel::Build()` (called
  immediately before, same function) already reads/mutates from its own checkboxes, so there is no
  chance of the detector ever comparing against a different registry than the one the user is actually
  looking at.

## Step 3.3 — Surfaced via the engine's own logging system, log-only (no new UI pattern)

1. **Confirmed live** (not assumed): `GET /get_logs?category=RenderPassHonesty` genuinely returns fired
   entries — see the live-fire transcript below.
2. **Visual indicator decision — no `ask_questions` needed**, per the phase document's own explicit
   fallback guidance: `ImGuiIdConflictGuard` (the direct precedent this whole detector mirrors) has NO
   visual surfacing of its own — it is a pure log-only mechanism (`GTE_LOG_ERROR("ImGuiIdConflict",
   ...)`, nothing drawn in any panel). The phase document itself says: *"if `ImGuiIdConflictGuard` has no
   such visual surfacing either, doing the same log-only treatment here is consistent and acceptable — do
   not invent a new UI pattern nothing else in this codebase uses."* Confirmed by directly reading
   `ImGuiIdConflictGuard.cpp` before deciding — it has no such surfacing. This detector therefore stays
   log-only too, by design, not oversight.

## Step 3.4 — Proof the detector actually works

### 3.4.1 — Tier-1 test suite (`tests/Editor/RenderPassHonestyCheckerTests.cpp`, 6 tests)

Added to `tests/CMakeLists.txt`'s unconditional (`if(TRUE)`) Editor test list, mirroring
`ImGuiIdConflictTrackerTests.cpp`'s own placement. Real run output:

```
Running main() from .../gtest_main.cc
Note: Google Test filter = RenderPassHonestyCheckerTest.*
[==========] Running 6 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 6 tests from RenderPassHonestyCheckerTest
[ RUN      ] RenderPassHonestyCheckerTest.ConsistentCaseProducesZeroMismatches
[       OK ] RenderPassHonestyCheckerTest.ConsistentCaseProducesZeroMismatches (0 ms)
[ RUN      ] RenderPassHonestyCheckerTest.SyntheticContradictionProducesExactlyOneMismatchNamingThatPass
[       OK ] RenderPassHonestyCheckerTest.SyntheticContradictionProducesExactlyOneMismatchNamingThatPass (0 ms)
[ RUN      ] RenderPassHonestyCheckerTest.CulledPassIsNeverReportedEvenIfItsToggleEntryReadsFalse
[       OK ] RenderPassHonestyCheckerTest.CulledPassIsNeverReportedEvenIfItsToggleEntryReadsFalse (0 ms)
[ RUN      ] RenderPassHonestyCheckerTest.PassNameNeverSeenByTheRegistryIsNeverReported
[       OK ] RenderPassHonestyCheckerTest.PassNameNeverSeenByTheRegistryIsNeverReported (0 ms)
[ RUN      ] RenderPassHonestyCheckerTest.MultipleMismatchesAreAllReported
[       OK ] RenderPassHonestyCheckerTest.MultipleMismatchesAreAllReported (0 ms)
[ RUN      ] RenderPassHonestyCheckerTest.EmptyLookupReturnsEmptyMismatchListDefensively
[       OK ] RenderPassHonestyCheckerTest.EmptyLookupReturnsEmptyMismatchListDefensively (0 ms)
[----------] 6 tests from RenderPassHonestyCheckerTest (0 ms total)
[----------] Global test environment tear-down
[==========] 6 tests from 1 test suite ran. (1 ms total)
[  PASSED  ] 6 tests.
```

Covers every scenario the phase document's own Step 3.4.1 mandated: the consistent case (0 mismatches),
the synthetic contradiction (exactly 1 mismatch, naming the exact pass), the culled-pass exclusion, and
the never-registered-name exclusion — plus two extra tests (multiple simultaneous mismatches all
reported; a defensively-empty lookup callable) for completeness.

### 3.4.2 — Live, HTTP-driven proof (real running `GreatTamanaEditor.exe`)

**Part A — deliberately reintroducing the original bug (temporary, throwaway hack, reverted after).**
`AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`'s own toggle guard was temporarily changed
from:
```cpp
const bool passEnabledThisFrame = toggleRegistry == nullptr
    || toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereAerialPerspectiveCompositePass");
```
to (still calling `NoteDeclaredAndCheckEnabled()` for its registration side effect, but ignoring its
return value):
```cpp
if (toggleRegistry != nullptr) {
    toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereAerialPerspectiveCompositePass");
}
const bool passEnabledThisFrame = true;
```
— i.e. the exact original bug PHASE1/PHASE2 fixed, reintroduced on purpose, for one throwaway
verification run.

After an incremental rebuild and launching `GreatTamanaEditor.exe` (PID 24352):

```
POST /clear_logs                                                                         -> {"cleared_count":38,"success":true}
GET  /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false -> {"success":true}
GET  /frame_debugger/open                                                                -> {"state":{...,"windowOpen":true},"success":true}
GET  /frame_debugger/enable?value=true                                                   -> {"state":{...,"enabled":true},"success":true}
GET  /frame_debugger/capture                                                             -> {"state":{...,"hasCapturedFrame":true,"totalEventCount":63},"success":true}
GET  /get_logs?category=RenderPassHonesty&min_level=Error&limit=50
  -> {"count":1,"entries":[{"category":"RenderPassHonesty","frame":618,"id":40,"level":"Error",
      "message":"Pass 'AtmosphereAerialPerspectiveCompositePass' is marked DISABLED in
       RenderPassToggleRegistry but still executed and appears in this frame's captured Render Graph
       snapshot - the render pass and the Frame Debugger disagree.","timestamp_seconds":10.4865814}],
      "latest_id":40,"logging_enabled":true}
```

**The detector fired a real, fresh `GTE_LOG_ERROR("RenderPassHonesty", ...)` entry, retrieved via
`GET /get_logs?category=RenderPassHonesty`, exactly as the phase document requires.**

**Log-once-per-new-incident proof (still the same ongoing mismatch — a second capture must NOT
duplicate it):**
```
GET /frame_debugger/capture -> {"state":{...,"totalEventCount":63},"success":true}
GET /get_logs?category=RenderPassHonesty&min_level=Error&limit=50
  -> {"count":1, ...}   <- STILL 1, not 2 - confirms "log once per new incident" dedup works.
```

**Clear-then-reappear-is-a-fresh-incident proof:**
```
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=true  -> {"success":true}
GET /frame_debugger/capture                                                                    -> {"state":{...},"success":true}
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false  -> {"success":true}
GET /frame_debugger/capture                                                                    -> {"state":{...},"success":true}
GET /get_logs?category=RenderPassHonesty&min_level=Error&limit=50
  -> {"count":2,"entries":[
        {"id":40,"frame":618, ...},
        {"id":41,"frame":1835,"message":"Pass 'AtmosphereAerialPerspectiveCompositePass' is marked
         DISABLED ... - the render pass and the Frame Debugger disagree."}
      ],"latest_id":41,"logging_enabled":true}
```

**A SECOND, genuinely new log entry (id 41, a new frame number) appeared** — confirms a mismatch that
cleared for at least one capture and then reappeared is correctly treated as a fresh incident and logged
again, exactly matching `RenderPassHonestyGuard`'s own documented contract.

**Cleanup for Part A**: `enabled=true` restored, Frame Debugger disabled
(`/frame_debugger/enable?value=false`), engine stopped (`stop_app_background`, PID 24352). The hack was
then reverted with `edit_line`, and confirmed via:
```
git diff --stat src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp   -> (empty output - zero diff)
```
i.e. the file is byte-for-byte identical to `HEAD` again — the temporary hack left no trace.

**Part B — the real, already-fixed production code stays silent (no false positives).** After
rebuilding with the hack reverted and relaunching (PID 6888):

```
POST /clear_logs                                                                           -> {"cleared_count":38,"success":true}
GET  /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false -> {"success":true}
GET  /frame_debugger/open                                                                  -> {"state":{...,"windowOpen":true},"success":true}
GET  /frame_debugger/enable?value=true                                                     -> {"state":{...,"enabled":true},"success":true}
GET  /frame_debugger/capture                                                               -> {"state":{...,"hasCapturedFrame":true,"totalEventCount":61},"success":true}
```

`totalEventCount` is **61**, not 63 (the earlier, bugged-hack capture) — the real fix genuinely removes
both the pass leaf and its owned "Compute Dispatch" child from the tree when disabled, exactly as
PHASE2's own fix guarantees.

```
GET /get_logs?category=RenderPassHonesty&min_level=Error&limit=50 -> {"count":0,"entries":[],"latest_id":39,"logging_enabled":true}
GET /get_game_view -> HTTP 200, image/png, 158923 bytes, sane solid-blue sky (no magenta)
```

**Zero mismatches, zero false positives, against the real, correctly-behaving production code** — the
detector is silent exactly when it should be. Cleanup: `enabled=true` restored, Frame Debugger disabled,
engine stopped cleanly (PID 6888).

---

## Step 3.5 — Wrap-up

1. **Incremental compile check succeeded** after every change (three separate `cmake --build build` runs
   in this phase: the real feature, the temporary hack, and the reverted hack — all three succeeded
   cleanly, zero warnings/errors from any file this phase touched).
2. **Tier-1 test suite for the new file passes** — see the 6/6 passing transcript above
   (`GreatTamanaEngineTests.exe --gtest_filter=RenderPassHonestyCheckerTest.*`). The full regression suite
   was deliberately NOT run (reserved for PHASE6 per `PHASE0_MASTER_STRATEGY.md`'s Locked Decision #2).
3. This report (`PHASE5_COMPLETION_REPORT.md`) documents the detector's final design, the Tier-1 test
   transcript, and the full live-fire verification transcript, including explicit confirmation the
   temporary hack was reverted cleanly (`git diff --stat` empty for the hacked file).
4. `git_add` + `git_commit` follow this report.
5. The running `GreatTamanaEditor.exe` instance was cleanly closed via `stop_app_background` both times
   it was launched this phase (PID 24352, PID 6888) — no stray instance left running.
6. No `delegate_task` call was made (none permitted, none needed). No `ask_questions` call was made — the
   one point the phase document flagged as a possible decision point (Step 3.3.2, the visual-indicator
   question) was resolved by the phase document's own explicit fallback rule (mirror
   `ImGuiIdConflictGuard`'s own log-only treatment, confirmed by direct inspection), not a genuine
   ambiguity requiring a human decision.

---

## Files changed

- `src/Editor/RenderPassHonestyChecker.h` (new) / `.cpp` (new) — the pure, Tier-1-tested detector.
- `src/Editor/RenderPassHonestyGuard.h` (new) / `.cpp` (new) — the Logger-aware, log-once-per-new-incident
  singleton wrapper.
- `src/Editor/Panels/FrameDebuggerPanel.h` — new forward declaration, `Build()`'s new trailing
  `toggleRegistry` parameter, new `m_frameToggleRegistry` member.
- `src/Editor/Panels/FrameDebuggerPanel.cpp` — new `#include`s, `Build()`'s new parameter/assignment,
  `TriggerCapture()`'s new detector call replacing PHASE1's own `TODO` comment.
- `src/Editor/ImGuiEditorLayer.cpp` — the one real call site now passes `&renderPassToggleRegistry`.
- `CMakeLists.txt` — registers the two new `gte_editor` source file pairs.
- `tests/CMakeLists.txt` — registers the new Tier-1 test file.
- `tests/Editor/RenderPassHonestyCheckerTests.cpp` (new) — 6 Tier-1 tests.
- `task_manager/editor-core-separation-21/PHASE5_COMPLETION_REPORT.md` (new — this report).

`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` was temporarily modified for Step 3.4.2's live-fire
verification and then fully reverted — it carries **zero** net change and is **not** part of this
phase's committed diff (confirmed via `git status`/`git diff --stat` immediately before committing).

## No delegation, no unresolved ambiguity

No `delegate_task` call was made. No genuine design ambiguity was encountered that required
`ask_questions` — the one candidate point (visual surfacing) was resolved by the phase document's own
documented fallback rule after directly confirming `ImGuiIdConflictGuard` has no such surfacing itself.
