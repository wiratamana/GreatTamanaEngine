# PHASE4 — Completion Report: Docs, Regression, Live Verification, and Full Build

_Reports to `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE4_DOCS_REGRESSION_LIVE_VERIFICATION_AND_FULL_BUILD.md` (v2, including
its addenda) in full. This is the closing phase of the `frame-debugger-8`
campaign — see `CAMPAIGN_COMPLETION_REPORT.md` in this same folder for the
full four-phase writeup._

## Summary

Re-read `PHASE4_DOCS_REGRESSION_LIVE_VERIFICATION_AND_FULL_BUILD.md` and
`PHASE0_MASTER_STRATEGY.md` fresh from disk per this task's own instructions,
confirmed PHASE1/PHASE2/PHASE3 are all already complete and committed
(clean `git status`, all three `PHASEn_COMPLETION_REPORT.md` files present
and internally consistent with each other's "Notes for next phase" sections),
spot-checked two of the four v2-addendum doc-comment fixes directly against
the current source (both confirmed correct), then executed this phase's own
three-part closing job: documentation, the one full build + full regression
this whole campaign earns, and a live, HTTP-driven, screenshot-verified smoke
test.

## What was done (maps 1:1 to the phase document's Step 3)

- **3.1(a) — `docs/conventions/frame-debugger.md`'s existing "What is real
  today" section updated in TWO places**, exactly per the phase document's
  own verbatim instructions:
  1. The ASCII tree diagram under the "Every real compute-shader dispatch..."
     bullet gained a new line under `"GameView"` leaf's children, describing
     the new Sky Background child leaf.
  2. The "exactly ONE Pipeline configuration today... nothing to fabricate
     per-mesh here" bullet gained a new appended sentence (the existing
     sentence itself was left untouched, per the phase document's explicit
     instruction) noting the Sky Background leaf is the one other real,
     distinct pipeline-state fact in this window.
- **3.1(b) — a brand-new "## What's new (`frame-debugger-8` campaign)"
  section** was inserted immediately after "## What's new (`frame-debugger-7`
  campaign)" and immediately before "## Known limitation, now fixed
  (`frame-debugger-4` campaign)" — using the phase document's own verbatim
  markdown block, unmodified.
- **3.2 — `AGENTS.md`'s "Frame Debugger" section** gained the one specified
  appended clause to its existing per-entity-child-leaf sentence, without
  restructuring the paragraph.
- **3.3 — Full build, both `GTE_ENABLE_EDITOR` configurations**:
  - `cmake --build build` — succeeded (single relink step; everything was
    already up to date from PHASE1-3's own incremental builds).
  - `cmake --build build-editor-off` (v2 addendum) — succeeded (a full
    55-step build, since this directory's own binaries had not yet been
    rebuilt this session; confirmed clean, no errors).
  - `ctest -C Debug --output-on-failure` (against `build/`) — **100% pass,
    1559/1559 run tests**, 1 pre-existing, environment-dependent skip
    (`PmxLoaderRealModelSmokeTest`, unrelated to this campaign). Zero
    regressions anywhere in the suite, including every new test PHASE1/PHASE3
    added.
- **3.4 — Live, HTTP-driven, screenshot-verified smoke test**: launched
  `build/GreatTamanaEngine.exe` via `run_app_background`, drove the Frame
  Debugger entirely over its `/frame_debugger/*` HTTP routes plus
  `POST /instantiate_primitive`/`POST /delete_entity`, and visually confirmed,
  via `GET /get_swapchain` screenshots:
  - On the project's actual default scene (Camera + Directional Light only,
    ZERO mesh entities): the event tree shows exactly one leaf under
    `"GameView"` — the Sky Background leaf — with the correct `Pass`/
    `Shader`/`ZTest`/`ZWrite` Inspector rows and a correct, pure sky-only
    preview image.
  - After spawning a real mesh entity (`PhaseCheckCube`) and re-capturing:
    selecting the LAST entity's own leaf shows the cube WITHOUT the sky;
    selecting the new dedicated sky leaf immediately after it shows the SAME
    cube WITH the sky — a real, visible, screenshot-confirmed difference,
    directly proving the PHASE2 replay-preview bug fix end-to-end.
  - Cleaned up: deleted the spawned entity, disabled the Frame Debugger
    (confirmed the capture was cleared), and stopped the background process.
- **3.5 — `CAMPAIGN_COMPLETION_REPORT.md`** written in
  `task_manager/frame-debugger-8/`, mirroring
  `task_manager/frame-debugger-7/CAMPAIGN_COMPLETION_REPORT.md`'s own
  structure (read first as a template, per this phase's own instruction).

## Deviations from the plan

None. Every Step 3.x item was implemented literally as specified in the v2
phase document, including both v2 addenda (the `build-editor-off` build and
the two "What is real today" section edits). No new gaps or surprises were
discovered while implementing this phase — PHASE1/PHASE2/PHASE3's own
completion reports and "Notes for next phase" sections were all internally
consistent and accurate, and every artifact they claimed to have produced
(new tests, doc-comment fixes, the restructured replay-pass function) was
independently confirmed present and correct in the actual current source
before this phase made any of its own edits.

## Full build and regression results (this phase's own scope — the only
phase in this campaign allowed to run these)

- `cmake --build build` — **succeeded** (1/1 step, clean relink).
- `cmake --build build-editor-off` — **succeeded** (55/55 steps, clean).
- `ctest -C Debug --output-on-failure` — **100% pass, 1559 tests run, 1559
  passed** (0 failed), 1 additional test reported `Skipped` (environment-
  dependent, pre-existing, unrelated to this campaign). Total test time:
  108.18 seconds.

## Live verification results (this phase's own scope)

See `CAMPAIGN_COMPLETION_REPORT.md`, Section 5, for the full request/response
transcript and screenshot descriptions. Both halves of this campaign's
Definition of Done requiring a live running engine (the zero-mesh-entity
scene, and the normal scene with at least one mesh entity) were independently
proven, with real HTTP requests and real screenshots — not just reasoned
about from the code.

## Definition of Done (this phase, and the whole campaign) — verified

- [x] `docs/conventions/frame-debugger.md` (both its updated "What is real
      today" section AND its new "What's new (`frame-debugger-8` campaign)"
      section) and `AGENTS.md` are all updated per Step 3.1/3.2.
- [x] `cmake --build build` succeeds cleanly.
- [x] `cmake --build build-editor-off` succeeds cleanly (v2 addendum).
- [x] `ctest -C Debug --output-on-failure` shows 100% pass, including every
      new test from PHASE1/PHASE3.
- [x] The live smoke test (Step 3.4) confirms every bullet in
      `PHASE0_MASTER_STRATEGY.md`'s own campaign-wide Definition of Done.
- [x] `CAMPAIGN_COMPLETION_REPORT.md` exists and is accurate.

## Notes

No blockers, no discovered deviations requiring a design re-think, and no
regression needed fixing (the campaign was left in a fully green state by
PHASE1/PHASE2/PHASE3, exactly as their own completion reports claimed). This
closes out the `frame-debugger-8` campaign.
