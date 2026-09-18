# PHASE1_CORE_VOCABULARY_AND_BLACKBOARD - Completion Report

_Child of `PHASE0_MASTER_STRATEGY.md`, `render-pass-3` campaign. Branch:
`feature/render-pass-impl` (unchanged, never switched)._

## Summary

Implemented PHASE1 exactly as scoped: a brand-new, purely additive
declaration layer (`src/Renderer/RenderGraph/RenderPipeline.h`/`.cpp`) with
`RenderPassId`/`RenderPassTag`/`RenderPassTagMask`/`RenderViewId`/
`RenderPassDesc`/`RenderPassProvider`/`ProviderScope`/`RenderPassBlackboard`/
`RenderPassFrameContext`/`RenderPipeline`, plus the small, additive changes
to the existing render-graph vocabulary/builder/snapshot files the phase doc
called for. **Zero real consumers** - nothing under `src/Application/` or
`src/Editor/` was touched, and nothing outside `src/Renderer/RenderGraph/`/
`tests/` references `RenderPipeline` (confirmed by `search_in_dir`).

## Files changed

- **New**: `src/Renderer/RenderGraph/RenderPipeline.h` - the whole new
  vocabulary described above.
- **New**: `src/Renderer/RenderGraph/RenderPipeline.cpp` - the debug-only
  `RegisterPassIdDebugName()`/`DebugNameForPassId()` pair (see "Deviation 1"
  below for why these live here rather than inside the `consteval` literal
  operator itself).
- **New**: `tests/Renderer/RenderGraph/RenderPipelineTests.cpp` - 24 new
  Tier-1 tests (all passing - see "Verification" below).
- `src/Renderer/RenderGraph/RenderGraphTypes.h` - added the `RenderPassEvent`
  enum + `ToString(RenderPassEvent)` declaration (placed here, not in
  `RenderPipeline.h`, per the phase doc's own instruction, since it is also
  threaded onto `PassRecord`); appended `PassRecord::renderPassEvent`
  (defaults to `Opaques`) at the very end of the struct.
- `src/Renderer/RenderGraph/RenderGraphTypes.cpp` - added
  `ToString(RenderPassEvent)`, following the same exhaustive-switch-with-
  no-`default:` convention as every sibling `ToString()` in this file.
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` - added ONE new, trailing,
  DEFAULTED `RenderPassEvent renderPassEvent = RenderPassEvent::Opaques`
  parameter to BOTH `AddRenderPass()` overloads (mirroring `drawKind`'s own
  `render-pass-2` precedent exactly), stamped onto `m_passes.back()` in the
  same place `drawKind`/`category` already are. No other change to this
  file.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h`/`.cpp` -
  `RenderGraphPassSnapshot` gained its own `renderPassEvent` field, copied
  straight through in `BuildPassSnapshot()` for both a surviving and a
  culled pass, mirroring `category`/`drawKind`/`viewScope`'s own existing
  copy-through precedent exactly.
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` - added
  `ToString(RenderPassEvent)` coverage (every enumerator non-null/non-empty
  + exact-name-per-enumerator) and a `PassRecord::renderPassEvent` default-
  value test, mirroring the file's own existing per-enum test shape.
- `CMakeLists.txt` (root) - registered the two new `RenderPipeline.h`/`.cpp`
  files in `gte_core`'s source list, right after
  `RenderGraphDebugVolumeTextureRegistry.cpp`.
- `tests/CMakeLists.txt` - registered `RenderPipelineTests.cpp` in
  `GTE_TEST_SOURCES` (plus its own descriptive comment block, mirroring
  every sibling entry's documentation style).

## Deviations from the phase doc (both intentional, both narrow)

### Deviation 1 - the debug-only `RenderPassId` name-registration mechanism could not literally live inside `operator""_passId`

The phase doc's own Step 3.1 says: "In debug builds only... every `_passId`
construction ALSO registers its hash against the original source string in a
small global table (a plain function-local `static
std::unordered_map<std::uint64_t, const char*>` is fine)."

This is not actually expressible the way it reads literally: `operator""_passId`
is required to be `consteval` (per the same doc section, "MUST be consteval
so `"Foo"_passId` never costs anything at runtime"). A `consteval` function is
evaluated **entirely** at compile time, and the C++ standard forbids a
function-local `static` variable inside a `constexpr`/`consteval` function
precisely because such a variable's state could never survive from one
compile-time evaluation to "the running program" - there is no way for a
`consteval` call to leave any runtime-observable side effect behind at all.

**Resolution**: `RegisterPassIdDebugName(RenderPassId, const char*)` and
`DebugNameForPassId(RenderPassId) -> const char*` exist exactly as specified
(debug-only, `#ifndef NDEBUG`, backed by a function-local
`static std::unordered_map`), but as a genuinely **separate**, ordinary
(non-`consteval`) pair of runtime functions - NOT automatically invoked by
`operator""_passId` itself. A future call site that wants a `RenderPassId` to
be debuggable (e.g. a later phase's `RenderPipeline::Register()`) would need
to call `RegisterPassIdDebugName()` explicitly at the point it mints that id.
This has **zero observable effect today**, since this phase's own Definition
of Done requires "zero real consumers" of any of this new vocabulary - no
code anywhere calls either function yet, so there is nothing to register in
practice. Flagging this explicitly per the phase doc's own "document this
choice plainly" convention (see `ReportUnusedPublishesIfAny()`'s own
identical instruction) - a later phase should decide whether to wire an
explicit registration call in wherever it first becomes useful (e.g.
`RenderPipeline::Register()` accepting the debug name and registering it),
rather than re-attempting to put it inside the literal operator.

### Deviation 2 - `RenderPassBlackboard::SlotCapacityForTesting()` - a small, additive testing-only accessor not in the phase doc's own pseudocode

The phase doc's own Step 3.5 explicitly requires a test that
"`BeginFrame()` clears a previously-published key... while NOT shrinking
`m_slots`'s own `.capacity()` below its prior high-water mark." `m_slots` is
(correctly) a private implementation detail of `RenderPassBlackboard`, so a
test file cannot inspect it directly. Added one small, side-effect-free
accessor, `std::size_t SlotCapacityForTesting() const noexcept`, mirroring
this codebase's own established `...ForTesting()` naming convention (see
`FrameProfiler::ResetForTesting()`/`OverrideLastFrameCpuMillisecondsForTesting()`,
`src/Profiling/FrameProfiler.h`). Always compiled (not debug-gated), since it
has no behavior of its own beyond returning `m_slots.capacity()`.

Neither deviation touches any Locked Design Decision from `PHASE0_MASTER_STRATEGY.md`
- both are narrow, additive, test-support-only adjustments inside this
phase's own brand-new file.

## Verification performed

- **Incremental compile, `gte_core`**: succeeded cleanly (46 objects
  rebuilt/relinked, including the two new `RenderPipeline.*` files and every
  file touched by the additive `RenderGraphTypes.h`/`RenderGraphBuilder.h`/
  `RenderGraphSnapshot.h` changes - `Application.cpp`, `RenderPasses.cpp`,
  `AtmosphereLutRenderer.cpp`, `AtmosphereSkyBackgroundRenderer.cpp`,
  `ComputeBlurValidation.cpp`, `FrameDebuggerData.cpp`, etc. - all of which
  call the now-8-argument `AddRenderPass()` overloads via their PRE-EXISTING,
  unmodified call sites, relying purely on the new trailing default).
- **Incremental compile, `GreatTamanaEngineTests`**: succeeded cleanly.
- **Full targeted test run**: `GreatTamanaEngineTests.exe
  --gtest_filter=RenderGraph*:RenderPass*` - **207/207 passing**, zero
  failures, zero new skips - covering every pre-existing Render Graph test
  file (`RenderGraphTypesTests`, `RenderGraphBuilderTests`, `RenderPassTests`,
  `RenderGraphCompilerTests`, `RenderGraphBarrierPlannerTests`,
  `RenderGraphNameSlotTableTests`, `RenderGraphSnapshotTests`,
  `RenderGraphDebugTextureRegistryTests`,
  `RenderGraphDebugVolumeTextureRegistryTests`) PLUS the 24 brand-new
  `RenderPipelineTests.cpp` tests and the 3 new `RenderPassEvent`-specific
  tests added to `RenderGraphTypesTests.cpp` - confirming zero regressions
  anywhere in this area.
- **`grep`/`search_in_dir` verification** (per the phase's own Definition of
  Done): `RenderPipeline` appears ONLY inside `src/Renderer/RenderGraph/`
  (its own new files' doc comments/definitions) and, separately, inside
  `tests/`. Every real `AddRenderPass(` call site across the repo
  (`Application/RenderPasses.cpp` x4, `Editor/ComputeBlurValidation.cpp`,
  `Renderer/Atmosphere/AtmosphereLutRenderer.cpp` x6) is a pre-existing call
  using the OLD argument shape, compiling unmodified against the new
  trailing default.
- No full build, no full `ctest` run, no live/HTTP smoke test was performed
  - correctly out of scope for this phase per `PHASE0_MASTER_STRATEGY.md`'s
  cross-cutting rules ("No full build/regression test until PHASE5").

## Notes for PHASE2's implementer

- `RenderPipeline::DeclareInto()` currently translates every `RenderPassDesc::view`
  into `ViewScope::Shared` UNCONDITONALLY (a deliberate, documented, trivial
  stand-in - see the phase doc's own Step 3.1: "this phase's own test double
  can use a trivial Shared-only translation, since no real per-view
  translation table exists to call into yet"). PHASE2 registering
  `"GpuSkinning"`/`"RenderOpaque"` on `m_offscreenRenderPipeline` will both
  naturally want `RenderViewId::Shared()` anyway (GPU Skinning's dispatch and
  the Opaque pass in today's actual call sequence for the Game View are not
  yet part of a real multi-view loop) - the real Game/Scene `ViewScope`
  translation table is explicitly PHASE3's job (`PHASE0_MASTER_STRATEGY.md`'s
  Locked Design Decision 5), not this phase's or PHASE2's.
- The blackboard's `ReportUnusedPublishesIfAny()` is implemented as a soft,
  non-fatal `std::fprintf(stderr, ...)` log line, per the phase doc's own
  explicit instruction ("do NOT resolve the design doc's own still-open
  'hard assert vs. soft log' question with anything more than a simple, safe
  default"). Nothing calls it yet (no `RenderPipeline::DeclareInto()` call
  site invokes it after running every provider) - PHASE2, once it wires GPU
  Skinning's real publish + Opaque's real fetch, is a natural place to also
  call `blackboard.ReportUnusedPublishesIfAny()` once per frame after
  `DeclareInto()` returns, if that phase's implementer judges it useful (not
  mandated by this phase, since it has no observable effect with zero real
  publishers/fetchers yet).
- `RenderPassFrameContext::finalTextureOutputs`/`finalVolumeTextureOutputs`
  exist but are not yet read by anything (no caller today runs
  `DeclareInto()` at all) - PHASE2/PHASE3's `Application.cpp` wiring is
  where these get read back out and forwarded into the existing
  `RenderGraph::Execute()` `build` callback's own `finalOutputs`/
  `KeepVolumeTextureOutput()` mechanism, per this phase's own header comment
  on that struct.
- `RenderPipeline::Register()`/`Unregister()` are both fully implemented and
  tested (including the `Unregister()` "same content, different pointer"
  `strcmp()` fallback), even though the phase doc calls `Unregister()` "a
  light escape hatch... not load-bearing" - available for later phases if
  ever needed, but not required by any of them today.
- `RenderPassId`'s debug-name registration gap (Deviation 1 above) means
  `DebugNameForPassId()` will report `"<unknown>"` for every id until some
  future call site explicitly calls `RegisterPassIdDebugName()` - this is
  harmless today (nothing reads `DebugNameForPassId()`'s output except
  `RenderPassBlackboard::ReportUnusedPublishesIfAny()`'s own log line, which
  nothing calls yet either), but worth knowing before relying on that debug
  log output for real debugging in a later phase.

## Definition of Done - checklist

- [x] `RenderPassId`/`RenderPassTag`/`RenderPassTagMask`/`RenderViewId`/
      `RenderPassDesc`/`RenderPassProvider`/`ProviderScope`/
      `RenderPassBlackboard`/`RenderPassFrameContext`/`RenderPipeline` all
      exist in `src/Renderer/RenderGraph/RenderPipeline.h`/`.cpp`.
- [x] `RenderPassEvent` exists in `RenderGraphTypes.h`; `PassRecord`/
      `RenderGraphPassSnapshot` both carry `renderPassEvent`, defaulted to
      `Opaques`, copied through for both surviving and culled passes.
      `ToString(RenderPassEvent)` exists with the same exhaustive-switch
      convention, with a matching test in `RenderGraphTypesTests.cpp`.
- [x] Both `RenderGraphBuilder::AddRenderPass()` overloads compile with the
      new trailing, defaulted `renderPassEvent` parameter; every
      pre-existing call site across the repo compiles unmodified (confirmed
      via a real incremental `gte_core` build, not just a code read).
- [x] `tests/Renderer/RenderGraph/RenderPipelineTests.cpp` exists, is
      registered in `tests/CMakeLists.txt`, and every test in it passes.
- [x] Zero files under `src/Application/` or `src/Editor/` touched. Zero
      real pass declaration anywhere goes through `RenderPipeline` yet
      (confirmed via `search_in_dir`).
- [x] Incremental compile of `gte_core` AND `GreatTamanaEngineTests`
      succeeded; the new test binary, run directly, shows every new test
      passing.
