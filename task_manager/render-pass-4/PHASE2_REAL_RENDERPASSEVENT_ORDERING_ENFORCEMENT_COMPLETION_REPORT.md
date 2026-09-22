# PHASE2 Completion Report: Make RenderGraphCompiler Actually Use RenderPassEvent For Ordering

_Child of `PHASE0_MASTER_STRATEGY.md`. Part of the `render-pass-4` campaign.
Branch: `feature/render-pass-impl`._

## Summary

**This phase ships the one genuine, deliberate BEHAVIOR CHANGE in the whole
`render-pass-4` campaign** (per PHASE0's Locked Design Decision 1).
`RenderGraphCompiler::Compile()` (`src/Renderer/RenderGraph/RenderGraphCompiler.cpp`)
now computes an "effective order" - every pass stable-sorted by
`(RenderPassEvent, original declaration index)` - and walks passes in THAT
order, instead of raw declaration order, for both its RAW/WAW dependency-edge
scan and its Kahn's-algorithm ready-set tie-break. `RenderPassEvent` is
therefore real, load-bearing ordering input for the first time. This does
**NOT** make the historical bug class ("a pass's real writer is declared
later in C++ than its reader, so the reader silently gets no dependency edge
and the writer gets silently culled") impossible outright - it converts the
common case into a structural non-issue (since ordering by
`RenderPassEvent` no longer depends on which C++ file happened to call
`AddRenderPass()`/register a provider first), while an outright *wrong*
`RenderPassEvent` tag on a real pass remains a real hazard, now caught by
PHASE1's own `DetectRenderPassEventContradictions()` detector (updated in
this phase to check against this same new effective order instead of raw
declaration order).

**One real production-code fix landed alongside the compiler change**,
identified by this phase's own required Step 3.1 audit and confirmed with
the user via `ask_questions` before touching it (see "Deviations" below):
`src/Editor/ComputeBlurValidation.cpp`'s `AddPass()` is now explicitly
tagged `RenderPassEvent::AfterTransparents` instead of the implicit default
`Opaques`.

## What Was Changed

- **`src/Renderer/RenderGraph/RenderGraphCompiler.cpp`**:
  - Added `#include <algorithm>` (needed for `std::stable_sort`; confirmed,
    per the phase file's own precheck, that neither this file nor any of its
    own transitive includes provided it already).
  - `Compile()` now computes `effectiveOrder` (a `std::vector<std::int32_t>`
    permutation of `[0, passCount)`, stable-sorted by
    `input.passes[i].renderPassEvent`) and its inverse permutation,
    `effectivePosition`, immediately after the `passCount == 0` early return.
  - PHASE1's diagnostic block (added last phase) now passes `effectiveOrder`
    into `DetectRenderPassEventContradictions()` instead of building its own
    local identity-permutation `declarationOrder` vector (that local variable
    is gone - `effectiveOrder` is reused directly). `DetectRenderPassEventContradictions()`'s
    own body is completely untouched, exactly as PHASE1's own doc comment
    anticipated.
  - The RAW/WAW edge-building loop's outer `for (std::int32_t i = 0; i <
    passCount; ++i)` now walks `effectiveOrder` (`i = effectiveOrder[pos]`)
    instead of raw declaration order - the loop body (every `addEdge(...)`
    call, every `lastTextureWriter[...] = i` assignment) is byte-for-byte
    unchanged; only which order `i` takes each of its values changes.
  - Kahn's-algorithm's `ready` set (previously `std::set<std::int32_t> ready`
    storing/popping raw pass indices) is now `readyByEffectivePosition`,
    storing/popping **effective positions** instead, mapping back to the real
    pass index (`effectiveOrder[nodePosition]`) only when actually
    popped/pushed into `order`.
  - Every stale "declaration order"/"declared so far"/"declaration index"
    comment this rewire invalidated was updated in place (the function's own
    top-of-file cycle-detection note, the RAW-edge comment, the WAW-edge
    comment, and the Kahn's-algorithm section's own header comment) - re-grepped
    for the literal words "declaration"/"declared" after the code changes
    landed, per the phase file's own explicit instruction, to make sure both
    stale sentences in the cycle-detection paragraph (not just one) were
    fixed.
- **`src/Renderer/RenderGraph/RenderGraphCompiler.h`**: `Compile()`'s own doc
  comment and `DetectRenderPassEventContradictions()`'s own doc comment both
  updated to describe PHASE2's new effective-order behavior (no signature
  changes - `DetectRenderPassEventContradictions()`'s signature was already
  correctly shaped for this by PHASE1, exactly as that phase's own completion
  report predicted).
- **Doc comments updated for the SECOND time this campaign** (first time was
  PHASE1's addition; this phase's own update corrects the now-inaccurate
  "purely descriptive... NOT a dependency mechanism" claim, per Step 3.5):
  - `RenderGraphTypes.h`: `RenderPassEvent`'s own enum comment, and
    `PassRecord::renderPassEvent`'s own field comment.
  - `RenderPipeline.h`: the file's own header comment, and
    `RenderPassDesc::order`'s own field comment.
  - `RenderGraphBuilder.h`: both `AddRenderPass()` overloads' doc comments.
- **`src/Editor/ComputeBlurValidation.cpp`**: `AddPass()`'s `AddRenderPass()`
  call now explicitly passes `rg::RenderPassDrawKind::DrawMesh,
  rg::RenderPassEvent::AfterTransparents` as its trailing two arguments
  (previously omitted, silently defaulting to `RenderPassDrawKind::DrawMesh`/
  `RenderPassEvent::Opaques`) - see "Deviations" below for the full story.
- **`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`**: added a new
  `render-pass-4 campaign, PHASE2` test section (4 new tests, all calling the
  REAL `Compile()` end-to-end):
  - `ReaderTaggedEarlierThanItsTextuallyLaterWriterStillResolvesCorrectly` -
    **the permanent regression test for the real fix** (mirrors PHASE1's own
    `OrphanReadWithLaterWriterIsDetected`, but exercises the real compiler
    instead of just the detector): reproduces the exact historical
    `AtmosphereComposite`/`RenderOpaque` bug shape (`"Composite"`,
    `AfterTransparents`, declared first and reading a texture;
    `"Opaque"`, `Opaques`, declared second and writing it) - confirmed, by
    re-running this exact test against the pre-PHASE2 code (reverting
    `RenderGraphCompiler.cpp`'s changes locally, keeping the test), that it
    fails before the fix (`"Opaque"` is silently culled, leaving only 1
    surviving pass) and passes after it (both survive, in the correct
    `Opaque` -> `Composite` order) - satisfying `AGENTS.md`'s regression-test
    requirement.
  - `TwoIndependentPassesWithNoSharedResourceExecuteInRenderPassEventOrderRegardlessOfDeclarationOrder` -
    two passes with NO shared resource at all; the one tagged `PreOpaques`
    executes before the one tagged `AfterOpaques`, despite being declared
    SECOND - proving `RenderPassEvent` now decides order between passes with
    zero real dependency (Kahn's ready-set tie-break), this phase's own
    stated goal.
  - `WriteAfterWriteAcrossDifferentRenderPassEventTiersResolvesByEffectiveOrderNotDeclarationOrder` -
    a second, WAW-shaped regression test (the RAW test above only exercises
    the read-side "orphan" fix): two passes writing the SAME texture, with
    their declaration order and `RenderPassEvent` tags deliberately inverted
    relative to each other - confirms the WAW edge direction genuinely
    REVERSES under the new effective order (`"Late"`/`AfterTransparents`,
    declared first, now executes SECOND; `"Early"`/`Opaques`, declared
    second, now executes FIRST).
  - `PassesSharingTheSameRenderPassEventTierPreserveTheirOriginalDeclarationOrder` -
    confirms the stable-sort tie-break: two passes sharing the same
    `RenderPassEvent` tier with a real WAW dependency still execute in their
    original declaration order, byte-identical to before this phase (mirrors
    `MultipleWritersToSameResourcePreserveWriteAfterWriteOrder`'s own graph
    shape, using the `AddRenderPass()` overload instead of plain `AddPass()`).
  - Every pre-existing test in this file (all of `RenderGraphCompilerTests.cpp`'s
    prior tests, none of which ever set `.renderPassEvent` explicitly except
    PHASE1's own PHASE1-authored tests) was re-run **unmodified** and all
    still pass - confirming the all-`Opaques`-default case reduces to a
    stable-sort no-op, exactly as expected.

No changes to `tests/CMakeLists.txt` - everything lives in the
already-registered `RenderGraphCompilerTests.cpp`, per the phase file's own
instruction.

## Step 3.1 Audit (declaration order vs. RenderPassEvent, every real pass)

Performed directly against `Application.cpp`'s
`RegisterOffscreenRenderPipelineProviders()`/`Run()`, `RenderPipeline.h`'s
`DeclareOnePhase()`, and every real pass's own trailing `RenderPassEvent`
argument (`AtmosphereLutRenderer.cpp`, `ComputeBlurValidation.cpp`,
`RenderPasses.cpp`), re-confirmed this session (not copied verbatim from
PHASE0's own draft audit, which had already flagged one correction of its
own - the `AtmosphereComposite`/`AddAerialPerspectiveCompositePass()`
attribution note).

**Raw declaration order into `m_offscreenRenderPipeline`'s underlying
`CompiledGraphInput::passes` (BEFORE this phase's fix), by position:**

| # | Pass | RenderPassEvent | How it gets there |
|---|---|---|---|
| 1 | `AtmosphereSharedLut`: Transmittance | `PreOpaques` | immediate provider, `BeforeDeferredPasses`, registered 1st |
| 2 | `AtmosphereSharedLut`: MultiScattering | `PreOpaques` | same immediate call |
| 3 | `AtmosphereViewLut` (Game): SkyViewLut | `PreOpaques` | immediate provider, registered 3rd, runs per-view |
| 4 | `AtmosphereViewLut` (Game): AerialPerspectiveVolume | `PreOpaques` | same |
| 5 | `AtmosphereAerialPerspectiveVolumeDebugSlice` (Game only) | `PreOpaques` | same immediate call site, Game-View-only |
| 6 | `AtmosphereViewLut` (Scene): SkyViewLut | `PreOpaques` | same provider, second view |
| 7 | `AtmosphereViewLut` (Scene): AerialPerspectiveVolume | `PreOpaques` | same |
| 8 | `GpuSkinning` (N dispatches) | `PreOpaques` | deferred provider (registered 2nd), flushed after ALL Phase-1 providers ran |
| 9 | `RenderOpaque` (Game) | `Opaques` | deferred provider, flushed in the same sorted batch as #8/#10-13 |
| 10 | `RenderOpaque` (Scene) | `Opaques` | same |
| 11 | `DrawSkyBackground` (Game) | `AfterOpaques` | same |
| 12 | `DrawSkyBackground` (Scene) | `AfterOpaques` | same |
| 13 | `RenderTransparent` (Scene, ground-grid overlay) | `Transparents` | same (Game View has no overlay - not declared) |
| 14 | `AtmosphereComposite` (Game) | `AfterTransparents` | immediate provider, `ProviderTiming::AfterDeferredPasses` - strictly SECOND `DeclareOnePhase()` call |
| 15 | `AtmosphereComposite` (Scene) | `AfterTransparents` | same |
| 16 | `FrameDebuggerReplayStepN` (0..objectCount, Game View only, only on a serviced replay request) | `Opaques` (default - never set explicitly) | legacy direct `builder.AddRenderPass()` call, AFTER `DeclareInto()` returns entirely (`Application::Run()`) |
| 17 | `ComputeBlurValidation` (Scene View only, only when the debug toggle + Scene are both visible) | `Opaques` **(default, BEFORE this phase's fix)** | legacy direct `builder.AddRenderPass()` call, also after `DeclareInto()` returns, AFTER even #16 |

(`"Present"` lives on the completely separate `m_presentRenderPipeline`/
`CompiledGraphInput` and is unaffected by anything above.)

**Confirmed: for every pass #1-15 (the entire `RenderPipeline`-declared
production chain), a stable sort by `(RenderPassEvent, original index)`
reproduces this EXACT table, unchanged** - every `RenderPassEvent` tier
above already happens to be contiguous in raw declaration order (PreOpaques
1-8, Opaques 9-10, AfterOpaques 11-12, Transparents 13, AfterTransparents
14-15), so this phase's reorder is a byte-identical no-op for the entire
"official" pass chain. This directly confirms the specific side-benefit
PHASE2's own Step 3.1 called out as worth stating plainly: **`"AtmosphereComposite"`'s
own `RenderPassEvent::AfterTransparents` tag ALONE - independent of its
current `ProviderTiming::AfterDeferredPasses` registration - now ALSO
resolves correctly.** `AfterTransparents` sorts after every other tier
regardless of raw declaration/registration position, so a future author
could no longer reintroduce the exact historical `AtmosphereComposite` bug
shape even if they accidentally left that pass on the default
`BeforeDeferredPasses` timing again. This does **NOT** mean the
`ProviderTiming` workaround should be removed - Locked Design Decision 4
forbids changing `RenderPipeline.h`'s behavior in this campaign, and it
remains real, correct, load-bearing protection for the (still real) gap
described in that enum's own doc comment - this is purely a confidence-
building confirmation, recorded here as this phase's own doc asked for.

**A genuine mismatch WAS found for #16/#17 (the two off-pipeline, legacy-style
declarations `PHASE0`/`PHASE2`'s own Step 2 anticipated auditing)**:

- **`FrameDebuggerReplayStepN` (#16): confirmed SAFE to leave at the default
  `Opaques`.** Under the new effective order, these passes (tagged `Opaques`,
  same tier as `RenderOpaque`) get walked much EARLIER than their raw
  declaration position (right after `RenderOpaque`, instead of dead last) -
  a real reorder, but a harmless one: they only ever read
  `gpuSkinningOutputBuffers` (already written by `GpuSkinning`, `PreOpaques`
  tier, always walked first regardless) and write their OWN freshly-imported,
  per-step destination textures (`FrameDebuggerReplayStepN`) - never
  `gameTarget`/`viewTarget` itself, and never anything `DrawSkyBackground`/
  `RenderTransparent`/`AtmosphereComposite` touch. No RAW/WAW edge connects
  them to anything later in the real chain, so moving earlier in the
  topological order changes nothing about correctness.
- **`ComputeBlurValidation` (#17): a REAL, CONFIRMED regression, fixed in
  this phase (see "Deviations" below) rather than left for later.** It reads
  `sceneViewHandle` - the SAME texture handle `RenderOpaque` (Scene)/
  `DrawSkyBackground` (Scene)/`RenderTransparent` (Scene) all write, and must
  resolve to the LATEST of those three writes (the fully-composed,
  pre-atmosphere-composite Scene color). Left at the default `Opaques`, the
  new effective order would have walked it INSIDE the `Opaques` tier
  (alongside `RenderOpaque`, long before `DrawSkyBackground`/
  `RenderTransparent` - both later tiers - are ever walked), so its RAW edge
  would have silently bound to `RenderOpaque`'s write instead of the correct,
  later one - the exact "orphan read" bug shape this whole campaign exists
  to fix, this time freshly INTRODUCED by PHASE2's own reorder rather than
  fixed by it. See "Deviations" below for the full finding and the fix that
  landed.

## Deviations From The Plan

**One deviation, explicitly resolved via `ask_questions` per this phase's
own instructions (Step 3.1: "if you find a real counter-example, flag it via
`ask_questions` before proceeding").**

While performing the required Step 3.1 audit, a genuine, confirmed
correctness regression was found (see the audit table above): `ComputeBlurValidation`'s
pass, left at the default `RenderPassEvent::Opaques`, would have its real
dependency on `RenderTransparent` (Scene) silently broken by this phase's own
reorder - and, notably, PHASE1's own `DetectRenderPassEventContradictions()`
detector would **NOT** have caught this specific case even after being
updated to check the new effective order, since it only compares a resolved
read against its *actual* nearest writer's tier (`RenderOpaque`, also
`Opaques`) - which agree, once the read has already silently mis-resolved -
not against every possible writer of that same resource. This was surfaced
to the user directly (see this phase's own transcript) with three concrete
options; **the user chose to retag `ComputeBlurValidation`'s pass to
`RenderPassEvent::AfterTransparents`** (the "safest/simplest, unambiguous
regardless of tie-break" option) rather than the same-tier `Transparents`
alternative or leaving the regression in place for a later phase to
discover. This is documented, per PHASE2's own "What We Will NOT Do" clause
("do NOT retag any real pass's `RenderPassEvent` value unless the Step 3.1
audit finds a genuine, confirmed mismatch AND `ask_questions` confirms the
user wants it fixed here"), and the fix itself carries an extensive inline
comment at its own call site explaining the finding for any future reader.

No other deviation from the phase file's own Step 3 plan. The `effectiveOrder`/
`effectivePosition` computation, the edge-loop/Kahn's-algorithm rewire, and
every doc-comment update match the phase file's own sample code and
instructions essentially verbatim.

## Verification Performed

- Incremental build: `cmake --build build --target gte_core` - succeeds
  (45/45 objects, no warnings/errors from any touched file).
- Incremental build: `cmake --build build --target GreatTamanaEngineTests` -
  succeeds (15/15 objects/link).
- Ran `tests\GreatTamanaEngineTests.exe --gtest_filter=RenderGraph*:RenderPipeline*:RenderPass*`
  - **228 tests, 228 passed, 0 failed** - includes all 25
    `RenderGraphCompilerTest` cases (the 15 original + PHASE1's own 6 +
    this phase's own 4 new ones) and every other RenderGraph-module suite
    (`RenderGraphBuilderTest`, `RenderGraphSnapshotTest`, `RenderPipelineTest`,
    `RenderPassTest`, `RenderGraphBarrierPlannerTest`, etc. - none of which
    changed behavior). No full `ctest` regression run performed, per this
    phase's own scope (PHASE3 only).
  - The permanent regression test
    (`ReaderTaggedEarlierThanItsTextuallyLaterWriterStillResolvesCorrectly`)
    was additionally confirmed against the pre-fix code: temporarily disabled
    just the `std::stable_sort()` call inside `Compile()` (so `effectiveOrder`
    reduces back to the identity/raw-declaration-order permutation, i.e.
    PHASE1-only behavior), rebuilt, and re-ran this exact test in isolation.
    Result: since PHASE1's own `DetectRenderPassEventContradictions()` was
    already live and wired into `Compile()` from the prior phase, the
    pre-fix run correctly detected and reported the exact contradiction this
    test's graph shape encodes (`stderr`: `"a read resolved to NO prior
    writer, but a LATER-declared pass writes the same resource..."`) and then
    hit the `assert()` that follows it, aborting the test process - the
    Debug-build manifestation of the SAME underlying defect an NDEBUG/Release
    build would instead have silently manifested as (`"Opaque"` culled,
    `executionOrder.size() == 1`, failing this test's own `ASSERT_EQ(...,
    2u)` via a normal `EXPECT`/`ASSERT` mismatch instead of an abort). Either
    way, this is a genuine, confirmed "fails before the fix" result, exactly
    as `AGENTS.md`'s regression-test rule requires - the `std::stable_sort()`
    call was then restored immediately, and the full 228-test suite above was
    re-run afterward to confirm the real fix is back in place and nothing
    else regressed.
- Built the full `GreatTamanaEngine` target (`cmake --build build --target
  GreatTamanaEngine`) and ran it via `run_app_background`, then hit `GET
  /get_swapchain` and `GET /get_game_view` live: the Editor's Scene/Game
  panels both render the atmosphere sky gradient correctly (no solid white/
  black regression - the exact historical failure mode this whole campaign
  traces back to), the process stayed up and kept responding to HTTP the
  entire time (confirming no `assert()` fired against the real, live
  production pass graph), and the app was cleanly stopped afterward
  (`stop_app_background`). This is a targeted sanity check only, not the
  full `ctest`/live-verification pass PHASE3 itself owns.

## Notes For The Next Phase (PHASE3)

- PHASE2's own reorder is a byte-identical no-op for the entire official
  `RenderPipeline`-declared production chain (see the audit table above) -
  the only two passes whose real WALK POSITION changed at all this phase are
  the legacy, off-pipeline `FrameDebuggerReplayStepN` passes (harmless - no
  real dependency on anything later) and `ComputeBlurValidation` (fixed via
  an explicit retag, confirmed live above).
- `AGENTS.md`'s "Render Pass System" section still needs its own new
  paragraph describing this campaign's existence and its one real behavior
  change (Step 3.5 of this phase's own file asked for this, but also said
  "if PHASE3 also plans to touch `AGENTS.md`, coordinate... just make sure it
  happens exactly once, correctly, by the end of the campaign"). **This
  phase did NOT touch `AGENTS.md`** - deferred entirely to PHASE3, which
  already owns the campaign completion report / `AGENTS.md` update per
  `PHASE0_MASTER_STRATEGY.md`'s own Phase Index table ("Campaign completion
  report, `AGENTS.md` update, explicit write-up of the one genuine behavior
  change (PHASE2) for future readers"). PHASE3 should add ONE new paragraph
  there (mirroring the existing `render-pass-3` paragraph's own "LOUD,
  DELIBERATE" callout style for its own Locked Design Decision) covering:
  the safety-net detector (PHASE1), the real ordering fix (PHASE2, this
  file), and the one real production-code fix this phase made
  (`ComputeBlurValidation`'s retag).
- If PHASE3's own full `ctest` regression run or live HTTP verification
  surfaces an `assert()` firing against some OTHER real production pass this
  session's own audit did not cover (e.g. a pass registered somewhere this
  report didn't check), that is exactly PHASE1's detector doing its job for
  the first time against the fully-reordered effective order - not
  necessarily a regression in this phase's own work - but it should still be
  investigated and fixed (or flagged via `ask_questions`) before PHASE3 is
  considered done, per this campaign's own Locked Design Decision 5.

## Definition of Done - Checklist

- [x] `Compile()` builds and walks `effectiveOrder`/`effectivePosition` as
      described, and both the RAW/WAW edge loop and Kahn's-algorithm
      ready-set use it instead of raw declaration order.
- [x] PHASE1's diagnostic call site now passes `effectiveOrder`, not a
      locally-built identity permutation.
- [x] Every new test in Step 3.6 passes; every pre-existing test across
      `RenderGraphCompilerTests.cpp`, `RenderGraphSnapshotTests.cpp`,
      `RenderPipelineTests.cpp` still passes UNCHANGED.
- [x] The Step 3.1 audit is written up in this report, including the
      declaration-order/`RenderPassEvent` table for every real pass in the
      engine today, and an explicit statement of whether any pass's
      effective position moved relative to today's shipping behavior (and
      what was done about it - see "Deviations" above).
- [x] Doc comments from Step 3.5 updated. `AGENTS.md` deliberately left to
      PHASE3 (see "Notes For The Next Phase" above - this is itself an
      explicit coordination note, not a silently-skipped item).
- [x] Incremental compile of `gte_core` and `GreatTamanaEngineTests`
      succeeds. A quick, targeted live smoke check (`run_app_background` +
      `GET /get_swapchain`/`GET /get_game_view`) confirms the real Game
      View/Scene View still render correctly.
