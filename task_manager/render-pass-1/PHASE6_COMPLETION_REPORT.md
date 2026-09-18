# PHASE6 COMPLETION REPORT — Application Orchestration Cleanup + Documentation Update

_Child of `PHASE0_MASTER_STRATEGY.md`. Campaign: `render-pass-1`, branch
`feature/render-pass-impl`._

## Summary

Implemented `PHASE6_APPLICATION_ORCHESTRATION_CLEANUP_AND_DOCS.md` exactly per
its own Step 3 plan (read in full, along with `PHASE0_MASTER_STRATEGY.md` and
`PHASE5_COMPLETION_REPORT.md`, before any file was touched). Confirmed — via
direct `search_in_dir` sweeps of `src/` and `tests/` — that PHASE2/PHASE4/
PHASE5 had already fully deleted every piece of dead code this phase names
(`AddGameViewPass()`, `FrameDebuggerDrawRecord::isSkyBackgroundDraw`,
`FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()`); this phase's own
"final safety net" role therefore found nothing left to delete there. What it
DID find and fix: a small number of genuinely stale, present-tense doc
comments in `src/Application/Application.cpp` (and, while auditing the same
call graph, two adjacent ones in `src/Application/RenderPasses.h`) that still
claimed a pass literally named `"GameView"` exists, plus the two documentation
files (`docs/conventions/frame-debugger.md`, `AGENTS.md`) this phase's own
Step 3.3/3.4 explicitly targets, which had not been touched by any earlier
phase and still described the OLD, pre-`render-pass-1` tree shape as if it
were current behavior. No genuine design ambiguity was hit during
implementation — every judgment call this phase needed (how much of
`docs/conventions/frame-debugger.md`'s "What is real today" section to rewrite
vs. leave untouched, where to place the new `AGENTS.md` section) was resolved
by the phase document's own guidance plus this codebase's own well-established
"narrate history truthfully, only the CURRENT-behavior section needs to stay
accurate" convention (see PHASE1's own completion report for the identical
precedent) — so `ask_questions` was never needed.

## What Changed

### 1. `src/Application/Application.cpp` (Step 3.1 — comment audit)

`search_in_dir` for the literal, quoted string `"GameView"` across this file
found 41 hits. Every one was individually re-checked against the rule in the
phase document (texture/RenderTarget/enum/variable names are unrelated and
must NOT be touched; only a comment that specifically claims `"GameView"`
names a PASS needed fixing):

- **Two real, stale claims fixed** (both inside the block that declares
  `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`/
  `AddRenderTransparentPass()`, immediately below where those three real
  calls already correctly replaced the old single `AddGameViewPass()` call
  during PHASE2):
  - A comment describing `AddFrameDebuggerReplayPasses()`'s own timing
    ("BEFORE any of them (or the real `"GameView"` pass above, which was only
    just DECLARED, not yet executed) actually run...") now reads "...(or the
    real `"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"` passes
    above, which were only just DECLARED, not yet executed)...".
  - A comment describing the Aerial Perspective Composite pass's own
    declaration order ("declared AFTER the `GameView` pass above...") now
    reads "declared AFTER the RenderOpaque/DrawSkyBackground/RenderTransparent
    passes above...".
- **Every other hit confirmed correct, left untouched** — texture/
  render-target names (`b.ImportTexture("GameView", ...)`,
  `"GameViewComposited"`, `"AtmosphereSkyViewLut_GameView"`,
  `"AtmosphereAerialPerspectiveVolume_GameView"`), enum values
  (`FrameCaptureKind::GameView`, `Profiling::GpuPass::GameView`), variable/
  function names (`gameViewProjection`, `gameViewStats`,
  `GameViewTarget()`, `CountGameViewDrawCommandsThisFrame()`,
  `SetGameViewCompositedTexture()`), and one already-correct HISTORICAL
  comment (line ~958, added by PHASE2 itself) that accurately narrates past
  tense ("`Profiling::GpuPass::GameView` used to read a single `"GameView"`
  pass's own `LastKnownStatsFor()` before this campaign split it into three
  real, separate passes...") — this one is definitionally not stale, since it
  correctly describes history, not current behavior.
- Final re-check: `search_in_dir` for `"GameView" pass` (the literal phrase)
  across `Application.cpp` now returns exactly one hit, the historically-
  accurate PHASE2 comment above — confirming the Definition of Done's own
  bullet ("no hits that are about a pass name, except genuinely historical
  ones already correct").

### 2. `src/Application/RenderPasses.h` (found during the same audit pass)

Two adjacent, present-tense doc-comment claims on `AddFrameDebuggerReplayPasses()`
— left over from the `frame-debugger-7` campaign, predating this whole
`render-pass-1` campaign, and never touched by PHASE2/4/5 since those phases'
own scope was the call's `AddRenderPass()` migration, not this doc text — were
corrected for the same reason as above (not explicitly named in PHASE6's own
Step 3.2 list, but a direct, textbook instance of the "final safety net for
any leftover" role Step 3.2 itself describes):

- "adds N debug-only, self-contained Render Graph passes (one per real object
  this frame's `"GameView"` pass will draw)..." → "...this frame's
  `"RenderOpaque"` pass will draw)...".
- "NEVER touches the real `"GameView"` pass/target in any way..." → "NEVER
  touches the real `"RenderOpaque"`/`"DrawSkyBackground"` passes/target in any
  way...".

### 3. `src/`/`tests/` dead-code confirmation (Step 3.2)

Re-ran the exact `search_in_dir` sweeps the phase document names:
`void AddGameViewPass`, `bool isSkyBackgroundDraw`, `void
RecordSkyBackgroundDraw` — **zero hits anywhere under `src/`.** Every one was
already fully deleted by PHASE2 (`AddGameViewPass()` → renamed to
`AddRenderOpaquePass()`, no forwarding shim left behind) and PHASE4
(`isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()` deleted outright, replaced
with doc comments narrating the removal). This phase's own "final safety net"
role therefore confirmed a clean prior migration rather than finding anything
new to delete — consistent with PHASE5's own identical "confirmation only, no
regression found" experience for `AddFrameDebuggerReplayPasses()`.

### 4. `docs/conventions/frame-debugger.md` (Step 3.3)

- Added a new `## What's new (`render-pass-1` campaign)` section, placed
  immediately before `## What is real today` (matching every prior
  `## What's new (frame-debugger-N campaign)` section's own established
  placement/tone/level-of-detail convention — modeled directly on the
  `frame-debugger-8`/`frame-debugger-9` sections per the phase document's own
  instruction). Summarizes: the `"GameView"`-pass split into
  `"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"`; the new
  `"Compute LUT"` grouping; the `"RenderOpaque"` pivot lookup replacing the
  old `"GameView"`-literal search; and an explicit cross-reference to
  PHASE0's own Locked Design Decision #4 (breaking changes are expected and
  documented).
- Rewrote the `## What is real today` section's own tree-shape ASCII diagram
  and every surrounding bullet that stated OLD, now-incorrect facts as if they
  were CURRENT behavior (this section — unlike every `## What's new
  (frame-debugger-N campaign)` section — describes present-tense, current
  behavior, so it needed direct editing in place, not append-only treatment,
  per the phase document's own Step 2 guidance): the tree diagram now shows
  `"Compute LUT"` → `"Compute Dispatches (Pre-GameView)"` → `"RenderOpaque"`
  (with its per-entity children) → `"DrawSkyBackground"` →
  `"RenderTransparent"` → `"Compute Dispatches (Post-GameView)"`, exactly
  matching PHASE0's own target diagram and PHASE4's shipped, live-verified
  shape; the generic-discovery paragraph now correctly describes
  `RenderGraphBuilder::AddRenderPass()` (not the old, separate `AddPass()`/
  `AddComputePass()`) and `RenderGraphPassSnapshot::kind`/`category` (not the
  older plain `bool isComputePass`, itself a stale fact predating even PHASE1
  of this campaign that had never been corrected in this doc file before now);
  the shader/pass-state-reflection bullet and the preview-reconstruction
  bullet both now say `"RenderOpaque"`/`"DrawSkyBackground"`/
  `"RenderTransparent"` wherever they used to say `"GameView"`. Every
  `## What's new (frame-debugger-N campaign)` HISTORICAL section below this
  one (6 through 9) was deliberately left completely untouched, per the phase
  document's own explicit "append/edit-in-place only... do NOT rewrite from
  scratch" rule — those sections correctly narrate what was true AT THE TIME
  of their own campaign, which this campaign does not change.

### 5. `AGENTS.md` (Step 3.4)

- Updated the existing `## Frame Debugger` section's summary paragraph:
  `"GameView"` → `"RenderOpaque"` for the per-entity child-leaf claim, plus a
  new sentence stating Sky Background is now its own real, separate,
  individually selectable `"DrawSkyBackground"` pass/leaf (linking forward to
  the new `## Render Pass System` section below it).
- Confirmed via `search_in_dir` for `"Render Graph"` that no existing
  `## Render Graph`/`## Render Pass System` section existed (3 passing
  in-sentence mentions only, no heading — exactly as PHASE0's own double-check
  report and this phase's own Step 2 predicted) before adding a brand-new
  `## Render Pass System` section, placed directly after `## Frame Debugger`
  (the section that most immediately benefits from linking to it) and before
  `## Job System`: describes `RenderGraphBuilder::AddRenderPass()` as the one
  official way to declare any pass, the `PassKind`/`RenderPassCategory`
  metadata pair, the `"RenderOpaque"`/`"DrawSkyBackground"`/
  `"RenderTransparent"` split and its locked ordering rule, and links to
  `task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md` for full campaign
  history — mirroring every other `AGENTS.md` section's own
  summary-paragraph-plus-"Full convention"-link shape.

### 6. `TODO.md` (found during the same audit, not explicitly named by this
phase's own Step 3, but a direct, one-line instance of the same stale-`"GameView"`-pass problem)

One open-question bullet about the release-build direct-to-swapchain path
("...does not go through `AddGameViewPass()`/`AddAtmosphereCompositePass()` at
all...") was updated to say `AddRenderOpaquePass()` — the still-genuinely-open
question itself (whether that direct-render branch needs its own Sky
Background + Composite treatment) is completely unaffected and left as-is.

## Verification

- `search_in_dir` for `"GameView"` across `src/Application/Application.cpp`:
  re-ran after every edit above; every remaining hit is a texture/render-
  target/enum/variable name or the one already-correct historical PHASE2
  comment — zero hits claim a pass literally named `"GameView"` currently
  exists.
- `search_in_dir` for `void AddGameViewPass` / `bool isSkyBackgroundDraw` /
  `void RecordSkyBackgroundDraw` across `src/`: zero hits (confirmed dead code
  already fully removed by PHASE2/PHASE4).
- Incremental compile: `cmake --build build --target gte_core` — clean,
  2 files recompiled (`RenderPasses.cpp`, `Application.cpp` — both only
  because their own OTHER, unrelated comment lines happened to live in the
  same translation units as this phase's comment-only edits;
  `RenderPasses.h`'s comment-only edit correctly triggered no separate object
  file of its own), zero errors/warnings.
- Incremental compile: `cmake --build build --target GreatTamanaEngineTests`
  — clean link, no test file needed a change (this phase touched zero
  testable logic — every edit was a comment or a Markdown file).
- Incremental compile: `cmake --build build --target GreatTamanaEngine` —
  clean link.
- This phase's own Definition of Done does not call for a live runtime smoke
  test (unlike PHASE4/PHASE5) — per the phase document's own "What We Will
  NOT Do", the full build/regression/live-verification pass is PHASE7's job
  only; this phase's verification is scoped to the grep audits and compile
  checks above.

## Deviations From The Phase Document

None in scope or substance. Two small, deliberate ADDITIONS beyond the
phase document's own explicitly-named file list (both directly consistent
with its own stated "final safety net for any leftovers" framing, not a scope
expansion):

1. Two adjacent stale doc-comment lines in `src/Application/RenderPasses.h`
   (found while auditing `Application.cpp`'s own call graph for context) were
   corrected alongside the two `Application.cpp` fixes, rather than left for
   a hypothetical future phase — same class of fix, same file family, zero
   behavioral risk (comment-only).
2. One stale `AddGameViewPass()` mention in `TODO.md` (an already-known,
   still-genuinely-open item, unrelated to this campaign's own scope) was
   updated to the current function name so a future reader is not misled
   into searching for a symbol that no longer exists.

Neither addition touched any code path, test, or the phase's own named
Definition-of-Done checklist items — both are purely incremental accuracy
fixes discovered in the course of doing the named audit.

## Definition of Done — Checklist

- [x] `search_in_dir` for `"GameView"` inside `Application.cpp` returns ONLY
      hits that are genuinely about the TEXTURE/RenderTarget/enum/variable
      name, or an already-correct historical comment — never a false claim
      about a currently-existing pass.
- [x] `AddGameViewPass()`, `isSkyBackgroundDraw`, `RecordSkyBackgroundDraw()`
      confirmed absent from the entire repository (re-confirmed by this
      phase's own independent `search_in_dir` sweep, not just trusted from
      earlier phases' own completion reports).
- [x] `docs/conventions/frame-debugger.md` and `AGENTS.md` both now accurately
      describe the shipped, as-built architecture (the new `"Compute LUT"` /
      `"RenderOpaque"` / `"DrawSkyBackground"` / `"RenderTransparent"` tree
      shape, the `AddRenderPass()`/`PassKind`/`RenderPassCategory`
      vocabulary) — no reader following only these two documents would be
      surprised by anything the actual code does.
- [x] Incremental compile succeeds (`gte_core`, `GreatTamanaEngineTests`,
      `GreatTamanaEngine` all built clean) — confirming this phase's
      dead-code-deletion double-check (finding nothing to delete) and its
      comment-only edits broke nothing; completion report + git commit
      follow.

## Handoff To PHASE7

Every real pass in the engine is declared through `AddRenderPass()` (PHASE1-5),
`Application::Run()`'s own doc comments and this engine's documentation
(`AGENTS.md`, `docs/conventions/frame-debugger.md`, and the one stale
`TODO.md` mention) now correctly describe the `"RenderOpaque"`/
`"DrawSkyBackground"`/`"RenderTransparent"` shape instead of the old
`"GameView"`-the-pass, and every genuinely dead symbol
(`AddGameViewPass()`/`isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()`) is
confirmed absent from the whole repository. PHASE7
(`PHASE7_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`) can now
proceed with its own full clean build, full `ctest` regression run, and a
live, HTTP-driven Frame Debugger screenshot check confirming the exact tree
shape `PHASE0_MASTER_STRATEGY.md`'s own Step 1 diagram calls for.
