# PHASE5 — The Iron Rule, Made Permanent And Self-Enforcing In Code

Parent: `PHASE0_MASTER_STRATEGY.md` (MUST READ FIRST).
Previous phase report: `PHASE4_COMPLETION_REPORT.md` (MUST READ FIRST).

---

## Step 1: The Goal (Where are we going?)

Turn the user's "iron rule" — *render pass state must mirror the Frame
Debugger, always* — from a promise into a real, permanent, automatic runtime
check that fires the instant it is ever violated again, by anyone, in any
future change, without requiring a human to notice a screenshot mismatch.
This must be discoverable through the engine's OWN logging system
(`GET /get_logs`), exactly like every other defensive check already built
into this codebase (see Step 2 below for the two precedents to mirror).

## Step 2: The Situation (Where are we now?)

This codebase already has TWO directly analogous precedents for exactly this
kind of self-enforcing invariant, both documented in `AGENTS.md`:

1. **`ImGuiIdConflictTracker`/`ImGuiIdConflictGuard`**
   (`src/Editor/ImGuiIdConflictTracker.h`/`ImGuiIdConflictGuard.h`) — catches
   an ImGui widget ID collision the moment it happens and logs it exactly
   once per new incident via `GTE_LOG_ERROR("ImGuiIdConflict", ...)`, reset
   once per frame. This is the closest possible precedent: a "proactive
   detector wired into a per-frame reset point, logging through the engine's
   own logger, once per NEW incident (not spammed every frame)".
2. **`DetectRenderPassEventContradictions()`**
   (`RenderGraphCompiler::Compile()`, `render-pass-4` campaign) — reports,
   via an unconditional message plus a debug-build `assert()`, the exact
   moment a pass's declared ordering metadata contradicts what its real
   resource dependencies say must happen. This is the closest possible
   precedent for "a render-graph-shape correctness check running every
   single compile, not just on request".

This phase's detector is architecturally a hybrid of both: it needs to run
once per CAPTURED Frame Debugger frame (mirroring precedent 1's "once per
frame" cadence) and it needs to compare two pieces of render-graph-adjacent
state against each other structurally (mirroring precedent 2's "contradiction
between two independently-derived facts" shape).

## Step 3: The Plan

### 3.1 — Design the check itself (pure, Tier-1-testable core)

Create a new, small, pure function/class — new file
`src/Editor/RenderPassHonestyChecker.h` (mirrring `AtmospherePassToggleLogic.h`'s
own "deliberately dependency-light" precedent, and `ImGuiIdConflictTracker.h`'s
"pure detector separate from its ImGui/Logger-aware guard wrapper" shape).
Its ONE job: given
- a list of `(name, isCulled/wasExecutedThisFrame)` pairs for every pass in a
  captured `RenderGraphSnapshot` (or, more directly, the exact
  `rg::RenderGraphPassSnapshot` list `FrameDebuggerData.cpp` already builds
  its tree from — reuse that type, do not invent a parallel one), and
- a way to query `RenderPassToggleRegistry::IsEnabled(name)` for any given
  name,
produce a list of MISMATCHES: every pass name that is BOTH (a) present,
non-culled, in the captured snapshot (i.e. it genuinely executed/would be
shown in the Frame Debugger tree as a real leaf) AND (b) reports `enabled ==
false` in the toggle registry. This is a contradiction by construction — see
`RenderGraphPanel.cpp`'s own documented contract, "a disabled pass leaves
ZERO trace in `rg::RenderGraphMetadata`, since it is never declared into the
graph at all" — if a name satisfies both (a) and (b) simultaneously, the
render graph lied about a pass it claims is disabled.

Take a plain `std::vector<T>`/span of a minimal pure struct (mirroring
`ImGuiIdConflictTracker`'s own "plain data in, plain data out" test-friendly
shape) so this function needs zero live `RenderGraphSnapshot`/
`RenderPassToggleRegistry` object graph to unit test — a Tier-1 test can
construct two tiny fake lists directly. Name it something explicit, e.g.
`DetectRenderPassHonestyMismatches(passSummaries, isEnabledLookup)`.

### 3.2 — Wire it into the real per-capture pipeline

Call this function from wherever `FrameDebuggerData.cpp` finishes building a
fresh capture's tree (the natural, existing "once per capture" cadence,
mirroring precedent 1's "once per frame" cadence but scoped to captures,
since that is this feature's own natural unit — Frame Debugger captures are
already documented as "exactly ONE captured frame is ever held in memory").
For every mismatch found, log EXACTLY ONCE PER NEWLY-DETECTED INCIDENT (not
every single capture that still has the same stale mismatch — mirror
`ImGuiIdConflictGuard`'s own "log once per new incident" de-duplication logic
precisely, reusing its actual pattern/shape rather than inventing a new one)
via:
```cpp
GTE_LOG_ERROR("RenderPassHonesty",
    "Pass '%s' is marked DISABLED in RenderPassToggleRegistry but still "
    "executed and appears in this frame's captured Render Graph snapshot - "
    "the render pass and the Frame Debugger disagree.", name.c_str());
```
(adjust exact `GTE_LOG_ERROR` call signature to match this codebase's real
macro shape — check `Logger.h` first).

### 3.3 — Surface it, loudly, beyond just the log

1. Confirm `GET /get_logs?category=RenderPassHonesty` genuinely returns any
   fired entries (this is "free" once the category string is used
   consistently, per the existing `Logger`/`GET /get_logs` contract — but
   verify it live anyway, do not assume).
2. Consider (and decide via `ask_questions` if genuinely ambiguous) whether
   the Editor's own "Render Graph" panel or "Log" panel should surface an
   explicit, impossible-to-miss visual indicator (e.g. a red banner) the
   moment this category has ANY entries this session — mirroring how
   `ImGuiIdConflictTracker`'s own findings are surfaced. This is a genuinely
   optional nice-to-have improving "no human has to grep logs to notice", but
   must not be skipped without at least considering it — check what, if
   anything, the existing `ImGuiIdConflictGuard` integration already does
   for its own category and mirror that exact treatment here for consistency
   (if `ImGuiIdConflictGuard` has no such visual surfacing either, doing the
   same "log-only" treatment here is consistent and acceptable — do not
   invent a new UI pattern nothing else in this codebase uses).

### 3.4 — Prove the detector actually works (mandatory)

1. A genuine Tier-1 test file, `tests/Editor/RenderPassHonestyCheckerTests.cpp`
   (mirroring `tests/Editor/*Tests.cpp`'s own existing shape/build-gating —
   check whether it needs `GTE_ENABLE_EDITOR` gating like
   `EditorCameraTests.cpp`/`MemoryPanelDataTests.cpp` do, since this logic
   lives under `src/Editor/`), covering at minimum:
   - A consistent case (every executed pass reports enabled, every disabled
     pass is genuinely absent from the snapshot) produces ZERO mismatches.
   - A synthetic contradiction (one pass name present+non-culled in the fake
     snapshot list, but the fake `isEnabledLookup` reports it `false`)
     produces EXACTLY ONE mismatch, naming that exact pass.
   - A pass present but CULLED (i.e. `isCulled == true`, a real, legitimate,
     unrelated state per `RenderGraphCompiler::Compile()`'s own dead-code
     elimination) must NEVER be reported as a mismatch even if its toggle
     entry also happens to read `false` — culling and toggle-disabling are
     two different, both-legitimate reasons a pass might not run, and this
     detector's whole job is distinguishing "executed contradiction" from
     "did not execute for an unrelated, honest reason".
   - A pass name the toggle registry has never heard of at all (i.e.
     `IsEnabled()`'s own documented "never seen -> true" default) must never
     be reported as a mismatch either.
2. A LIVE, HTTP-driven proof, using a deliberately reachable real mismatch —
   the cleanest way to construct one on demand: TEMPORARILY (for this
   verification step only, reverted immediately after, never committed) hack
   one real pass's own declare-time guard to ignore the registry while
   `RenderPassToggleRegistry`'s own entry still reports it disabled (i.e.
   deliberately reintroduce, for one throwaway test run only, the EXACT
   original bug PHASE1/PHASE2 fixed) — confirm the detector fires a real,
   fresh `GTE_LOG_ERROR("RenderPassHonesty", ...)` entry, retrieved via
   `gte_send_request` against `GET /get_logs?category=RenderPassHonesty`,
   then immediately revert the temporary hack (verify `git_status` shows
   nothing left uncommitted from this throwaway step before moving on).

### 3.5 — Wrap-up

1. Incremental compile check must succeed.
2. Tier-1 test suite for the new file passes
   (`build\GreatTamanaEngineTests.exe`, or `ctest` scoped to just this new
   test binary/filter — NOT the full regression suite yet, that is PHASE6's
   job).
3. Write `PHASE5_COMPLETION_REPORT.md`: the detector's final design, the
   Tier-1 test transcript, and the live-fire verification transcript from
   Step 3.4.2 (including explicit confirmation the temporary hack was
   reverted cleanly).
4. `git_add` + `git_commit`.
5. `stop_app_background` the running engine instance before finishing.
6. Do NOT call `delegate_task`. Use `ask_questions` per Step 3.3.2 above.
