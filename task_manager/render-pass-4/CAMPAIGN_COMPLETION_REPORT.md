# CAMPAIGN_COMPLETION_REPORT — RenderPassEvent Ordering Truth Campaign (`render-pass-4`)

_Final record of the whole `render-pass-4` campaign, branch
`feature/render-pass-impl`. Mirrors the shape of
`task_manager/render-pass-3/CAMPAIGN_COMPLETION_REPORT.md`. Written at the
close of PHASE3
(`PHASE3_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`), the only
phase in this campaign permitted a full build + full `ctest` regression run
+ live HTTP-driven verification against the real running engine._

## Why this campaign existed

`RenderPassEvent` (`BeforeEverything`/`PreOpaques`/`Opaques`/`AfterOpaques`/
`Transparents`/`AfterTransparents`/`AfterEverything`, `RenderGraphTypes.h`)
LOOKED like the mechanism that decides render-pass order. It was not — real
ordering was 100% a function of which pass got DECLARED first, in C++ call
order, into `RenderGraphBuilder`'s internal pass list.
`RenderGraphCompiler::Compile()`'s own RAW/WAW dependency-edge construction
never read `PassRecord::renderPassEvent` at all — confirmed by that field's
own doc comment, three separate times, in `RenderGraphTypes.h`. This already
caused one real, live, confirmed production bug: `Application.cpp`'s
`"AtmosphereComposite"` provider carried an existing, explicit comment
documenting that it had to be registered with
`ProviderTiming::AfterDeferredPasses` — a targeted, narrow patch from
`render-pass-3`, not a structural fix — because the compiler's resource-
versioning scan would otherwise silently cull `"RenderOpaque"`/
`"DrawSkyBackground"` if `"AtmosphereComposite"` landed in the wrong C++
registration slot, regardless of its own, always-correct
`RenderPassEvent::AfterTransparents` tag. See
`PHASE0_MASTER_STRATEGY.md`'s own "Step 1"/"Step 2" for the full original
diagnosis, confirmed by direct source inspection before any phase began.

## The three phases, in one line each

| # | Phase | One-line outcome |
|---|-------|-------------------|
| 1 | Dependency/Event Contradiction Safety Net | Added a pure, Tier-1-tested `DetectRenderPassEventContradictions()` function to `RenderGraphCompiler`, wired into `Compile()` with an unconditional `stderr` report + debug-build `assert()` on a non-empty result. Zero scheduling-behavior change — confirmed by the entire pre-existing `RenderGraphCompilerTests.cpp`/`RenderGraphSnapshotTests.cpp`/`RenderPipelineTests.cpp` suites (224 tests) passing unchanged. 6 new tests added, including the permanent regression test for the exact historical `AtmosphereComposite`/`RenderOpaque` bug pattern. |
| 2 | Real RenderPassEvent Ordering Enforcement | **The one genuine, deliberate behavior change of this campaign.** `RenderGraphCompiler::Compile()` now computes an "effective order" — every pass stable-sorted by `(RenderPassEvent, original declaration index)` — and walks passes in THAT order (instead of raw declaration order) for both its RAW/WAW dependency-edge scan and its Kahn's-algorithm ready-set tie-break. A full audit of every real production pass confirmed this reorder is a byte-identical no-op for the entire officially-declared `RenderPipeline` chain. One real, confirmed regression WAS found and fixed, via `ask_questions`: `ComputeBlurValidation.cpp`'s pass, left at the default `RenderPassEvent::Opaques`, would have had its real dependency on `RenderTransparent` (Scene View) silently broken by the reorder — retagged to `RenderPassEvent::AfterTransparents`. 228 targeted tests passing (4 new). |
| 3 | Final Integration: Full Build, Full Regression, Live Verification | This phase. Full build, full `ctest` regression suite (1626 tests, 100% passing, one pre-existing environment-gated skip), and a live, HTTP-driven Frame Debugger + Scene View verification against the real running engine — see below. |

## Final, shipped mechanism

```
RenderGraphBuilder::AddRenderPass()  <- UNCHANGED, byte-for-byte, since render-pass-2
        |  every real pass stamps PassRecord::renderPassEvent (unchanged mechanism)
        v
RenderGraphCompiler::Compile()
        |  NEW (PHASE2): effectiveOrder = stable_sort(passes, by (renderPassEvent, declIndex))
        |  RAW/WAW edge scan now walks effectiveOrder, not raw declaration order
        |  Kahn's-algorithm ready-set tie-break now uses effectivePosition
        |  NEW (PHASE1): DetectRenderPassEventContradictions(input, effectiveOrder)
        |    -> non-empty result: unconditional stderr report + debug assert()
        v
executionOrder / isCulled / textureLifetimes  <- algorithm SHAPE unchanged; only
                                                   the ORDER passes are walked in,
                                                   and whether a contradiction is
                                                   reported, changed
```

`RenderGraphBarrierPlanner.cpp`, `RenderGraph::Execute()`, and every other
Render Graph orchestrator file are completely untouched by this campaign —
only `RenderGraphCompiler.h`/`.cpp` (plus doc comments in `RenderGraphTypes.h`,
`RenderPipeline.h`, `RenderGraphBuilder.h`) and one real pass retag in
`src/Editor/ComputeBlurValidation.cpp` were changed.

## Campaign-wide verification performed (culminating in PHASE3)

### Full build

`cmake --build build`: succeeded, zero errors, zero new warnings.

### Full regression test

`ctest -C Debug --output-on-failure`: **1626 tests run, 100% passing** (1625
passed outright, 1 — `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
— correctly `GTEST_SKIP()`s, the same pre-existing, environment-gated skip
every prior campaign on this branch has documented). This is 10 tests higher
than `render-pass-3`'s own 1616 baseline (PHASE1's 6 new tests + PHASE2's 4
new tests). Zero regressions found anywhere in the suite as a result of this
campaign's three phases of changes.

### Live launch + verification

1. `GET /get_game_view` and `GET /get_swapchain`: both returned a normal,
   fully-composited frame (blue-to-warm sky gradient over dark ground
   geometry) — NOT a solid white/black frame, the exact historical failure
   symptom this whole campaign traces back to.
2. `GET /frame_debugger/open` → `200`; `GET /frame_debugger/enable?value=true`
   → `200`; `GET /frame_debugger/capture` → `hasCapturedFrame: true`,
   `totalEventCount: 15` — identical baseline to every prior campaign.
3. `GET /get_swapchain` with the Frame Debugger open confirmed the captured
   event tree's visual execution order exactly matches expectations:
   `Compute LUT` (5 Atmosphere sub-passes) → `RenderOpaque` →
   `DrawSkyBackground` → `Draw Quad` → `Compute Dispatches (Post-GameView)` →
   `AtmosphereAerialPerspectiveCompositePass` → `Compute Dispatch` — concrete,
   visual, live confirmation that PHASE2's own reorder had zero unintended
   effect on the real, shipping pass sequence.
4. `GET /activate_tab?name=Scene` + `GET
   /get_texture?texture_name=SceneViewComposited`: confirmed Scene View's own
   Opaque/Sky/Transparent chain renders the same sky gradient correctly,
   end-to-end.
5. No `assert()` fired and no PHASE1 contradiction `stderr` report was
   observed at any point during this live session — confirming the real,
   shipping production pass graph remains fully self-consistent under the new
   effective order.
6. Engine stopped cleanly (`stop_app_background`).

Every check in PHASE3's own Definition of Done passed. No `bug_report` was
filed during this campaign — no tool malfunctioned at any point.

## What shipped (cumulative)

- `src/Renderer/RenderGraph/RenderGraphCompiler.h`/`.cpp` —
  `RenderPassEventContradictionKind`, `RenderPassEventContradiction`,
  `DetectRenderPassEventContradictions()` (PHASE1, pure/Tier-1-tested); a new
  `effectiveOrder`/`effectivePosition` computation inside `Compile()` that the
  RAW/WAW edge-building loop and Kahn's-algorithm ready-set both now walk
  instead of raw declaration order (PHASE2, the campaign's one real behavior
  change).
- `src/Editor/ComputeBlurValidation.cpp` — its `AddPass()` call now explicitly
  tags `RenderPassEvent::AfterTransparents` instead of silently defaulting to
  `Opaques`, fixing a real regression PHASE2's own reorder would otherwise
  have introduced (found by the required audit, fixed with explicit user
  sign-off via `ask_questions`).
- Doc comments strengthened TWICE this campaign (once per phase) in
  `RenderGraphTypes.h` (`RenderPassEvent`'s enum comment,
  `PassRecord::renderPassEvent`'s field comment), `RenderPipeline.h` (file
  header comment, `RenderPassDesc::order`'s field comment), and
  `RenderGraphBuilder.h` (both `AddRenderPass()` overloads) — `RenderPassEvent`
  itself was NEVER renamed, per this campaign's own Locked Design Decision 3.
- `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp` — 10 new tests
  total (6 from PHASE1, 4 from PHASE2), including the two permanent
  regression tests required by Locked Design Decision 5: one reproducing the
  historical `AtmosphereComposite`/`RenderOpaque` bug pattern against the
  detector directly (PHASE1), and one reproducing the same pattern against
  the real `Compile()` end-to-end, confirmed to fail before PHASE2's fix and
  pass after it (PHASE2).
- Updated documentation: `AGENTS.md`'s "Render Pass System" section gained a
  new paragraph describing this campaign and its one real behavior change,
  in the same "LOUD, DELIBERATE" callout style already used for
  `render-pass-3`'s own Locked Design Decisions.

## What remains true and false about `RenderPassEvent` now (for a future reader who only has time for this one file)

- **TRUE, as of this campaign**: `RenderPassEvent` is now a REAL, load-bearing
  ordering key. `RenderGraphCompiler::Compile()` stable-sorts every pass by
  `(RenderPassEvent, original declaration index)` before running its RAW/WAW
  dependency scan and Kahn's-algorithm tie-break — a pass tagged with an
  earlier tier than another pass it shares no real dependency with can no
  longer land on the wrong side purely by accident of which C++ file happened
  to call `AddRenderPass()`/register its provider first.
- **TRUE, as of this campaign**: a genuinely WRONG `RenderPassEvent` tag on a
  real pass (one that disagrees with that pass's own actual resource
  reads/writes) is now loudly, immediately caught —
  `DetectRenderPassEventContradictions()` reports it via `stderr`
  unconditionally (every build config) and `assert()`s in debug builds — the
  moment it would actually break something.
- **STILL FALSE**: `RenderPassEvent` is still NOT a dependency-declaration
  mechanism. It carries no information about which specific texture/buffer a
  pass reads or writes; it is purely an ordering TIER. Two passes in the same
  tier with a real RAW/WAW dependency are still ordered correctly only
  because the compiler's resource-tracking scan (unchanged this campaign)
  independently enforces it — `RenderPassEvent` itself never expresses "pass A
  depends on pass B's output."
- **STILL FALSE**: `RenderPassEvent` does not retroactively fix every possible
  mistagging. If a future pass author gives a real pass a tag that
  contradicts its true dependency AND that contradiction happens to still
  resolve to the SAME writer the detector already expected (an edge case
  PHASE2's own "Deviations" section documents for `ComputeBlurValidation`'s
  own near-miss), the detector will not flag it. Getting `RenderPassEvent`
  right at declaration time remains the author's own responsibility; this
  campaign makes getting it WRONG far louder and far less likely to matter in
  the common case, not literally impossible in every case.

## Explicit deviations from `PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decisions

None. All six Locked Design Decisions from `PHASE0_MASTER_STRATEGY.md` were
followed exactly as written across all three phases:

1. Both the safety-net detector (PHASE1) and the real ordering fix (PHASE2)
   shipped as two genuinely separate phases, never combined.
2. The detector's reaction is both an unconditional `stderr` message AND a
   debug-build `assert()`, every time, exactly as specified.
3. `RenderPassEvent` was never renamed anywhere in this campaign.
4. `RenderPipeline.h`'s known "immediate provider always lands before every
   deferred provider within its own `ProviderTiming` phase" gap received NO
   behavior change — PHASE1's generic detector covers it transparently, per
   design.
5. A permanent regression test reproducing the exact historical bug pattern
   exists in `RenderGraphCompilerTests.cpp`, for both the detector (PHASE1)
   and the real compiler fix (PHASE2).
6. This campaign shipped in three real phases (plus this orchestrator),
   narrower than the 3-5-phase campaigns before it, exactly as required.

The one narrower "Deviation" worth naming explicitly (already fully
documented in PHASE2's own completion report, restated here for completeness
per this campaign's own PHASE0 cross-cutting rules): PHASE2's required Step
3.1 audit found a genuine, confirmed correctness regression that its own
reorder would otherwise have introduced (`ComputeBlurValidation`'s pass,
Deviation resolved via `ask_questions`, retagged to `AfterTransparents`) — a
real, on-scope finding surfaced and fixed within PHASE2 itself, not a
deviation from PHASE0's own Locked Design Decisions.

## What was explicitly NOT done (out of scope, by design)

- **No `RenderGraph.cpp`/`RenderGraphBarrierPlanner.cpp` change** — this
  campaign is entirely confined to `RenderGraphCompiler.h`/`.cpp` (plus doc
  comments and one pass retag); the topological-sort-plus-reachability shape
  and the barrier/lifetime logic Phase 3 of the original Render Graph
  campaign built are completely unchanged.
- **No rename of `RenderPassEvent`** — permanently out of scope, per Locked
  Design Decision 3.
- **No new behavior in `RenderPipeline.h`'s `ProviderTiming` mechanism** —
  the known immediate-vs-deferred gap is left exactly as `render-pass-3` built
  it; PHASE1's detector covers it generically instead, per Locked Design
  Decision 4.
- **No migration of `AddFrameDebuggerReplayPasses()`'s replay steps onto the
  new effective order in any special way** — they were audited (PHASE2's
  Step 3.1) and confirmed safe to leave at the default `Opaques` tag; no code
  change was needed or made there.
- **Merging `feature/render-pass-impl` into any other branch** — outside this
  campaign's own authority entirely, per every phase's own "What We Will NOT
  Do".

## Recommendation for whoever picks up the next session

1. Any future NEW real pass — whether declared through the old
   `RenderGraphBuilder::AddRenderPass()` directly or through a new
   `RenderPipeline` provider — should set its own `RenderPassEvent` value
   deliberately and correctly relative to its ACTUAL resource reads/writes,
   not just copy whatever tag a nearby pass happens to already use. Getting
   this right now has real, immediate teeth: `Compile()` genuinely uses it to
   decide processing order, and `DetectRenderPassEventContradictions()` will
   loudly flag it (via `stderr` + a debug `assert()`) the moment a
   contradiction is introduced.
2. If a future author is unsure what `RenderPassEvent` value a new pass
   should carry, the honest current answer is: pick the value that matches
   when this pass's real inputs become available and its real outputs are
   needed — `PreOpaques` for anything the Opaque pass itself will read this
   frame, `Opaques` for the main geometry pass itself, `AfterOpaques` for
   anything depending on the depth buffer Opaque just wrote (Sky Background's
   own `EQUAL` depth test being the canonical example), `Transparents` for
   anything in the transparency pass itself, `AfterTransparents` for anything
   that must run strictly after every other regular pass (Atmosphere
   Composite, `ComputeBlurValidation`).
3. No further action is required to close out this campaign itself — every
   phase's own Definition of Done is met, the full regression suite is green,
   and the live verification in this report confirms the real, running
   engine works end-to-end with `RenderPassEvent` now genuinely deciding
   execution order.

## Final state

- `cmake --build build`: succeeds, zero errors, zero new warnings.
- `ctest -C Debug --output-on-failure`: **1626/1626 tests run, 100%
  passing** (1 correctly-skipped optional smoke test aside) — higher than
  `render-pass-3`'s own 1616 baseline, as required.
- Live, HTTP-driven verification: confirmed both Game View and Scene View
  render a correct, fully-composited frame; confirmed the Frame Debugger's
  captured event tree shows the expected visual execution order (Atmosphere
  LUTs → Opaque → Sky → Post-Game-View Composite) with no unintended change
  from PHASE2's own reorder; confirmed no `assert()`/contradiction report
  fired against the real, live production pass graph.
- `git status` on `feature/render-pass-impl`: clean after this report's own
  commit.
