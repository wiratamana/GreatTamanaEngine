# PHASE6: Application Orchestration Cleanup + Documentation Update

_Child of `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1` through
`PHASE5` already being merged. Part of the `render-pass-1` campaign._

## Step 1: The Goal

Now that every real pass in the engine is declared through the new
`AddRenderPass()` chokepoint (PHASE1-5), do a final consistency/cleanup
pass over `Application::Run()` (`src/Application/Application.cpp`) and
this engine's own documentation (`AGENTS.md`,
`docs/conventions/frame-debugger.md`) so both correctly describe the NEW
architecture instead of the OLD one — leaving no stale doc comment or
reference to `"GameView"`-the-pass, `AddGameViewPass()`, or
`isSkyBackgroundDraw` anywhere. Also delete any code this campaign made
genuinely dead.

## Step 2: The Situation

- `Application.cpp`'s doc comments (the big block-comment above the
  offscreen `RenderGraph::Execute()` call, and scattered inline comments
  throughout the ~1300-line `Run()` function) reference `"GameView"` the
  PASS and `AddGameViewPass()` by name in several places — these are now
  stale after PHASE2/PHASE3.
- `docs/conventions/frame-debugger.md` is an extremely long, historically
  accreted document (every past `frame-debugger-N` campaign appended its
  own `## What's new (frame-debugger-N campaign)` section rather than
  rewriting the whole file) — this campaign should follow the SAME
  convention: ADD a new `## What's new (render-pass-1 campaign)` section
  near the top (right after the intro paragraph, before `## What is real
  today`), rather than rewriting the whole file's history. The `## What
  is real today` section's own tree-shape ASCII diagram (currently
  showing the OLD `"GameView"`-centric shape) DOES need direct editing
  in place (it describes CURRENT behavior, not history) — update it to
  match PHASE4's new tree shape exactly.
- `AGENTS.md`'s own `## Frame Debugger` section (the short summary +
  link every subsystem gets) currently reads: "...one real, individually
  selectable per-entity child leaf under `"GameView"`..." — this needs a
  small wording update (`"GameView"` → `"RenderOpaque"`) plus a one-line
  mention that Sky Background is now its own real, separate pass/leaf,
  linking to this campaign's own new `docs/conventions/` section (see
  3.3 below).
- `AGENTS.md` has NO existing `## Render Pass System` or `## Render Graph`
  top-level section at all today (the `render_graphs` campaign's own
  documentation apparently never got its own `AGENTS.md` summary
  section — confirm this via `search_in_dir` for "Render Graph" in
  `AGENTS.md` before assuming; if one already exists, extend it instead
  of creating a duplicate).

## Step 3: The Plan

### 3.1 — `Application.cpp` comment audit

`search_in_dir` for `"GameView"` (the literal, quoted pass-name string,
as opposed to the `gameTarget`/`m_gameView` variable/member names, which
are UNRELATED and must NOT be renamed — those refer to the RenderTexture/
Editor panel concept, not the pass) across `Application.cpp` and every
comment block near the render-graph `build` lambda. Update every doc
comment that specifically claims `"GameView"` names a PASS (e.g. "must
match RenderPasses.cpp's own AddGameViewPass()/AddSceneViewPass() pass
name literals exactly") to instead say `"RenderOpaque"`/
`AddRenderOpaquePass()`. Do NOT touch comments that correctly refer to
`"GameView"` as a TEXTURE/RenderTarget name (e.g.
`b.ImportTexture("GameView", gameTarget->Target(), ...)` — this texture
name is UNCHANGED by this whole campaign, only the PASS that writes it
changed).

### 3.2 — Delete dead code

Confirm (via `search_in_dir`) that the following are now genuinely,
completely unreferenced anywhere in `src/` or `tests/`, and delete them if
so: `AddGameViewPass()`'s old declaration/definition (should already be
fully renamed away by PHASE2 — if a stale forwarding shim was left behind
"just in case", remove it now), `FrameDebuggerDrawRecord::isSkyBackgroundDraw`
and `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` (should
already be deleted by PHASE4 — if anything was left as dead code, remove
it now). This phase is the final safety net for any such leftovers, not
the primary place either deletion was expected to happen.

### 3.3 — `docs/conventions/frame-debugger.md` update

- Edit the `## What is real today` section's tree-shape ASCII diagram
  in place to match PHASE4's actual shipped shape (see that phase's own
  Step 1 diagram — copy it here, adjusted for whatever the real,
  as-shipped behavior turned out to be if it diverged from the plan in
  any way; document any such divergence explicitly).
- Add a new `## What's new (render-pass-1 campaign)` section (placed
  right after the file's own intro paragraph, before `## What is real
  today`, matching every prior campaign's own placement convention)
  summarizing: the new `PassKind`/`RenderPassCategory` vocabulary; the
  `"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"` split (an
  explicit, user-approved BREAKING CHANGE to the previous single
  `"GameView"` pass's tree shape — cross-reference PHASE0's own Locked
  Design Decision #4); the removal of `isSkyBackgroundDraw`/
  `RecordSkyBackgroundDraw()`; and the new `"Compute LUT"` grouping.
  Mirror the writing style/level of detail every prior
  `## What's new (frame-debugger-N campaign)` section already uses (read
  at least two of them first, e.g. the `frame-debugger-8` and
  `frame-debugger-9` sections, for the expected tone/structure).

### 3.4 — `AGENTS.md` updates

- Update the existing `## Frame Debugger` section's own summary
  paragraph: replace `"GameView"` (pass) references with
  `"RenderOpaque"`, and add one sentence noting Sky Background is now its
  own real, separate, individually-selectable pass.
- Add a new `## Render Pass System` section (or extend an existing
  `## Render Graph`-titled section if `search_in_dir` finds one already —
  see Step 2 above) briefly describing: `src/Renderer/RenderGraph/
  RenderGraphBuilder::AddRenderPass()` is now the ONE official way to
  declare any pass in this engine; `PassKind` (Graphics/Compute) and
  `RenderPassCategory` (General/AtmosphereLut/GpuSkinning/Debug) are the
  two pieces of metadata every real pass declaration stamps; every
  render/compute/blit operation in the engine funnels through a
  Render-Graph-declared pass — link to
  `task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md` for the full
  campaign history, mirroring how other `AGENTS.md` sections link to
  their own `task_manager/*` campaign folders.

## Definition of Done

- `search_in_dir` for `"GameView"` inside `Application.cpp` returns ONLY
  hits that are genuinely about the TEXTURE/RenderTarget name, never
  about a pass name.
- `AddGameViewPass()`, `isSkyBackgroundDraw`, `RecordSkyBackgroundDraw()`
  are confirmed absent from the entire repository.
- `docs/conventions/frame-debugger.md` and `AGENTS.md` both accurately
  describe the shipped, as-built architecture — no reader following only
  these two documents would be surprised by anything the actual code
  does.
- Incremental compile succeeds (this phase is mostly comments/docs, but
  re-verify nothing was accidentally broken by the dead-code deletion in
  3.2); completion report + git commit as usual.

## What We Will NOT Do

- Do NOT rewrite `docs/conventions/frame-debugger.md` from scratch —
  append/edit-in-place only, following its own established historical
  convention (see Step 2 above).
- Do NOT rename `gameTarget`/`m_gameView`/any other RenderTexture-level
  variable or the `"GameView"` TEXTURE import name — only the PASS name
  changed, never the resource/panel name.
- Do NOT start the full build/regression test yet — that is PHASE7 only.
