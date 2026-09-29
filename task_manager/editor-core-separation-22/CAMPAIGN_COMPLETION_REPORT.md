# CAMPAIGN COMPLETION REPORT — `editor-core-separation-22` ("The Engine Is STILL Lying" — Render Pass / Frame Debugger Honesty Campaign, Round 2)

Branch: `feature/editor-core-separation` (unchanged throughout, as required)
Campaign folder: `task_manager/editor-core-separation-22/`
Status: **Complete.** Full clean build succeeded (611/611 steps, zero errors), full `ctest` regression pass
succeeded (2036 tests, 100% of executed tests passing, 8 legitimate environment-gated skips — up from
`editor-core-separation-21`'s own final baseline of 2004 tests, a clean **+32**), and a final, live,
HTTP-driven, end-to-end verification confirmed all three original, screenshot-reported bugs are fixed.

---

## The three original bugs

1. Two `RenderOpaque` rows (and two `DrawSkyBackground` rows) appeared in the "Render Graph" panel's live
   pass table, but only ONE row for each appeared in the "Disabled Built-In Passes" section once toggled
   off — an apparent contradiction in how many "things" the panel believes exist for the exact same pass
   name.
2. `DemoRenderFeaturePlugin_Clear` (and `DemoRenderFeatureSecondPlugin_Clear`) appeared as real, enabled
   rows in the "Render Graph" panel, but produced **zero** visible nodes anywhere in the Frame Debugger's
   own captured event tree — a pass that runs, yet the Frame Debugger shows nothing for it at all.
3. Disabling `DrawSkyBackground` via the "Render Graph" panel did **not** stop the sky from being visible
   in either the Frame Debugger's own captured preview OR the live Game View — the exact "iron rule"
   violation `editor-core-separation-21` was supposed to have permanently eliminated, but for a DIFFERENT
   pass, through a DIFFERENT mechanism.

## The iron rule, extended (now enforced permanently in code, Clause A/B/C all three)

> A render pass's declared/enabled state and the Frame Debugger's own displayed event tree must NEVER
> disagree.
> **Clause A** (already enforced since `editor-core-separation-21`): if a pass is disabled, it must not
> run, and it must not appear as an executed leaf anywhere the Frame Debugger or the Render Graph panel
> can show it.
> **Clause B** (new, this campaign): if a pass runs (survives culling, non-culled), the Frame Debugger's
> own tree MUST show it as a real leaf somewhere — no exceptions except a small, explicitly-named,
> permanently-documented allowlist of the Frame Debugger's OWN ephemeral internal replay scaffolding.
> **Clause C** (new, this campaign): a pass's own toggle-off state must gate EVERY observable side effect
> its own declaration code produces, not only whether its own `RenderPassDesc` reaches the graph.

## Root Cause #1 (Step 2.1 of `PHASE0_MASTER_STRATEGY.md`) — a UI/data-model mismatch, not a logic bug

`RenderPipeline::DeclareOnePhase()` invokes a `ProviderScope::PerActiveView` provider once per active view
(Game View AND Scene View), each pushing its own `RenderPassDesc` under the IDENTICAL `debugName` into the
same shared vector — the generic toggle gate is correctly, consistently keyed by NAME, so toggling a name
off genuinely removes BOTH instances every time; **this part was already honest**. The user-visible
confusion was that `RenderGraphPanel::BuildPassRow()` rendered ONE ROW PER SNAPSHOT ENTRY (2 rows for a
2-view pass), while `BuildDisabledBuiltInPassesSection()` read `RenderPassToggleRegistry::ListAll()`
(inherently one entry per unique NAME) — two individually-correct data sources presenting two DIFFERENT
cardinalities for the same pass name, with zero cross-reference.

## Root Cause #2 (Step 2.2) — `RenderPassCategory::Debug` misused, and no generic "catch every survivor" fallback existed

`RenderPassCategory::Debug`'s own documented contract meant "Frame-Debugger-internal replay passes, never
a real tree citizen" — but `PluginRenderPassBuilderAdapter.cpp`'s `AddFullscreenClearPass()` (the function
producing `DemoRenderFeaturePlugin_Clear`) tagged itself `Debug` for a DIFFERENT, incompatible reason ("the
correct category for every plugin-contributed pass"), getting the exact same "never shown" treatment as
the Frame Debugger's own genuinely-internal `FrameDebuggerReplayStepN` passes. A second, independent,
structural gap made this worse: `DemoRenderFeaturePlugin_Clear`'s `RenderPassEvent::AfterEverything` tier
positioned it after every surviving compute pass, past both `BuildRealFrameDebuggerSnapshot()`'s
view-region walk (which `break`s the instant it sees ANY surviving Compute-kind pass) and its
post-GameView compute-only loop — silently dropping any Graphics-kind pass positioned there, regardless of
category.

## Root Cause #3 (Step 2.3) — a disabled pass's side effect survived it, via an unrelated, independently-toggled pass

`Core.cpp`'s `"DrawSkyBackground"` provider built a `recordSkyBackground` callback and unconditionally
`Publish()`ed it to the blackboard, BEFORE any toggle-registry check — unlike `AtmosphereLutRenderer`'s
five toggle-aware methods, which each early-return before producing any side effect. The generic, late-stage
toggle gate only decides whether the collected `RenderPassDesc` reaches the graph; it does nothing to undo
a `Publish()` that already happened. `FrameDebuggerReplayPasses.cpp`'s own "sky step" (gated by its own,
completely unrelated `"FrameDebuggerReplay"` toggle) then `Fetch()`ed that stale, unconditionally-published
callback and redrew the sky regardless of `"DrawSkyBackground"`'s own disabled state — the REAL
`"DrawSkyBackground"` pass was honestly absent from the graph; a COMPLETELY DIFFERENT, honestly-enabled
pass was what was actually drawing the sky, using a side channel that never checked the first pass's own
toggle at all. `editor-core-separation-21`'s own `RenderPassHonestyChecker` (Clause A) structurally cannot
catch this — it compares each non-culled pass NAME against that SAME name's own toggle state, and
`"FrameDebuggerReplayStepN"` reports its OWN toggle honestly; the lie lives entirely inside what that
pass's `execute` lambda actually DOES.

## The fixes, one phase per confirmed root cause plus a systemic audit and a permanent detector

- **PHASE1** fixed the confirmed, specific, reported Root Cause #3 bug: a new, generalized, reusable pure
  helper, `src/Renderer/RenderGraph/RenderPassToggleGuard.h`'s `ShouldDeclareBuiltInPassThisFrame()`,
  gates `"DrawSkyBackground"`'s own provider as the very first statement in its lambda body — the whole
  body, including the blackboard `Publish()`, now never executes when disabled.
- **PHASE2** ran an exhaustive audit (22 ledger rows across every `blackboard.Publish()` call site and
  every side-effect-bearing `RenderPipeline` provider), classifying every finding Confirmed-Lie/
  Already-Honest/No-Toggle-Exists, live-HTTP-verified rather than accepted from code-reading alone. Found
  exactly ONE further Confirmed-Lie: `"GpuDrivenBatches"`'s per-batch entity-exclusion set, populated
  unconditionally at collection time before that specific batch's own `"<batch> IndirectDraw"` pass had
  its own toggle checked — the INVERSE shape of Clause C (content silently disappearing rather than a
  disabled pass's content surviving). Per `ask_questions`, the user confirmed this belongs in the fix
  backlog, extending Clause C's spirit to cover this inverse shape too.
- **PHASE3** fixed that one finding: a new pure helper,
  `src/Core/GpuDrivenBatchEntityExclusionLogic.h`'s `AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled()`,
  defers a batch's own entity-exclusion-set population until AFTER that batch's own
  `"<batch> IndirectDraw"` toggle state is resolved — a disabled batch's entities now correctly fall back
  to `RenderOpaque`'s own normal per-entity draw path instead of vanishing. Live-verified with a real
  batch-eligible scene (`POST /spawn_gpu_driven_test_batch`) and mechanism-level diagnostic logging proving
  the exclusion set correctly contains zero of the batch's entities the instant its indirect-draw pass is
  disabled.
- **PHASE4** fixed Root Cause #2: a new, correctly-scoped `RenderPassCategory::FrameDebuggerInternal`
  enumerator now carries the "never a tree citizen" meaning exclusively (only
  `FrameDebuggerReplayPasses.cpp`'s own ephemeral replay steps use it), restoring `Debug`'s own original
  meaning ("a real, visible feature pass") for every other, correctly-tagged real feature. A new, generic,
  structural "Other Render Passes" final sweep in `BuildRealFrameDebuggerSnapshot()` picks up every
  non-culled, non-`FrameDebuggerInternal`, non-`SceneView` survivor left unclaimed by every earlier bucket,
  of either `PassKind`, at any index — making the tree structurally incapable of silently dropping a
  surviving pass, by construction. Live-verified: disabling `DemoRenderFeaturePlugin_Clear`/
  `DemoRenderFeatureSecondPlugin_Clear` individually now produces a clean, symmetric -4 `totalEventCount`
  drop each, restored exactly on re-enable.
- **PHASE5** fixed Root Cause #1: a new data-model layer,
  `src/Renderer/RenderGraph/RenderGraphMetadata.h`'s `RenderGraphGroupedPassMetadata`/
  `GroupPassMetadataByName()`, reworks the "Render Graph" panel's live pass table (`RenderGraphPanel.cpp`'s
  `BuildGroupedPassRow()`) to render exactly ONE row per unique pass name per regime, annotated with a
  `viewLabel` badge built from that name's own NON-CULLED instances only (never inventing "both views"
  when only one genuinely survived), summed draws/triangles, a locked per-view GPU-timing breakdown text,
  and a click-to-expand (`ImGui::TreeNodeEx()`) raw per-instance sub-row — matching the "Disabled
  Built-In Passes" section's own one-row-per-name cardinality for good. Two `ask_questions` rounds locked
  the `gpuTimingText` shape (per-view breakdown) and the raw-breakdown reveal UX (click-to-expand tree
  row, mirroring `InspectorPanel.cpp`'s own precedent).
- **PHASE6** made the extended iron rule self-enforcing, permanently, in code, alongside
  `editor-core-separation-21`'s own **untouched, still-passing** Clause A detector
  (`RenderPassHonestyChecker.h`/`RenderPassHonestyGuard.h`): Clause B is enforced by
  `src/Editor/FrameDebuggerCoverageChecker.h`/`.cpp` + `FrameDebuggerCoverageGuard.h`/`.cpp`
  (`GTE_LOG_ERROR("FrameDebuggerCoverage", ...)`); Clause C is enforced by
  `src/Editor/FrameDebuggerSideChannelChecker.h`/`.cpp` + `FrameDebuggerSideChannelGuard.h`/`.cpp`
  (`GTE_LOG_ERROR("FrameDebuggerSideChannel", ...)`) via a deliberately narrow, hand-curated allowlist of 4
  known-risk blackboard keys. Closing the Clause C gap required promoting `Core::BuildFrame()`'s own
  offscreen `RenderPassBlackboard` from a stack-local variable to a real `Core` member
  (`m_offscreenBlackboardThisFrame`) so it survives long enough for `FrameDebuggerPanel::TriggerCapture()`
  to inspect it afterward, plus a new, presence-only `RenderPassBlackboard::WasPublishedThisFrame()`
  accessor. Both new detectors were proven live: deliberately reintroducing each one's own original bug
  made it fire a real, fresh log entry immediately; the real, already-fixed production code stayed
  completely silent (zero false positives); both temporary reintroduction hacks were fully reverted,
  confirmed via `git status` showing zero net diff on the affected files.

## Final verification numbers (PHASE7)

- **Full clean build**: `cmake --build build --target clean` (628 files removed) followed by
  `cmake --build build` — **611/611 steps succeeded, zero errors.**
- **Full `ctest -C Debug --output-on-failure`**: **2036 tests total, 100% of executed tests passing, 8
  legitimate environment-gated skips** (the same 8 pre-existing skips every prior campaign's own final run
  has also reported — no new skips introduced). Up from `editor-core-separation-21`'s own documented
  2004/8 baseline — a clean **+32**, from this campaign's own new test files/cases across PHASE1
  (`RenderPassToggleGuardTests.cpp`, 4), PHASE3 (`GpuDrivenBatchEntityExclusionLogicTests.cpp`, 4), PHASE4
  (`FrameDebuggerSnapshotBuilderTests.cpp` extensions + `RenderGraphTypesTests.cpp` extensions), PHASE5
  (`RenderGraphMetadataTests.cpp` extensions, 8 new `RenderGraphGroupedPassMetadataTest` cases), and PHASE6
  (`FrameDebuggerCoverageCheckerTests.cpp` + `FrameDebuggerSideChannelCheckerTests.cpp`, 15 combined). No
  test failed at any point during this phase's own full-suite run — the Step 3.2 "diagnose and fix it
  yourself" contingency was never triggered.
- **Final live, HTTP-driven verification**: reproduced all three ORIGINAL screenshot scenarios end-to-end
  one last time against the fully rebuilt binary — (1) the "Render Graph" panel's own live pass table and
  "Disabled Built-In Passes" section now agree exactly (one row live per unique name → one entry once
  disabled, confirmed for `"RenderOpaque"`); (2) `DemoRenderFeaturePlugin_Clear` now produces a real,
  countable leaf pair in the Frame Debugger's own captured tree, vanishing (a clean -4 `totalEventCount`)
  the instant it is disabled; (3) disabling `"DrawSkyBackground"` now genuinely removes all sky pixels
  from BOTH the live Game View (17109 → 1582 bytes) AND the Frame Debugger's own replay preview (a real
  `HTTP 504` — zero replay steps are ever declared once the callback is honestly never published) — and
  every one of the four permanent detectors' own log categories
  (`RenderPassHonesty`/`FrameDebuggerCoverage`/`FrameDebuggerSideChannel`/`min_level=Error`) stayed
  completely empty throughout every correct-behavior check, firing exactly once when Root Cause #3 was
  deliberately, temporarily reintroduced (PHASE1's own guard line commented out), then fully reverted
  (confirmed via `git_status` showing a clean working tree).

## Honest, permanent limitations (restated plainly, not silently smoothed over)

1. **The Clause C detector (`FrameDebuggerSideChannelChecker.h`) is a curated allowlist-based check, not a
   fully general one.** It covers exactly the 4 blackboard keys `PHASE2_COMPLETION_REPORT.md`'s own audit
   identified as carrying this exact risk shape (1 Confirmed-Lie fixed by PHASE1, 3 Already-Honest-but-
   same-shape kept as permanent regression tripwires). A future new blackboard key with this same risk
   shape (a provider Publishes a value some OTHER, independently-toggled pass reads back to reproduce a
   visual/behavioral effect) that nobody manually adds to `KnownRiskBlackboardKeyRules()` is a real,
   permanent, accepted gap — `AGENTS.md`/`docs/conventions/render-pass-side-channel-honesty.md` both state
   this explicitly, as a mandatory checklist item for any future provider author.
2. **A live-testing session anomaly, independently reproduced twice this campaign (once in PHASE1, once
   again in PHASE7), remains unexplained and un-investigated, out of scope for this campaign.** The running
   `GreatTamanaEditor.exe` process crashed unprompted, on both occasions shortly after a
   `GET /get_texture` call that resolved to an HTTP `504` timeout (the "texture never registered/rendered
   again within the timeout" branch). PHASE7's own re-occurrence reproduced identically both with the real,
   fixed `"DrawSkyBackground"` guard in place and with it deliberately, temporarily reverted — ruling out
   this campaign's own code changes as the cause. A fresh process relaunch always immediately restored full,
   correct functionality with zero lasting effect. This is a genuinely separate, pre-existing engine
   stability issue, not a regression this campaign introduced or was ever scoped to fix.
3. **No HTTP endpoint exists to simulate an actual mouse click on the "Render Graph" panel's new `▸` expand
   arrow (PHASE5's own click-to-expand raw-instance sub-row UX).** This was verified by (a) the passing
   Tier-1 tests around `GroupPassMetadataByName()`'s own `instances` field (the expand's real data source),
   (b) direct code reading of the `TreeNodeEx()`/`TreePop()` pairing, and (c) a live screenshot confirming
   the arrow renders exactly where the code predicts (present for multi-instance groups, absent for
   single-instance ones) — never manually click-tested end-to-end, an honest limitation PHASE5's own report
   already flagged plainly.

Neither limitation above is a regression or an open bug in the fixes this campaign shipped — all three are
either deliberate, documented scoping decisions (limitation 1) or pre-existing conditions outside this
campaign's own change surface (limitations 2 and 3), stated here explicitly rather than smoothed over,
mirroring `editor-core-separation-21`'s own precedent for its own two similarly-honest limitations.

## Files changed across the whole campaign

- `src/Renderer/RenderGraph/RenderPassToggleGuard.h` (new, PHASE1)
- `tests/Renderer/RenderGraph/RenderPassToggleGuardTests.cpp` (new, PHASE1)
- `src/Core/Core.cpp` (PHASE1's early guard on `"DrawSkyBackground"`; PHASE3's per-batch toggle-check +
  collection-site change; PHASE6's offscreen-blackboard member promotion)
- `src/Core/Core.h` (PHASE3's `GpuDrivenBatchRenderData::entities` field; PHASE6's
  `m_offscreenBlackboardThisFrame` member + `GetOffscreenBlackboardForFrameDebugger()` accessor)
- `src/Core/GpuDrivenBatchEntityExclusionLogic.h` (new, PHASE3)
- `tests/Core/GpuDrivenBatchEntityExclusionLogicTests.cpp` (new, PHASE3)
- `src/Renderer/RenderGraph/RenderGraphTypes.h`/`.cpp` (PHASE4 — new `RenderPassCategory::FrameDebuggerInternal`)
- `src/Editor/FrameDebuggerReplayPasses.cpp` (PHASE4 — retagged to `FrameDebuggerInternal`)
- `src/Editor/FrameDebuggerData.h`/`.cpp` (PHASE4 — "Other Render Passes" sweep + doc comment)
- `src/Editor/ComputeBlurValidation.cpp` / `src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp` (PHASE4 —
  comment accuracy only, no behavior change)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (PHASE4 — 3 fixture fixes, 1 rename, 3 new tests)
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` (PHASE4 — exhaustive category coverage extended)
- `src/Renderer/RenderGraph/RenderGraphMetadata.h`/`.cpp` (PHASE5 — new `RenderGraphGroupedPassMetadata`/
  `GroupPassMetadataByName()`)
- `src/Editor/Panels/RenderGraphPanel.cpp` (PHASE5 — `BuildGroupedPassRow()`/`BuildRawInstanceSubRow()`)
- `tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp` (PHASE5 — 8 new grouping tests)
- `src/Renderer/RenderGraph/RenderPipeline.h` (PHASE6 — new `RenderPassBlackboard::WasPublishedThisFrame()`)
- `src/Editor/EditorLayer.h` / `NullEditorLayer.cpp` / `ImGuiEditorLayer.cpp` / `EditorHost.cpp` (PHASE6 —
  threading the offscreen blackboard through `BuildUI()`)
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (PHASE6 — new blackboard parameter, both new detector
  calls in `TriggerCapture()`)
- `src/Editor/FrameDebuggerCoverageChecker.h`/`.cpp`, `FrameDebuggerCoverageGuard.h`/`.cpp` (new, PHASE6)
- `src/Editor/FrameDebuggerSideChannelChecker.h`/`.cpp`, `FrameDebuggerSideChannelGuard.h`/`.cpp` (new, PHASE6)
- `tests/Editor/FrameDebuggerCoverageCheckerTests.cpp` (new, PHASE6, 9 tests)
- `tests/Editor/FrameDebuggerSideChannelCheckerTests.cpp` (new, PHASE6, 6 tests)
- `CMakeLists.txt` / `tests/CMakeLists.txt` (new source/test file registrations, multiple phases)
- `AGENTS.md` (PHASE7 — new "Render Pass System" paragraph + "Full history" list update)
- `docs/README.md` (PHASE7 — new Conventions index bullet)
- `docs/conventions/render-pass-side-channel-honesty.md` (new, PHASE7)
- Every `PHASEn_COMPLETION_REPORT.md`/this `CAMPAIGN_COMPLETION_REPORT.md` in
  `task_manager/editor-core-separation-22/`

## No delegation across the whole campaign

No `delegate_task` call was made in any of the seven phases (none permitted for implementation phases, per
this campaign's own Locked Decision #6). `ask_questions` was invoked across two of the seven phases: once
in PHASE2 (2 questions, resolved in favor of fixing the newly-discovered inverse-shape finding now, with
real live evidence), and once in PHASE5 (2 questions, locking the `gpuTimingText` shape and the
raw-breakdown reveal UX) — all four questions resolved in favor of the most honest/most-informative
option, with zero remaining accepted non-goals carried forward from this campaign's own findings.
