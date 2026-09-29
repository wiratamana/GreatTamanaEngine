# PHASE4 COMPLETION REPORT — Fix `RenderPassCategory::Debug` misuse and make the Frame Debugger tree structurally incapable of silently dropping a surviving pass

Campaign folder: `task_manager/editor-core-separation-22/`
Branch: `feature/editor-core-separation` (unchanged, as required)
Status: **Complete.** Both halves of Root Cause #2 are fixed: the
`RenderPassCategory::Debug` misuse is corrected via a new, correctly-scoped
`RenderPassCategory::FrameDebuggerInternal` value, and `BuildRealFrameDebuggerSnapshot()`
now has a generic, structural "nothing survives silently dropped" sweep
("Other Render Passes") that closes the coverage gap for good. Confirmed
live, on a real running `GreatTamanaEditor.exe`, that `DemoRenderFeaturePlugin_Clear`/
`DemoRenderFeatureSecondPlugin_Clear` now produce real leaves and vanish
correctly when disabled. Zero `ask_questions` were needed — both potential
ambiguity points the phase file called out (the `FrameDebuggerStepPreviewKind`
choice for the sweep, and whether `GBufferValidation`/`ComputeBlurValidation`
should become visible) were resolved by engineering rather than by guessing,
and neither produced a genuine open question (see below).

## PHASE3 pre-check (required reading before starting)

Re-read `PHASE3_COMPLETION_REPORT.md` in full first, per this phase's own
instruction. PHASE3's only fix (finding #19, `GpuDrivenBatchEntityExclusionLogic`)
touched `src/Core/Core.cpp`/`Core.h` only — it never touched
`RenderPassCategory` or `FrameDebuggerData.cpp`. This phase's own starting
assumptions (Step 2 of the phase file, quoting the exact current line numbers
of `RenderGraphTypes.h`/`FrameDebuggerData.cpp`) were still accurate; no
reconciliation was needed.

## Step 3.1 — Category misuse fix

1. **`src/Renderer/RenderGraph/RenderGraphTypes.h`** — added
   `RenderPassCategory::FrameDebuggerInternal` as a new, third enumerator.
   Updated `Debug`'s own doc comment to remove the "never a real Frame
   Debugger tree citizen" clause (moved to `FrameDebuggerInternal`'s own doc
   comment) — `Debug` now means "a real, optional/debug-flavored FEATURE
   pass, fully visible in the tree like any other survivor, when it runs."
2. **`src/Renderer/RenderGraph/RenderGraphTypes.cpp`** — `ToString()`'s
   exhaustive switch gained the new `case RenderPassCategory::FrameDebuggerInternal:
   return "FrameDebuggerInternal";` (no `default:` case exists in this file —
   confirmed the build would have failed to compile without this addition).
3. **`src/Editor/FrameDebuggerReplayPasses.cpp`** — the one real production
   user of the old "invisible" meaning: `AddRenderPass(...)`'s category
   argument changed from `rg::RenderPassCategory::Debug` to
   `rg::RenderPassCategory::FrameDebuggerInternal`. Doc comment updated to
   match.
4. **`src/Editor/FrameDebuggerData.cpp`** — the "view region" walk's own
   exclusion check changed from `pass.category == rg::RenderPassCategory::Debug`
   to `pass.category == rg::RenderPassCategory::FrameDebuggerInternal`
   (~line 889 post-edit). `search_in_dir` for `RenderPassCategory::Debug`
   across `src/` confirmed EVERY other call site
   (`PluginRenderPassBuilderAdapter.cpp`, `GBufferValidation.cpp` x2,
   `ComputeBlurValidation.cpp`) keeps `RenderPassCategory::Debug` unchanged —
   exactly as the phase file specified — since all four are genuinely real,
   user-toggleable features, not Frame-Debugger-internal scaffolding.
5. Updated stale doc-comment claims that depended on the OLD meaning:
   `PluginRenderPassBuilderAdapter.cpp`'s own comment (confirmed this call
   site is still correct under the new meaning) and
   `ComputeBlurValidation.cpp`'s inline comment (removed the now-false "the
   Debug tag is a second, independent safety net" claim — its own
   invisibility comes ENTIRELY from its `ViewScope::SceneView` tag, which is
   unaffected by this phase).

## Step 3.2 — The generic "Other Render Passes" sweep

`BuildRealFrameDebuggerSnapshot()` (`FrameDebuggerData.cpp`) was reworked
exactly per the phase's own plan:

1. Added `std::vector<bool> claimed(graphSnapshot.passesInExecutionOrder.size(), false);`
   near the top.
2. Every existing loop (pre-GameView compute bucket, view-region walk,
   post-GameView compute bucket) now marks `claimed[i] = true` both when it
   turns a pass into a real leaf AND when it deliberately, honestly excludes
   one for an already-documented reason (culled, `ViewScope::SceneView`,
   `RenderPassCategory::FrameDebuggerInternal`). A pass that is simply "not
   this loop's concern" (e.g. a Graphics-kind pass inside a compute-only
   loop) is explicitly, deliberately left UNCLAIMED — that is exactly what
   lets the final sweep pick it up.
3. Added the final, generic sweep loop after the post-GameView compute
   bucket, building an `"Other Render Passes"` group (only appended if
   non-empty) containing one wrapped leaf (via the existing
   `WrapPassWithOwnedChildEvent()`/`BuildComputeDispatchLeaf()`/
   `BuildGraphicsPassLeaf()`/`GraphicsChildEventLabelFor()` machinery — no new
   leaf-building code needed) per unclaimed, non-culled, non-SceneView,
   non-`FrameDebuggerInternal` survivor, of EITHER `PassKind`, at ANY index.
4. **`FrameDebuggerStepPreviewKind` decision (the phase file's own flagged
   ambiguity point)**: rather than hardcoding the sketch's literal
   `PostComposite` for every swept-up pass, this implementation computes the
   SAME structural comparison the existing post-GameView compute loop
   already uses (`pivotIndex`/`compositePassIndex`), extended one step
   further back: `index < pivotIndex` → `NotYetDrawn` (nothing has drawn yet
   at that point); `index < compositePassIndex` → `PreComposite`; otherwise
   → `PostComposite`. This is provably more honest than a fixed constant (a
   swept-up pass CAN legitimately sit anywhere relative to both boundaries —
   see the new `GraphicsPassAfterSurvivingComputePassIsSweptIntoOtherRenderPassesGroup`
   test, which sits after the composite pass and correctly gets
   `PostComposite`) and required no new `FrameDebuggerStepPreviewKind`
   enumerator — the three existing values already cover every real case. No
   `ask_questions` was needed because this is a strictly-better refinement of
   the sketch, not a genuine fork in the design; `PostComposite`'s own doc
   comment ("any leaf at/after it, or nothing selected at all") already
   anticipated exactly this kind of generic fallback.

## Step 3.3 — `FrameDebuggerData.h` doc-comment update

The tree-shape diagram (~line 446-503 pre-edit) gained the new `"Other
Render Passes"` terminal group entry, and the function's own top-of-file doc
comment was rewritten to describe `RenderPassCategory::FrameDebuggerInternal`'s
corrected exclusivity and the sweep's own "structurally complete, by
construction" guarantee.

## Step 3.4 — Tests

All new/updated coverage lives in
`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (confirmed, per the
corrected phase file, this is the right home — `FrameDebuggerDataTests.cpp`
never calls `BuildRealFrameDebuggerSnapshot()` at all).

**Mandatory pre-existing fixture fix (done, not skipped)**: the old
`DebugCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf` test's own two
synthetic passes are now tagged `rg::RenderPassCategory::FrameDebuggerInternal`
instead of `rg::RenderPassCategory::Debug` (matching `FrameDebuggerReplayPasses.cpp`'s
own real production retag). Renamed to
`FrameDebuggerInternalCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf`
so its name matches what it now actually asserts.

**Three additional, unflagged-by-the-phase-file, pre-existing tests also
needed a fix** (discovered by careful trace, not by "run and see what
breaks blindly" — though the build/test run below did independently confirm
they would have failed otherwise): each fed the tree a plain, default-category,
default-`ViewScope::Shared`, Graphics-kind pass positioned BEFORE the
`"RenderOpaque"` pivot, and asserted it produced NO leaf — this was ALWAYS
an accidental byproduct of the exact same structural gap this phase fixes
(a Graphics-kind pass before the pivot was never inspected by the
Compute-only pre-view loop), not an intentional exclusion. Once the sweep
exists, these passes correctly, honestly surface too (Clause B) unless
their fixture is corrected to use a genuinely-documented exclusion reason
instead:
1. `RenderOpaqueWithNoComputePassesProducesExactlyOneLeaf` — its `"SceneView"`
   fixture pass is now ALSO explicitly tagged `viewScope = rg::ViewScope::SceneView`
   (it never was before — an honest bug in the fixture, since the test's own
   comment already claimed "Locked Design Decision #7 (Game-View-only scope)
   must exclude it", a claim the fixture never actually enforced). This
   keeps the test's own original assertions completely unchanged.
2. `NonComputePassIsNeverTreatedAsComputeDispatch` — rewritten to assert the
   pass now correctly surfaces under `"Other Render Passes"`, built via the
   GRAPHICS leaf path (`eventLabel == "Draw Mesh"`, never `"Compute
   Dispatch"`) — this is actually a MORE direct proof of the test's own
   stated purpose than before.
3. `ViewRegionPivotIsFoundStructurallyEvenWhenNotNamedRenderOpaque` — the red
   herring pass now correctly surfaces under `"Other Render Passes"` too;
   updated assertions confirm it is STILL never confused with the real pivot
   (its own draw stats stay all-zero, `stepPreviewKind == NotYetDrawn` since
   it sits before the pivot) — the test's actual point (structural,
   name-free pivot lookup) is unweakened.

**New tests added** (Step 3.4 items a/b/c):
- `GraphicsPassAfterSurvivingComputePassIsSweptIntoOtherRenderPassesGroup`
  (item a) — reproduces the exact `DemoRenderFeaturePlugin_Clear` shape (a
  `General`-category Graphics pass tagged `RenderPassEvent::AfterEverything`,
  positioned after a surviving compute pass) and confirms it now produces a
  real leaf under `"Other Render Passes"`, correctly `PostComposite`, never
  mistaken for a compute dispatch.
- `FrameDebuggerInternalCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf`
  (item b, renamed from the mandatory fix above) — the Frame Debugger's own
  internal replay scaffolding still produces ZERO leaves, anywhere, ever.
- `DebugCategoryGraphicsPassInsideViewRegionIsNowVisibleAsANormalLeaf`
  (item c) — a real `Debug`-category pass, positioned INSIDE the ordinary
  view-region walk's own range, is now a normal, visible, directly-selectable
  sibling leaf (discovered by the existing walk itself, not the sweep — it
  never needed to be "swept", since only `FrameDebuggerInternal` is excluded
  from that walk now).

Total new/changed tests in this file: 3 mandatory fixture fixes + 1 rename +
3 new tests = 7 test-level changes, on top of zero regressions in the
remaining 31 pre-existing tests.

`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` — the two exhaustive
`RenderPassCategory::ToString()` coverage tests
(`ToStringCoversEveryEnumeratorNonNullNonEmpty`/
`ToStringProducesDistinctNamesForDistinctEnumerators`) extended to include
`RenderPassCategory::FrameDebuggerInternal`.

### Targeted build + test results

```
cmake --build build                                          -> succeeded (128/128 targets)
ctest -C Debug --output-on-failure -R "FrameDebuggerSnapshotBuilderTest|RenderGraphRenderPassCategoryTest|RenderGraphMetadataTest"
  -> 51/51 passed
ctest -C Debug --output-on-failure -R "FrameDebugger|RenderGraph|RenderPass"
  -> 464/464 passed  (wider net cast deliberately, to catch any collateral
     regression in every Frame Debugger/RenderGraph/RenderPass-related test
     in the suite — still not the full suite, per this phase's own
     "incremental only" rule; PHASE7 owns the full run)
```

Never a full clean build, never a full `ctest` regression pass (reserved for
PHASE7), per Locked Decision #2.

## Step 3.5 — Live, HTTP-driven verification

Performed on a real running `GreatTamanaEditor.exe` via
`run_app_background`/`gte_send_request`/`stop_app_background`
(PID 16440, cleanly stopped at the end).

1. `GET /render_graph/passes` confirmed `DemoRenderFeaturePlugin_Clear`/
   `DemoRenderFeatureSecondPlugin_Clear` both `enabled:true` this session.
2. `GET /frame_debugger/open` → `/enable?value=true` → `/capture` succeeded;
   baseline `totalEventCount == 79` with both plugin passes enabled.
3. **The fix's own proof**: `GET /render_graph/set_pass_enabled?name=DemoRenderFeaturePlugin_Clear&enabled=false`
   then re-`/capture` → `totalEventCount` dropped to **75**, a clean, exactly
   explainable **-4** (this pass declares TWICE per frame — once for Game
   View, once for Scene View, both tagged `ViewScope::Shared` by
   `PluginRenderPassBuilderAdapter` — each instance now produces one real
   swept leaf, parent+child = 2 indices, so disabling the shared NAME removes
   both instances' leaves = 4 indices total). Re-enabling brought it back to
   **79** exactly. Repeated the identical experiment for
   `DemoRenderFeatureSecondPlugin_Clear` alone (with the first plugin pass
   left enabled) — same clean, symmetric **-4** / restore-to-79 result.
   `GET /get_logs?min_level=Error` stayed `{"count":0}` throughout.
4. Re-ran PHASE1's own sky-related checks: `GET /render_graph/set_pass_enabled?name=DrawSkyBackground&enabled=false`
   → `/capture` → `totalEventCount` dropped by a clean **-2** (79 → 77 — the
   sky pass's own single, GameView-only leaf gone, unaffected by this phase's
   sweep addition, confirmed rather than assumed). `GET /get_game_view`
   confirmed NO sky pixels remain (flat background color only, no blue-sky
   gradient). Re-enabled → back to 79. `GET /get_logs?category=RenderPassHonesty`
   stayed `{"count":0}` the entire session — zero honesty-checker mismatches,
   confirming this phase introduced no Clause-A regression.
5. `GET /render_graph/passes` showed no `GBufferValidation`/`ComputeBlurValidation`
   entries at all in this session (off by default, gated by their own
   Scene-panel checkbox, not the generic toggle registry) — nothing to
   observably regress; their code-level analysis (both stay
   `ViewScope::SceneView`, independently excluded by the SAME filter this
   phase leaves untouched, both in the ordinary walk AND the new sweep's own
   defensive check) already confirms zero behavior change. No live testing
   result suggested the user wants them visible, so no `ask_questions` was
   needed for this point either (the phase file's own note was explicitly
   conditional on that).

## A plain, honest side-effect note (not a bug, not a regression — flagging it per this codebase's own convention)

The live verification above also surfaced something worth stating plainly:
the `"Other Render Passes"` sweep, being genuinely name-free and structural
per this phase's own design mandate, ALSO now surfaces the entire family of
`DemoRenderFeatureV2*/V3*/V2Second*/V3Second*/V3Third*` chain passes
registered by `RenderFeatureCompositor` — EACH declared twice (once per
active view) but ALL tagged `ViewScope::Shared` (not `GameView`/`SceneView`
per instance), exactly like `DemoRenderFeaturePlugin_Clear` itself. This
means the GameView-scoped Frame Debugger tree's new `"Other Render Passes"`
group can show a Scene-View-targeting duplicate alongside a Game-View one,
with nothing in this tree distinguishing which is which by view — this is
NOT a new bug this phase introduces; it is the EXACT SAME "duplicate
per-view rows sharing one ambiguous `Shared` tag" root cause PHASE0 already
identified as Root Cause #1, explicitly assigned to PHASE5
(`PHASE5_UNIFY_DUPLICATE_PASS_ROWS_RENDER_GRAPH_PANEL.md`). Before this
phase, these passes were simply invisible everywhere (the same "silently
dropped" bug this phase fixes) so this ambiguity was never observable in the
Frame Debugger tree at all; now that they are honestly visible (Clause B),
this pre-existing view-scoping ambiguity becomes visible too. This is
flagged here plainly for whoever picks up PHASE5, mirroring PHASE1's own
"Note 2"/PHASE3's own "Note" precedent — explicitly NOT fixed by this phase
(fixing it would mean changing `PluginRenderPassBuilderAdapter`'s/
`RenderFeatureCompositor`'s own `ViewScope` tagging, a bigger, genuinely
out-of-scope change for a category-fix-and-completeness phase).

## No delegation, no `ask_questions` needed

No `delegate_task` call was made (not permitted for this phase, per PHASE0
Locked Decision #6). No `ask_questions` call was needed — both of the phase
file's own flagged potential ambiguity points were resolved by engineering
(the `FrameDebuggerStepPreviewKind` choice was strictly better-determined
structurally, never a genuine fork; `GBufferValidation`/`ComputeBlurValidation`
visibility never actually changed, so there was nothing to ask about) and no
other genuine ambiguity was discovered while implementing.

## Files changed

- `src/Renderer/RenderGraph/RenderGraphTypes.h` (new `FrameDebuggerInternal`
  enumerator + corrected `Debug` doc comment)
- `src/Renderer/RenderGraph/RenderGraphTypes.cpp` (`ToString()` switch
  extended)
- `src/Editor/FrameDebuggerReplayPasses.cpp` (retagged to
  `FrameDebuggerInternal`)
- `src/Editor/FrameDebuggerData.cpp` (`claimed` tracking added to every
  existing loop; new "Other Render Passes" final sweep; exclusion check
  retargeted to `FrameDebuggerInternal`)
- `src/Editor/FrameDebuggerData.h` (tree-shape doc comment updated)
- `src/Editor/ComputeBlurValidation.cpp` (comment accuracy fix only — no
  behavior change)
- `src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp` (comment addendum
  confirming this call site is still correct under `Debug`'s corrected
  meaning — no behavior change)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (3 mandatory fixture
  fixes, 1 rename, 3 new tests, helper doc-comment update)
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` (exhaustive
  `RenderPassCategory` coverage extended)
- `task_manager/editor-core-separation-22/PHASE4_COMPLETION_REPORT.md` (this
  file)

## End of phase checklist (Step 3.6)

1. ✅ Incremental build (`cmake --build build`) succeeded.
2. ✅ Targeted `ctest` filters — 51/51 (this phase's own new/extended tests)
   and 464/464 (wider Frame Debugger/RenderGraph/RenderPass net) — both
   passed. Never the full suite (reserved for PHASE7).
3. ✅ This completion report written — exact diff summary, live-verification
   evidence, and the honest side-effect note above.
4. Next: `git_add` + `git_commit` covering every file changed and this
   report.
