# PHASE5 — Full validation, documentation, and regression safety

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1`–`PHASE4` all
already landed. This is the **last** phase of this campaign.

## Step 1: The Goal

Close out the campaign: a full clean build, a full `ctest` regression pass,
a live runtime confirmation that Pause/Step/Resume genuinely freezes
gameplay while rendering/camera navigation keep working, and the
documentation updates every prior campaign in this repository consistently
makes (`AGENTS.md`, `README.md`, `TODO.md`) so this feature is discoverable
by the next person (or the next AI agent) reading those files, exactly like
every other completed campaign referenced throughout this codebase already
is.

## Step 2: The Situation

- Every completed campaign in this repository ends with: (a) a full clean
  build, (b) a full `ctest` regression pass, (c) usually a live runtime
  smoke test, and (d) a short new paragraph in `README.md`'s "Status"
  section (newest entries at the top, oldest history moved to
  `docs/CHANGELOG.md`), plus a one-paragraph summary line added to
  `AGENTS.md` under a relevant heading when the campaign introduces a
  genuinely new, permanent subsystem/convention (see e.g. the "Job System"
  or "Scene Serialization" sections there).
- `TODO.md` is where deliberately-deferred follow-up ideas get recorded
  (see its own existing structure) — this is where PHASE0's Non-Goals
  (network endpoints, `timeScale` slider, keyboard shortcut, a real
  Play/Stop-with-snapshot mode, and the actual future frame-debugger
  feature this campaign's `Time`/`EngineContext` groundwork was built for)
  belong, so nobody mistakes their absence for an oversight.
- `docs/README.md` is the documentation index; `docs/conventions/` holds
  one file per established engineering convention (mirroring `AGENTS.md`'s
  own per-topic summaries). Whether this campaign is "permanent-convention"
  worthy enough to warrant a brand-new `docs/conventions/time-and-pause.md`
  file (vs. just an `AGENTS.md` paragraph) is a judgment call for whoever
  implements this phase — err toward adding one, given `Time`/
  `EngineContext` are genuinely new, permanent, always-compiled engine
  concepts other future campaigns (starting with the actual frame debugger)
  will build on.

## Step 3: The Plan

### 3.1 Full clean build

Run a full, from-scratch build (matching whatever this repository's own
documented "clean build" procedure is — see `BUILDING.md`) covering, at
minimum:

- `GTE_ENABLE_EDITOR=ON` (default) — the real, everyday configuration.
- `GTE_ENABLE_EDITOR=OFF` — confirms `NullEditorLayer`'s two new trivial
  overrides (PHASE3) keep a release-style build working, and that
  `Game::Update()`'s new signature/freeze logic (PHASE2) is completely
  independent of the Editor even existing (a `GTE_ENABLE_EDITOR=OFF` build
  simply never pauses at all, since `IsPlaybackPaused()` always returns
  `false` there — confirm this is exactly what happens, not a compile
  error or a silent behavior difference).

### 3.2 Full `ctest` regression pass

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build
ctest -C Debug --output-on-failure
```

Every test must pass — including every pre-existing test untouched by this
campaign (proving PHASE2's refactor and PHASE4's new freeze branch didn't
regress anything), every new `Core/TimeTests.cpp` case from PHASE1 (`Time`'s
own isolated arithmetic), and every new
`Game/GameUpdateFreezeGatingTests.cpp` case from PHASE2 (the real proof that
`Game::Update()` itself actually skips Animation/Physics/GPU-skinning work
on a frozen frame, and resumes/steps correctly) — these two new test files
together are this campaign's actual regression safety net; the live
runtime verification below (3.3) is a confirmatory, best-effort visual
check on top of them, never the only proof this feature works.
Treat any newly-failing test as a real regression to actually fix, per
`AGENTS.md`'s own "Testability & Regression Safety" rule — never loosen a
test's expectation to make it pass without understanding why it failed.

### 3.3 Live runtime verification

Using `run_app_background`/`gte_send_request`/`stop_app_background`:

1. Launch the built `GreatTamanaEngine.exe`.
2. Capture a screenshot (`/get_swapchain`) confirming the Editor loads with
   the new Pause/Step toolbar visible under the menu bar.
3. If at all possible with the available tool set, drive an actual
   pause/step/resume cycle and confirm visually (via repeated
   `/get_swapchain` or `/get_game_view` captures) that:
   - a moving/animating object (e.g. a spawned primitive with a played
     animation, or a dynamic-bone-chain physics rig if a convenient test
     asset already exists) visibly stops moving once paused, across
     several consecutive captures taken a moment apart;
   - clicking Step (if actual button clicks can be driven at all — no
     mouse-control tool is loaded in this environment as of this writing;
     if genuinely not possible, this specific sub-item is a documented
     **manual/human verification note** in the completion report rather
     than a blocking failure of this phase) advances the pose by exactly
     one visible tick and then holds again;
   - resuming continues motion normally, with no visible "jump" from a
     replayed catch-up burst (the Locked Design Decision #11 clamp working
     as intended).
4. Confirm the Scene-view camera can still be panned/rotated/dollied while
   paused (per PHASE0's investigation this requires zero code and should
   already just work — this is a confirmation, not a fix).
5. `stop_app_background` the process.

If the interactive click-driven checks in step 3 truly cannot be automated
with the currently available tools, do not treat that as this phase
failing — record it plainly as an accepted, documented manual-verification
gap in the completion report (see 3.8), and rely on PHASE1's unit tests
(`tests/Core/TimeTests.cpp`, which exhaustively cover `Time::Advance()`'s
exact freeze/step/resume-clamp arithmetic in isolation) PLUS PHASE2's own
`tests/Game/GameUpdateFreezeGatingTests.cpp` (which proves that arithmetic
is actually wired correctly into the real `Game::Update()` entry point —
pose changes when unfrozen, stays frozen when paused, resumes/steps
correctly) as the actual correctness proof for the underlying logic.

### 3.4 `AGENTS.md` update

Add a new section (alphabetically/thematically near "Profiling"/"Job
System" — pick whichever placement reads best once written), e.g.:

```markdown
## Time and Playback Pause

`src/Core/Time.h/.cpp` (`gte::Time`) plus `src/Core/EngineContext.h`
(`gte::EngineContext`) are the engine's dedicated, explicit (never
singleton) per-frame time-keeping objects - `Application` owns the one
`EngineContext` instance, advances its `Time` once per frame
(`Time::Advance()`), and passes it by `const&` into `Game::Update()`,
which uses `Time::IsFrozenThisFrame()` to skip Animation/Physics/GPU-
skinning work entirely on a paused (non-stepping) frame - Unity-style
Pause, driven by a small Pause/Resume/Step toolbar in the Editor
(`src/Editor/PlaybackControls.h/.cpp`, `EditorContext::playbackPaused`/
`stepOneFrameRequested`, `IEditorLayer::IsPlaybackPaused()`/
`TryConsumeStepRequest()`). Rendering, the Editor UI, and the
independently-orbitable Scene-view camera all keep running normally
regardless of pause - only gameplay simulation freezes.

Full convention: [docs/conventions/time-and-playback-pause.md](docs/conventions/time-and-playback-pause.md).
```

(Only add the `docs/conventions/...` link if 3.5 below actually creates
that file — otherwise drop that last sentence, or point it at this
campaign's own `task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md`
instead, matching how some other summaries in `AGENTS.md` reference a
`task_manager/` strategy doc directly rather than a `docs/conventions/`
file.)

### 3.5 `docs/conventions/time-and-playback-pause.md` (recommended)

A full write-up mirroring the depth/structure of an existing file under
`docs/conventions/` (e.g. `docs/conventions/profiling.md` or
`docs/conventions/job-system.md`) — cover: the `Time`/`EngineContext`
API surface, the "called every frame regardless of pause, frozen inside
`Game::Update()`" architecture decision and why (frame-debugger synergy),
the fixed-1/60s Step contract, the resume-clamp behavior, and the toolbar
UI. Add it to `docs/README.md`'s index alongside the other convention
files.

### 3.6 `README.md` "Status" section update

Add a new bullet at the top of the "Status" section (above the existing
`atmosphere-scattering-2` entry), following the exact same voice/format as
every other entry there (bold one-line summary, then a paragraph
mentioning the exact new files/classes and pointing at
`task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md`), e.g.:

```markdown
- **The Editor now has a genuine Unity-style Pause/Step control, backed by
  a brand-new, dedicated, explicit `Time` class** (`frame-debugger-1`
  campaign, five phases -
  `task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md`) - a small
  Pause/Resume + Step toolbar (`src/Editor/PlaybackControls.h/.cpp`) drives
  a new `gte::Time`/`gte::EngineContext` (`src/Core/Time.h/.cpp`,
  `src/Core/EngineContext.h`) that `Application` advances once per frame
  and `Game::Update()` reads to skip Animation/Physics/GPU-skinning work
  entirely on a frozen frame - rendering, the Editor UI, and the
  independently-orbitable Scene-view camera all keep working normally
  while paused, and "Step" advances by exactly one deterministic 1/60s
  tick. Resuming from an arbitrarily long pause is clamped to a single
  ordinary-sized simulation step rather than replaying the entire elapsed
  real-world gap. Verified with a full clean build (both
  `GTE_ENABLE_EDITOR=ON` and `=OFF`), a full `ctest` regression pass, and a
  live runtime smoke test.
```

Move whatever bullet(s) this displaces further down/into
`docs/CHANGELOG.md` if the "Status" section has an established maximum
length convention — check its current state before editing.

### 3.7 `TODO.md` update

Add an entry (or a small new subsection) listing this campaign's own
explicit Non-Goals (from `PHASE0_MASTER_STRATEGY.md`, section 3.4) as
deliberately-deferred future work, e.g.:

```markdown
### Deferred from the `frame-debugger-1` Pause/Time campaign

- A "Stop" button with full scene-state snapshot/revert (Unity's own
  Edit-mode <-> Play-mode split) - this campaign only added Pause/Resume/
  Step on top of the engine's existing, always-running loop.
- A `Time.timeScale` slider (slow-motion/fast-forward) - today's Pause is
  binary (running or fully frozen) only.
- HTTP/network endpoints to control Play/Pause/Step remotely, mirroring
  the existing `network-impl-*` campaigns' AI-agent-facing control surface.
- A keyboard shortcut for Play/Pause (e.g. Unity's own Ctrl+P).
- The actual frame-debugger feature (draw-call/render-pass stepping, a
  Render Graph pass-by-pass inspector) this campaign's `Time`/
  `EngineContext` groundwork exists to support - a future campaign's job.
```

### 3.8 Final campaign report

Write a `CAMPAIGN_COMPLETION_REPORT.md` in
`task_manager/frame-debugger-1/` (mirroring the naming convention other
completed campaigns in `task_manager/` already use for their own final
summary document) tying together all five phases' individual completion
reports into one end-to-end narrative, plus the full build/test/runtime
verification evidence gathered in this phase.

### 3.9 Final `git commit`

Stage and commit every doc file touched in this phase
(`AGENTS.md`, `README.md`, `TODO.md`, `docs/README.md` if changed,
`docs/conventions/time-and-playback-pause.md` if created) together with
this phase's own two report files, as one commit closing out the campaign.
