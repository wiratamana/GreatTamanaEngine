# PHASE4 — Docs, Full Build, Full Regression, Live Verification (Campaign Closeout) — COMPLETION REPORT

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE4_DOCS_TESTS_LIVE_VERIFICATION_AND_FULL_BUILD.md` exactly as specified — no
material deviations from the phase document were needed._

## What was done

### 3.1 — Tests (pre-existing coverage confirmed, nothing missing)

Read the actual diffs from Phases 1 and 3 (not just their completion reports) and
confirmed both required test additions are genuinely already in place:

- `tests/Editor/FrameDebuggerDataTests.cpp` has full `ComputeAspectFitImageRect()`
  coverage: `ComputeAspectFitImageRectExactAspectMatchFillsWithNoOffset`,
  `...WiderSourceLetterboxesTopAndBottom`, `...TallerSourcePillarboxesLeftAndRight`,
  `...DegenerateInputFillsAtOrigin`, `...RealWorldCase417x333In800x400Box` — all five
  cases the Phase 1 document's Step 3.4 called for.
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` asserts
  `FrameDebuggerTextureProperty::kind`/`isRenderGraphResource` for every read/write row
  (`ComputeDispatchLeafLabelsReadWriteRowsByRealResourceKind`, extended with `kind`/
  `isRenderGraphResource` checks for all six Texture/Buffer/VolumeTexture read+write
  combinations) plus a dedicated `MaterialTextureRowIsNeverARenderGraphResource` test
  proving a `BuildGameViewDrawRecordLeaf()` "Material Texture" row has
  `isRenderGraphResource == false`.

No gap found — nothing needed to be added in this phase, per the phase document's own
"this phase only RUNS them" framing.

### 3.2 — Full build

```
cmake --build build
```
(Working directory: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`.) Result:
`ninja: no work to do` — every target was already up to date from the narrow,
per-phase incremental compile checks Phases 1–3 already ran successfully, and no file
changed since then until this phase's own doc-only edits (which do not affect any
compiled target). This is a genuine, successful full-build confirmation, not a skipped
step — Ninja's own dependency graph re-verified every target's inputs and found nothing
stale.

### 3.3 — Full regression test

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

**Result: 100% tests passed, 1565/1565** (one additional test,
`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, reports
`***Skipped` — an existing, pre-existing, environment-conditional smoke test that only
runs when a specific MMD model file happens to be present on the local machine; this is
long-standing, unrelated-to-this-campaign behavior, not a new gap). Total test time:
121.67 seconds. Zero newly-failing tests — no regression was introduced by any of the
three phases' combined changes.

### 3.4 — Live, HTTP-driven, screenshot-verified proof

Launched `build\GreatTamanaEngine.exe` via `run_app_background`, then drove it over the
embedded HTTP server:

1. `GET /frame_debugger/open` → `"windowOpen":true` — window opened.
2. `GET /frame_debugger/enable?value=true` → `"enabled":true`. A follow-up
   `GET /frame_debugger/state` confirmed `"hasCapturedFrame":true`,
   `"totalEventCount":8` — the deferred-capture mechanism (from `frame-debugger-7`,
   unrelated to this campaign but exercised as a side effect) landed correctly.
3. **Feature 1 check**: `GET /get_swapchain` (nothing selected yet, default
   `PostComposite` whole-frame preview) — **screenshot confirms the real 417×333 Game
   View texture is shown correctly PILLARBOXED with solid black bars left/right**
   inside the wider preview box, not stretched.
4. **Feature 1 + Feature 3 check together**: `GET
   /frame_debugger/select_event?index=2` (the `AtmosphereSkyViewLutPass` compute
   leaf) → `GET /get_swapchain` — **screenshot confirms**: the tree highlights
   `AtmosphereSkyViewLutPass`, the frame-step slider/label read "3 of 8" (matching
   `selectedEventIndex: 2`, 0-based), the preview box correctly shows the
   `"Nothing drawn yet at this point in the frame."` placeholder (a Pre-GameView
   compute leaf, `NotYetDrawn` step-preview kind) on the same solid-black background,
   and the Event Details section shows `Event #2: Compute Dispatch` /
   `Shader: AtmosphereSkyViewLutPass` / `Pass: AtmosphereSkyViewLutPass` correctly.
5. **Feature 3 sequence check**: `GET /frame_debugger/select_event?index=5` → `GET
   /get_swapchain` — **screenshot confirms** the slider/label now reads "6 of 8"
   (matching `selectedEventIndex: 5`), the tree highlight moved to the `"GameView"`
   node, the preview box now shows the real pillarboxed whole-frame image again (this
   event's step-preview kind is not `NotYetDrawn`), and the Event Details section
   correctly shows `Event #5: Draw Mesh` / `Shader:
   AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag` / `Pass: GameView` /
   `Blend: Opaque (no blend)` / `ZTest: Less` — end-to-end proof that selection
   tracking through the new `SetSelectedEventIndex()` chokepoint (Phase 2) stays fully
   correct across repeated HTTP-driven index changes, exactly mirroring what a live
   mouse-drag/arrow-key-nudge would produce through the very same code path. As
   `PHASE2_COMPLETION_REPORT.md` already noted, this environment's automation toolset
   for the running Editor is HTTP-only — there is no mouse/keyboard input-injection
   tool available for this native SDL/Vulkan window, so a literal mouse-drag or
   key-press could not additionally be performed; this indirect-but-same-chokepoint
   confirmation is the phase document's own explicitly endorsed verification method
   (Step 3.4 point 4).
6. **Feature 2 check**: Verified via code inspection only — an actual mouse click on a
   "View" `SmallButton` inside the "ShaderProperties" tab could not be performed in
   this session, for the same "HTTP-only automation, no mouse/keyboard injection tool"
   reason `PHASE3_COMPLETION_REPORT.md` already documented honestly. No HTTP route
   exists for this by design (Feature 2 is explicitly scoped as a hand-driven UI
   button, never a new automation-facing endpoint, per `PHASE0_MASTER_STRATEGY.md`'s
   Locked Design Decision #3). This is a testability/tooling gap, not a code defect —
   Phase 3's own thorough line-by-line code review against its own exhaustive Step
   3.4/3.5 code samples, its Step 3.7 risk checklist (re-confirmed against the actual
   shipped code), and this phase's own confirmation that the two required Tier-1 test
   additions (`kind`/`isRenderGraphResource` assertions) are genuinely in place, remain
   the verification bar for this one sub-feature — consistent with `AGENTS.md`'s own
   accepted position that "the absence of automated Tier 2 coverage should never
   itself slow down or stop feature work."
7. `GET /frame_debugger/enable?value=false` → `"hasCapturedFrame":false` — clean
   teardown of the captured frame.
8. `stop_app_background` closed the Editor process — no crash.

### 3.5 — Documentation updates

- **`docs/conventions/frame-debugger.md`** — added a new `## What's new
  (\`frame-debugger-9\` campaign)` section (inserted immediately after the existing
  `frame-debugger-8` section, before the older `## Known limitation, now fixed
  (\`frame-debugger-4\` campaign)` section), following the exact established heading/
  tone/level-of-detail pattern of the `frame-debugger-6`/`-7`/`-8` sections already in
  that file. Summarizes all three features (aspect-fit preview, draggable/arrow-key
  slider + `SetSelectedEventIndex()` chokepoint, on-demand shader-property texture
  preview + its `kind`/`isRenderGraphResource` gating and explicit Material-Texture
  scope exclusion), plus this phase's own live-verification summary and the honest
  Feature-2 click-through limitation.
- **`AGENTS.md`** — re-read the existing "Frame Debugger" paragraph fresh, as required.
  **No edit was made.** The paragraph never claims the preview is stretched, never
  claims texture previews are unavailable, and never claims the frame-step slider is
  non-interactive — it is a dense, evergreen summary of capture/preview/HTTP-automation
  mechanics that stays accurate after this campaign's changes (the three features this
  campaign added are all UI/interaction refinements layered on top of the exact
  mechanisms this paragraph already describes correctly: real pass-level capture, the
  per-object accumulated-preview mechanism, and the `/frame_debugger/*` HTTP route
  family). No concrete inaccuracy was found, so per the phase document's own
  instruction, this is being explicitly noted rather than silently skipped.

## Deviations from the strategy document

None. Every deliverable in Step 3.8 of
`PHASE4_DOCS_TESTS_LIVE_VERIFICATION_AND_FULL_BUILD.md` was produced exactly as
specified, including the honest, explicitly-documented Feature-2 click-through
limitation (a testability/tooling gap in this environment, not a code defect — no
`bug_report` was filed, since no tool actually malfunctioned).

## Files changed

- `docs/conventions/frame-debugger.md`
- `task_manager/frame-debugger-9/PHASE4_COMPLETION_REPORT.md` (this file)
- `task_manager/frame-debugger-9/CAMPAIGN_COMPLETION_REPORT.md` (written alongside this
  report — see that file for the whole-campaign summary)
