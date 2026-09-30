# PHASE7 — Full Regression, Acceptance Tick-Through, and Campaign Closeout

Read `PHASE0_MASTER_STRATEGY.md` in full first. Read the source document's
own "COMBINED TECHNICAL REQUIREMENTS" and "COMBINED ACCEPTANCE CRITERIA"
sections (lines 469-536) in full — this phase's whole job is to tick every
box in that checklist with fresh, real evidence. Read
`PHASE6_COMPLETION_REPORT.md` first.

**This is the ONLY phase in this whole campaign allowed to run a full clean
build and a full `ctest` regression pass** (PHASE0 Rule 3). Every phase
before this one intentionally used only incremental builds + targeted test
filters.

## Step 1: The Goal

Every acceptance-criteria checkbox from the source document's own "COMBINED
ACCEPTANCE CRITERIA" section is individually re-confirmed with fresh
evidence gathered THIS phase (not copy-pasted from an earlier phase's own
report without re-verification). A full clean build succeeds. A full
`ctest` regression pass succeeds with zero unexplained delta versus the
prior known-good baseline (`editor-core-separation-25`'s own final numbers —
2090 tests, 25 legitimate skips, per that campaign's own
`CAMPAIGN_COMPLETION_REPORT.md`). `CAMPAIGN_COMPLETION_REPORT.md` is written,
closing this campaign for good.

## Step 2: The Situation

- Prior known-good baseline (`editor-core-separation-25`,
  `CAMPAIGN_COMPLETION_REPORT.md`): full clean build 624/624 steps, zero
  errors/warnings; full `ctest` 2090 tests total, 100% of executed tests
  passing, 25 legitimate environment-gated skips.
- This campaign's own expected NEW test count (sum across PHASE1-6's own
  completion reports — recompute this fresh from each phase's own reported
  number, do not just trust this document's own arithmetic, since a phase
  may have added more or fewer tests than its own phase file suggested):
  PHASE1 (+2, Buffer Roots), PHASE2 (+2, `WriteTexture` isDepthResource),
  PHASE3 (+7 or so, `BlitSpec`/`ToString`/pure helpers), PHASE4 (+2, Frame
  Debugger audit), PHASE5 (+6 or so, `AddBlitPass()`), PHASE6 (+0 new Tier-1
  tests expected — live/visual only, per that phase's own Step 3 item 8).
  Add up the REAL reported numbers from each phase's own completion report
  before writing this phase's own expected total.
- `RenderGraphBuilderTests.cpp`/`RenderGraphCompilerTests.cpp`/
  `RenderGraphTypesTests.cpp`/`FrameDebuggerSnapshotBuilderTests.cpp` are
  the 4 test files this whole campaign touched — no other test file should
  show any diff.
- This campaign's own new, permanent, real files:
  `src/Editor/BlitValidation.h`/`.cpp` (PHASE6) — the one exception to "no
  new files" (PHASE0 Step 2's own closing bullet already flags this).

## Step 3: The Plan

1. **Full clean build**:
   `cmake --build build --clean-first` (or the project's own documented
   clean-build sequence — confirm the exact command against how
   `editor-core-separation-25`'s own PHASE5 did it, referenced in that
   campaign's completion report, for consistency). Record the exact
   steps-succeeded count and confirm zero errors/warnings (a
   `-Wswitch`-class warning anywhere would mean PHASE3/PHASE4's own audits
   missed a site — treat any such warning as a real, blocking finding, not
   noise, and use `dispatch_sub_agent` to fix it before proceeding).
2. **Full regression**:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`
   — record the exact total/passed/skipped counts. Compare against the
   PHASE2 "Situation" expected-delta arithmetic above. If ANY test fails
   that is not already a documented, pre-existing, environment-gated skip,
   diagnose it (`gcc`/`cmake`/`run_shell`/`search_in_dir` as needed) and use
   `dispatch_sub_agent` to fix whatever is broken — per the top-level
   instructions, do not just report the failure and stop.
3. **Combined Acceptance Criteria tick-through** (source document, lines
   511-536) — re-confirm EACH item, this phase, with fresh evidence:
   - [ ] Buffer-root Tier-1 test passes (re-run
     `ctest -R RenderGraphCompilerTest`, confirm PHASE1's 2 named tests
     specifically, by name, in the output).
   - [ ] Blit Tier-1 test passes, including `isDepthResource` propagation
     AND `drawKind == RenderPassDrawKind::Blit` (re-run
     `ctest -R RenderGraphBuilderTest`, confirm PHASE5's named tests
     specifically).
   - [ ] A Tier-1 test confirms `srcIsDepth`/`dstIsDepth` resolves to
     `VK_FILTER_NEAREST` regardless of stored `filter`, AND a second
     confirms a fully-color spec resolves to `filter` verbatim (re-run
     `ctest -R RenderGraphTypesTest`, confirm PHASE3's named tests).
   - [ ] Blit live/visual proof passes for color-to-color — re-gather THIS
     PHASE (do not just cite PHASE6's own screenshot): `run_app_background`
     the freshly, fully-rebuilt `GreatTamanaEditor.exe`, `gte_send_request`
     `GET /get_texture?texture_name=BlitValidationOutput`, `load_image` it,
     confirm visually correct, `stop_app_background`. This is the "before
     AND after a full clean rebuild" pattern `editor-core-separation-25`'s
     own PHASE5 already established as this codebase's own convention for
     this exact kind of live-proof re-confirmation.
   - [ ] Depth-to-depth blit — restate PHASE6's own finding (verified live
     with evidence, OR explicitly documented as shipped-but-unverified) —
     do not silently upgrade an "unverified" finding to "verified" here
     without actually re-running the check.
   - [ ] Every non-exhaustive `PassKind` comparison identified is converted
     — re-run the FULL grep from PHASE0 Step 2 (regex `\.kind\s*[=!]=`
     across all of `src/`) ONE FINAL TIME, fresh, confirm the exact same 5
     sites in `FrameDebuggerData.cpp` and nowhere else, and confirm (by
     reading the actual current code, not by citing PHASE4's report) that
     exactly 3 of them are the real switch-based dispatch and 2 remain
     documented-safe one-way filters.
   - [ ] Full `ctest` regression pass, zero unexplained delta — already
     covered by Step 3.2 above.
4. **Write `CAMPAIGN_COMPLETION_REPORT.md`** into this same folder,
   mirroring `editor-core-separation-25/CAMPAIGN_COMPLETION_REPORT.md`'s
   own structure and honesty level exactly: the goal, the shape (7 phases +
   this one), final verification numbers, the acceptance checklist
   tick-through with real evidence per item, a "what would the NEXT
   campaign in this series (BIG STEP 3 of 4 — Persistent Resource Cache)
   need to know" section, an honest "permanent limitations" section (at
   minimum: restate whichever of the depth-to-depth path's two outcomes
   actually happened, restate that Sites 1/4 in `FrameDebuggerData.cpp`
   were deliberately left unconverted per Locked Decision 2 and why), a
   full "files changed across the whole campaign" list, and a "delegation
   and ambiguity summary" section covering every real `ask_questions`/
   `dispatch_sub_agent` use across PHASE1-7 (recall: PHASE1-7 must never use
   `delegate_task` themselves, per PHASE0 Rule 4 — any use of that tool
   found in a phase's own diff/report is itself a process violation worth
   flagging here).

## Step 4: Closeout

1. `git_add` + `git_commit` covering the completion report (and any
   regression fixes this phase itself needed to make).
2. `git_status` — confirm a clean working tree before ending this phase.
3. This campaign (`editor-core-separation-26`) is closed. BIG STEP 3 of 4
   (Persistent Resource Cache) may begin as its own, later, separate
   campaign — nothing further is expected here.
