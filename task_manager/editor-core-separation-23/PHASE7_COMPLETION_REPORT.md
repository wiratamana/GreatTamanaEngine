# PHASE7 — COMPLETION REPORT: Full regression, documentation update, and the Entry Gate for BIG-STEP 2

## Step 3.1 — Full clean incremental build

`cmake --build build --target clean` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) removed 631 stale files,
followed by `cmake --build build` from scratch: **614/614 steps succeeded,
zero errors.** A second, immediately-following `cmake --build build` reported
`ninja: no work to do`, confirming the tree is genuinely, fully built.

**Warning review**: the build's own console output (captured in full) shows
zero compiler warning lines anywhere in the log — not just for this
campaign's own files, for the whole tree. This matches every prior campaign's
own documented experience (this repo's build enables no `-Wall`/`-Wextra` for
its own code, per `AGENTS.md`'s own "GPU-Driven Rendering" section closing
note about `-Wswitch`) — there is nothing this phase needs to fix, since
zero NEW warnings were introduced by PHASE1-6's own changes (none existed to
begin with).

## Step 3.2 — Full regression suite

`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`:

```
100% tests passed out of 2055
Total Test time (real) = 185.52 sec
```

**25 legitimate environment-gated skips**, zero failures. Full accounting
against `editor-core-separation-22`'s own documented final baseline (2036
tests, 8 skips):

- **Test count: 2036 → 2055, a clean +19** — exactly the sum of every new
  Tier-1 test this campaign's own phases added:
  - PHASE1: +1 (`ProjectRenderFeatureCallbackHeaderCompilesStandaloneTests.
    DefaultConstructedCallbackIsEmpty`)
  - PHASE2: +8 (`RenderFeatureCompositorProjectFeatureTests.cpp`, all 8 new
    cases)
  - PHASE3: +7 (`RegisterProjectRenderFeatureApiTests.cpp`, all 7 new cases)
  - PHASE4: +2 (`ProjectAssemblyRegistrationLedgerTests.cpp`'s two brand-new
    cases, `FullRoundTripThroughARealRenderFeatureRegistrationProvesTheWholeWiring`
    and `UnregisterEverythingForTearsDownBothRenderFeatureAndRenderPassProviderCleanly`
    — its other edits this phase EXTENDED pre-existing tests, not new ones)
  - PHASE5: +1 (`RenderGraphCompilerTest.
    ProjectRenderFeatureStyleAfterEverythingPassesNeverContradict`)
  - PHASE6: +0 (live-verification phase only, added zero new Tier-1 tests, per
    its own report)
  - Total: 1+8+7+2+1 = **19**, matching exactly.
- **Skip count: 8 → 25, a clean +17** — exactly the sum of every new
  headless-`Core`-requiring test this campaign's own phases added, all
  legitimately skipped on this development machine (the same, pre-existing,
  documented `VK_EXT_headless_surface`-unavailable gap every prior phase in
  this campaign already reported, cross-checked identically at every one of
  PHASE2/PHASE3/PHASE4's own targeted runs):
  - PHASE2: +8 (all 8 `RenderFeatureCompositorProjectFeatureTest` cases skip)
  - PHASE3: +7 (all 7 `RegisterProjectRenderFeatureApiTest` cases skip)
  - PHASE4: +2 (both of its brand-new `ProjectAssemblyRegistrationLedgerTest`
    cases skip, joining the 2 pre-existing skips in that same file)
  - Total: 8+7+2 = **17**, matching exactly.
- **Zero unexplained delta** — every single new test/skip this phase's own
  full run reported was independently, mechanically predicted in advance from
  PHASE1-6's own completion reports before this run started, then confirmed
  to match exactly, one-for-one, by name.
- **No test failed anywhere in the 2055-test run.** The Step 3.2 "diagnose and
  fix it yourself"/`delegate_task` contingency (PHASE0's Locked Decision #9)
  was never triggered — no genuine regression was surfaced by this, the FIRST
  full-suite run of the whole campaign.

## Step 3.3/3.4 — Documentation updates

- **`docs/conventions/project-assembly-system.md`**: the stale
  `### On-screen Game View compositing — investigated, confirmed NOT safe
  today` section (25 lines, previously at index 289-313) was rewritten
  in-place as `### On-screen Game View compositing — safe today via
  Core::RegisterProjectRenderFeature()` (now 111 lines) — describing the real
  API signatures, the plain non-ABI-versioned nature of this path, the
  bounded-slot GPU-state design and why it exists, the hot-reload teardown
  guarantee, the `RenderPassEvent::AfterEverything`-only rule, a complete
  working code example taken verbatim from
  `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`'s own final,
  permanently-kept state (PHASE6), the `GET /render_graph`
  `"is_project_feature"`/`"[Project]"` observability story, and an explicit
  "BIG-STEP 1 only, no Editor UI yet" statement. The file's own second, stale
  cross-reference (the "Non-Goals" bullet list, previously listing "on-screen
  Game View compositing" as an unresolved gap) was also fixed — split into two
  clauses: the generic handle-aliasing gap remains real and unresolved, while
  on-screen compositing itself is now explicitly called out as solved,
  cross-referencing the rewritten section by its own new heading. No other
  part of this file was touched — every surrounding section/heading/link
  outside these two spots is byte-for-byte unchanged (re-confirmed by a fresh
  read of the full file after both edits).
- **`AGENTS.md`**: a new, short subsection, `## Project Assembly On-Screen
  Render Feature Compositing`, was added immediately before the existing
  `## Render Pass System` section (previously at line 133) — terse, in this
  file's own established style, describing what the capability is, that it is
  a third additive module kind reusing `RenderFeatureCompositor`, the bounded
  GPU-state slot pool's purpose, and the "BIG-STEP 1 only" scope statement —
  cross-referencing `docs/conventions/project-assembly-system.md`'s own
  rewritten section for the full detail, rather than duplicating it, mirroring
  how every other `AGENTS.md` subsection already cross-references its own
  `docs/conventions/*.md` file.

## Step 3.5 — Final, explicit Step 9 checklist tick-through

Verbatim from `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`'s
own Step 9, each item ticked with fresh evidence:

- [x] `ProjectRenderFeatureCallback.h` exists, compiles standalone, zero
      circular dependency (PHASE1). **Evidence**: `src/Core/Plugins/
      ProjectRenderFeatureCallback.h` exists, includes only `<functional>`/
      `<vulkan/vulkan.h>`-transitive types and this campaign's own
      `RenderGraphTypes.h` forward-declared handles; `tests/Core/Plugins/
      ProjectRenderFeatureCallbackHeaderCompilesStandaloneTests.cpp` includes
      ONLY this header + `<gtest/gtest.h>` and compiles/links/passes
      (`ProjectRenderFeatureCallbackHeaderCompilesStandaloneTests.
      DefaultConstructedCallbackIsEmpty`, **Passed**, this phase's own full
      run, test #1908). `Core.h` line 99 still reads `class
      RenderFeatureCompositor;` (forward declaration only) — confirmed fresh,
      unchanged across PHASE1-7.
- [x] `RenderFeatureCompositor::Entry`'s third module-kind +
      `projectFeatureSlot` field, Tier-1 tested (PHASE1/PHASE2);
      `RenderFeatureDebugEntry::isProjectFeature` (mirroring `isV3`)
      correctly distinguishes a Project Assembly feature from a plugin
      feature in `GET /render_graph`'s JSON AND the Editor's "Render Graph"
      panel (PHASE2 + PHASE6 item 2). **Evidence**: `RenderFeatureCompositor.h`'s
      `Entry` struct carries `projectCallback`/`projectFeatureSlot`
      (PHASE1_COMPLETION_REPORT.md); `RenderFeatureDebugEntry.h`'s
      `isProjectFeature` field, `RenderGraphMetadata.cpp`'s
      `"is_project_feature"` JSON key, and `RenderGraphPanel.cpp`'s
      `"[Project]"` tag (PHASE2_COMPLETION_REPORT.md); PHASE6's own live
      `GET /render_graph` capture shows `"is_project_feature": true` for
      `ProjectAssemblyProbe.ScreenTint` and a real screenshot shows the
      `"[Project]"` tag rendered in the "Render Graph" panel next to that
      exact row (PHASE6_COMPLETION_REPORT.md, "Item 2").
- [x] Fixed-size, reusable project-feature slot free list, Tier-1 tested AND
      live rename-cycle-bounded proof (PHASE2 + PHASE6 item 7). **Evidence**:
      `kMaxConcurrentProjectRenderFeatures = 16` + `m_freeProjectFeatureSlots`
      free-list, 8 Tier-1 tests in `RenderFeatureCompositorProjectFeatureTests.cpp`
      including `TheSeventeenthRegistrationFailsAndAllPriorSixteenRemainRegistered`
      (PHASE2_COMPLETION_REPORT.md); PHASE6's live 20-cycle hot-reload rename
      test shows every single one of 21 register calls claiming the EXACT
      SAME GPU-state slot `15`, confirmed via `GET /get_logs?category=
      RenderFeatureCompositor&keyword=ScreenTint&limit=200`
      (PHASE6_COMPLETION_REPORT.md, "Item 7").
- [x] `Core::RegisterProjectRenderFeature()`/`UnregisterProjectRenderFeature()`
      exist, null-safe, reject (never truncate) an over-length name, reach
      the same live `RenderFeatureCompositor` instance (PHASE3). **Evidence**:
      `Core.h`/`Core.cpp` bodies (PHASE3_COMPLETION_REPORT.md); Tier-1 tests
      `DebugNameOfExactly63BytesSucceeds`/`DebugNameOf64BytesIsRejectedAndNeverTruncated`/
      `AMuchLongerDebugNameIsAlsoRejected`/`NullDebugNameIsRefusedWithoutCrashing`
      in `tests/Core/RegisterProjectRenderFeatureApiTests.cpp`; PHASE6's live
      `GET /get_logs?category=ProjectAssembly` shows a real registration
      succeeding through this exact API against the real, running compositor
      instance.
- [x] `ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()` tears
      down render features, releases slots, BEFORE render-pass providers,
      live-confirmed across a real hot-reload cycle, zero crash/duplicate/
      dangling callback, `GET /project_assembly/debug/ledger` matches
      (PHASE4 + PHASE6 item 4/5). **Evidence**: `ProjectAssemblyRegistrationLedger.cpp`'s
      `renderFeatureNames` teardown loop placed strictly BEFORE the
      `renderPassNames` loop (PHASE4_COMPLETION_REPORT.md, confirmed by direct
      code re-read); PHASE6's live `POST /project_assembly/hot_reload?name=
      ProjectAssemblyProbe` (`"last_outcome":"Success"`) followed by
      `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` showing
      exactly one `render_feature_names` entry post-reload (never zero, never
      duplicated) and a re-captured `GET /get_game_view` still showing the
      tint (same 13103-byte size as pre-reload) — zero crash across the whole
      20-cycle rename sequence that followed (PHASE6_COMPLETION_REPORT.md,
      "Item 4/5").
- [x] Hand-wired trivial project render feature visible in a real Editor Game
      View screenshot, not merely the debug panel (PHASE6 item 3).
      **Evidence**: `GET /get_game_view` background pixel changes from
      `(212,199,185)` (tint OFF) to `(218,169,157)` (tint ON), matching the
      documented `AlphaOver` formula to the exact integer, plus a directly
      visible warmer/pinkish Game View confirmed via `load_image`
      (PHASE6_COMPLETION_REPORT.md, "Item 3").
- [x] `DetectRenderPassEventContradictions()` reports zero contradictions for
      the new pass kind, confirmed live (PHASE5 + PHASE6). **Evidence**: new
      Tier-1 regression test
      `RenderGraphCompilerTest.ProjectRenderFeatureStyleAfterEverythingPassesNeverContradict`
      passes (34/34 in that file, PHASE5_COMPLETION_REPORT.md); PHASE6's live
      session shows `GET /get_logs?keyword=contradiction` returning 0 entries
      across the whole verification sequence, including the 20-cycle rename
      run (PHASE6_COMPLETION_REPORT.md, "Ordering safety net (PHASE5)
      confirmed live").
- [x] `gte_plugin_abi`'s existing `_v2`/`_v3` plugins confirmed, live,
      completely unaffected (PHASE6 item 8). **Evidence**: `GET /render_graph`'s
      `render_features[]`, captured after every mutation this campaign's own
      PHASE6 performed (hot reload, 20 rename cycles, a temporary second
      feature added then removed), still lists all five demo plugin features
      unchanged, still enabled (PHASE6_COMPLETION_REPORT.md, "Item 8").
- [x] Full `ctest` regression pass, 100%, zero unexplained delta (this phase,
      3.2 above). **Evidence**: `100% tests passed out of 2055`, 25
      legitimate skips, +19 tests/+17 skips versus `editor-core-separation-22`'s
      own 2036/8 baseline, every single delta individually accounted for by
      name against PHASE1-6's own reports (see 3.2 above) — zero failures,
      zero unexplained skip, zero unexplained new test.

## ENTRY GATE FOR BIG-STEP 2

**Every checkbox above is independently confirmed TRUE, and therefore
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
may now be opened by a FUTURE, SEPARATE campaign — this campaign
(`editor-core-separation-23`) itself does NOT open or act on that file in any
way.**

## Ambiguity checkpoints (Step 3.7)

- The full `ctest` run surfaced zero failures — the "diagnose an ambiguous
  root cause" checkpoint never triggered; no `ask_questions` call was needed
  for this.
- `docs/conventions/project-assembly-system.md`'s existing section structure
  did NOT make the in-place rewrite awkward — the ONE other stale
  cross-reference the phase file itself pre-identified (the "Non-Goals"
  bullet) was the only other spot in the whole file referencing the old
  section's own framing by name (confirmed via `search_in_dir` for "On-screen
  Game View compositing" across the whole file before editing — exactly 2
  matches, both addressed); no `ask_questions` call was needed for this
  either, since the phase file's own instruction already fully anticipated
  and resolved this exact case.

## What was deliberately left alone

- `plugins/gte_plugin_abi/`, every `_v2`/`_v3` demo plugin, and every file
  PHASE1-6 already completed and committed — untouched by this phase; PHASE7
  only ran the full build/test pass and edited exactly the two documentation
  files + wrote the two reports below.
- `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
  — never opened or acted on by this campaign, per PHASE0's own explicit
  scope boundary.

## End-of-phase checklist

1. ✅ Full clean incremental build succeeds (614/614 steps, zero errors, zero
   warnings).
2. ✅ Full `ctest` regression pass, 100% (2055 tests, 25 legitimate
   environment-gated skips, count only growing versus the campaign's own
   starting baseline).
3. ✅ `docs/conventions/project-assembly-system.md` and `AGENTS.md` both
   updated.
4. ✅ This report — the full, evidenced Step 9 tick-through and the explicit
   Entry Gate statement.
5. Pending: `CAMPAIGN_COMPLETION_REPORT.md` (written next, same commit).
6. Pending: `git_add` + `git_commit` covering the doc updates and both
   reports — the final commit of this whole campaign.
