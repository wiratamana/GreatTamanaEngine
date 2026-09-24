# PHASE5 — Final Full Regression Verification and Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it in full first. Also read
`task_manager/editor-core-separation-1/PHASE19_FINAL_FULL_REGRESSION_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md`
and `task_manager/editor-core-separation-1/CAMPAIGN_COMPLETION_REPORT.md` in
full — this phase is this campaign's own equivalent closeout, and must be
held to the exact same standard of brutal honesty that report set (it did
NOT claim success it hadn't earned; neither should this one).

This is the ONLY phase in this campaign that runs a full clean build and
full `ctest` regression pass — every earlier phase deliberately used
incremental compile checks only, per `PHASE0`'s Universal Rule 4.

## Step 1: The Goal (Where are we going?)

Mechanically re-check every one of the design doc's original Four Hard
Rules (verbatim, restated in `PHASE0_MASTER_STRATEGY.md`, Step 1) against
the REAL, current, post-PHASE1-4 state of this repository — not by re-
reading code and reasoning about it (that is how the false confidence in
`editor-core-separation-1`'s own claims arose in the first place), but by
running the real, mechanical checks: a full clean build, a full `ctest`
pass, both probes, a live HTTP smoke test, and re-stating the Four Hard
Rules' verdicts with fresh evidence — honestly reporting **NO** on anything
that isn't actually, mechanically true today, exactly the way
`editor-core-separation-1`'s own Phase 19/`CAMPAIGN_COMPLETION_REPORT.md`
did.

## Step 2: The Situation (Where are we now?)

Read every `PHASEn_COMPLETION_REPORT.md` (n = 1..4) left in this same folder
by the four prior phases — each may have flagged a deviation, a discovered
fact, or an open question this final phase needs to account for. If ANY of
them reported a genuine unresolved gap (rather than a clean pass), that gap
MUST be reflected honestly in this phase's own Hard-Rule verdicts below — do
not silently claim success PHASE1-4's own reports didn't actually establish.

## Step 3: The Plan — exact steps

### Step 3.1 — Full clean build

```
rmdir /s /q build   (or equivalent - confirm the exact prior build dir name/location first)
cmake -S . -B build -G Ninja
cmake --build build
```
(Use whatever this repo's own documented full-build command actually is —
re-confirm against `BUILDING.md` before running; this document's own
command above is a best-effort placeholder, not guaranteed exact.)

Record the exact step count and whether it was zero-error, mirroring
`editor-core-separation-1` Phase 9/14/19's own "N/N steps, zero errors"
reporting convention.

### Step 3.2 — Full `ctest`

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Record the exact pass/fail/skip counts. Compare against
`editor-core-separation-1`'s own last recorded baseline (Phase 19: "1773
total, 1771 passed, 2 legitimate skips, zero failures") — this campaign
changed test files in PHASE1 (an `#include` path fix in
`tests/Editor/EditorPanelCatalogTests.cpp`/
`tests/Network/ActivateTabEndpointEndToEndTests.cpp`, no new tests), PHASE2
(no test file changes expected, but confirm), and PHASE3 (a REAL, required
test-file fix: `tests/Network/LogEndpointsEndToEndTests.cpp`'s own fixture
must have been updated to wire a real `EditorLogQueryCapability` into its
`NetworkServer` construction, plus one brand-new `TEST` covering the
null-capability-degrades-to-503 path - see PHASE3's own Step 3.6). The total
count should have grown by exactly ONE versus the 1773 baseline (PHASE3's
new null-capability test) — a total that grew by a DIFFERENT amount, stayed
flat, or a passing count that dropped, or any NEW failure, must be
investigated (and, if a genuine regression, fixed via `delegate_task` — see
Step 4).

### Step 3.3 — Both probes, run fresh

**Do NOT pass `--target gte_core` to the first probe** — its outer
meta-project only ever defines ONE real CMake target, the custom target
`gte_core_standalone_probe` itself (its own `ALL`-default target), which
nested-invokes the real inner build; there is no target literally named
"gte_core" reachable from that outer build directory at all. Just run the
plain default build (or pass `--target gte_core_standalone_probe`
explicitly, if you prefer to be explicit rather than relying on the
default `ALL` target):

```
cmake -S tools/ci/gte_core_standalone_probe -B build-core-probe
cmake --build build-core-probe

cmake -S tools/ci/gte_core_player_link_probe -B build-player-link-probe
cmake --build build-player-link-probe
```

Both must succeed. The second one succeeding, for real, is the single most
important piece of evidence this whole campaign produces — it is the first
time in either campaign's history that a REAL EXECUTABLE LINK against
`gte_core.a` alone has been mechanically, permanently, re-runnably proven to
work (`editor-core-separation-1`'s own Phase 19 only ever did this once, by
hand, with a throwaway file it deleted immediately afterward).

### Step 3.4 — Live, HTTP-driven end-to-end smoke test

`run_app_background` the built `GreatTamanaEditor.exe`, then drive it with
`gte_send_request`, mirroring (and extending, for this campaign's own new
surface area) `editor-core-separation-1` Phase 19's own smoke-test table:

| Endpoint | What it re-confirms |
|---|---|
| `GET /get_swapchain` | The Editor still renders correctly end-to-end |
| `GET /list_tabs` / `GET /activate_tab?name=Hierarchy` | PHASE1's `EditorPanelCatalog.h` relocation didn't break tab routing |
| `GET /frame_debugger/open` → `.../enable?value=true` → `.../capture` → `GET /frame_debugger/state` | PHASE2's `IFrameDebuggerCaptureRecorder` conversion didn't break real per-draw recording or replay-step preview generation |
| `GET /get_logs?limit=20` | PHASE3's `ILogQueryCapability` wiring genuinely reaches the real `Logger` singleton |
| `POST /clear_logs` then `GET /get_logs?limit=20` again | Confirms the capability's `Clear()` path genuinely empties the real ring buffer, not a stub |
| `POST /save_scene` (`{"path":""}`) / `POST /load_scene` (`{"path":""}`) | Nothing in the `ISceneIOCapability`/`EngineCommandBridge` path regressed as a side effect of this campaign's own Frame Debugger/Logger changes (these should be completely untouched, but confirm) |

`stop_app_background` when done. Paste every real response in the
completion report — do not summarize as "worked fine" without evidence.

### Step 3.5 — Re-check the Four Hard Rules, with fresh evidence, honestly

Write out each of the four rules (verbatim from `PHASE0_MASTER_STRATEGY.md`)
followed by a plain YES/NO verdict and the SPECIFIC evidence from Steps
3.1-3.4 that justifies it — exactly the rigor
`editor-core-separation-1/CAMPAIGN_COMPLETION_REPORT.md`'s own "Four Hard
Rules — restated" section used. Expected outcome, if PHASE1-4 all genuinely
succeeded:

1. **Rule 1 (zero editor code/ImGui/debug-only feature code/SDL in
   `gte_core`'s own translation units)** — YES: re-confirm via
   `search_in_dir(src, "#include \"[./]*Editor/")` scoped to files OUTSIDE
   `src/Editor/` returns ZERO real matches (PHASE1/PHASE3 closed the three
   that existed) — this is now the FULL bar, not the "partially true"
   verdict `editor-core-separation-1` had to give.
2. **Rule 2 (`gte_editor` may depend on `gte_core`, never the reverse, in
   BOTH `#include`/`target_link_libraries()` AND real-unresolved-symbol
   terms)** — YES: the `RESCAN` removal (PHASE4) succeeding IS the
   mechanical proof of the "no unresolved symbol" half; the `#include`/
   `target_link_libraries()` direction was already correct and remains so.
3. **Rule 3 (a thin Player-build host can link `gte_core.a` alone and get a
   running, renderable engine)** — YES, for the FIRST time in either
   campaign: the new `gte_core_player_link_probe` succeeding is direct,
   reproducible, permanent, mechanical proof — not merely "a throwaway
   experiment happened to work once, then was deleted" the way
   `editor-core-separation-1`'s own Phase 19 evidence was.
4. **Rule 4 (`gte_editor` unconditionally configured every time, no skip
   option except the pre-authorized `GTE_CORE_STANDALONE_PROBE_ONLY`
   CI-only escape hatch)** — YES, unchanged from `editor-core-separation-1`'s
   own already-correct verdict (this campaign added no new way to skip
   `gte_editor` in a normal configure).

**If your own real, fresh evidence contradicts any of the above expected
verdicts, report the REAL verdict, not this expected one.** This document's
own predictions are not a substitute for your own mechanical checks.

## Step 4: If Something Is Still Broken

If Step 3's mechanical checks surface ANY genuine regression or unclosed gap
(a real test failure, a real link failure that persists after
re-investigation, a smoke-test endpoint that stopped working), do NOT patch
it inline as an afterthought inside this closeout phase. Use `delegate_task`
to spin off a focused fix task, giving it: the exact failing check/error
text, which of PHASE1-4's own files are the likely cause, and an instruction
to write its own small `PHASE5_FIX_<short-name>_COMPLETION_REPORT.md` (a
NEW, clearly-named report — not a new numbered phase file) documenting what
it found/fixed, then re-run this phase's own Step 3 checks again from
scratch afterward. Every delegated fix task must also be instructed to use
`ask_questions` per the campaign-wide rule.

## Step 5: `CAMPAIGN_COMPLETION_REPORT.md`

Write `task_manager/editor-core-separation-2/CAMPAIGN_COMPLETION_REPORT.md`
— the single, top-level summary of this whole 5-phase campaign, mirroring
`editor-core-separation-1/CAMPAIGN_COMPLETION_REPORT.md`'s own shape and
tone exactly (What this campaign set out to do / What shipped, phase by
phase / the full clean build + full `ctest` results / the live smoke test
table / the Four Hard Rules restated with evidence / deviations from the
plan, if any / what remains genuinely open, if anything genuinely does).

Do not soften or oversell anything. If, after this whole campaign, some
narrower gap remains (e.g. a brand-new, previously-unknown third free
function this strategy's own research somehow missed), say so exactly as
plainly as `editor-core-separation-1`'s own report said "3 of 4 Hard Rules
fail". The whole point of this campaign is to replace hope with mechanical
proof — the closeout report must reflect exactly what was mechanically
proven, nothing more, nothing less.

- `git_add` + `git_commit` (the completion report, plus any final touch-ups).
- Call `ask_questions` if, after Step 3's checks, you are unsure whether a
  remaining discrepancy is in-scope for this campaign to fix versus a
  pre-existing, unrelated issue to merely flag and hand off.
