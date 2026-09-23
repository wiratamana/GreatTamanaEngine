# PHASE4 — Completion Report: `RenderGraphCompiler::Compile()` Adjacency-List Rewrite (item 2.3)

## Parent

`PHASE0_MASTER_STRATEGY.md` / `PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`.
`PHASE3_COMPLETION_REPORT.md` was read first, per that document's own
instructions — its one carry-forward note (whether a future
`RenderGraphCompiler.cpp`/`RenderGraphBuilder.h` change ever needs to
construct/inspect a `PassContext` directly) does not apply here: this phase
touches `RenderGraphCompiler.h`/`.cpp` only, operates purely on
`PassRecord`/`CompiledGraphInput`, and never touches `PassContext`.

## What was done

Implemented the plan in `PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md` exactly,
resolving one genuine ambiguity its own text did not anticipate via
`ask_questions` (see below) before finalizing the test suite.

### 1. `RenderGraphCompiler.cpp` — the algorithmic rewrite

- **`edgeExists` (the `O(P^2)` `std::vector<std::vector<bool>>` matrix) is
  gone.** Replaced by two adjacency lists, `successors`/`predecessors`
  (`std::vector<std::vector<std::int32_t>>`), built during the exact same
  single edge-construction walk (Step 1) that already existed — `addEdge()`
  now pushes onto both lists instead of flipping one matrix cell.
- **Duplicate-edge handling — DE-DUPLICATED** (the RECOMMENDED option per the
  plan's own Step 3.1): `addEdge()` does a linear `std::find()` over
  `successors[from]` before pushing, skipping the push (both `successors` and
  `predecessors`) if `(from, to)` is already present. This was chosen over
  "leave duplicates and rely on the self-consistency argument" for the two
  reasons the plan itself named as the honest tie-breaker: it keeps the lists'
  size bounded by true edge count (matching the source document's own
  "`O(P+E)`" framing more tightly), and it makes the produced lists a 1:1
  mirror of the original matrix's own naturally-idempotent edge set, which is
  exactly what let this session's own manual/automated verification (see
  "Duplicate-edge verification" below) reason about the rewrite with the least
  possible cognitive overhead. `std::find()` here is `O(current out-degree of
  from)`, never `O(P)` — `<algorithm>` was already included, no new include
  needed.
- **Step 2 (backward reachability)**: the `O(P^2)` matrix column scan
  (`for predecessor in [0, passCount) check edgeExists[predecessor][node]`)
  is replaced by a direct walk of `predecessors[node]` — `O(P + E)` total.
  The root-marking scan immediately above it (untouched, per the plan's own
  instruction) is byte-for-byte unchanged.
- **Step 3 (Kahn's algorithm)**: in-degree is now computed directly from
  `predecessors[to].size()` (restricted to kept predecessors) instead of a
  full `O(P^2)` double loop; the main loop's successor scan walks
  `successors[node]` directly instead of a full matrix row scan. Both
  `O(P + E)` instead of `O(P^2)`. The `readyByEffectivePosition` tie-break
  (`std::set`, ordered by effective position) is completely unchanged — it
  never depended on the adjacency storage shape, only on which nodes become
  ready and when, which (given the de-duplication above) is identical to the
  matrix version.
- **Contradiction detection folded into Step 1**: the old call site
  (`DetectRenderPassEventContradictions(input, effectiveOrder)`, previously
  run as a separate pre-pass before `edgeExists`/`lastXWriter` even existed)
  is **genuinely removed** — `Compile()` no longer calls that standalone
  function at all. In its place:
  - A small, separate, `O(P+W)` preliminary walk (before Step 1's own loop)
    computes `firstTextureWriter`/`firstBufferWriter`/`firstVolumeTextureWriter`
    — the original-pass-index of the FIRST pass, in effective order, that
    writes each resource (or `-1`).
  - Inside Step 1's existing per-read-usage loop, exactly where `writer` is
    already computed for edge-construction purposes, an inline check appends
    to a new `std::vector<RenderPassEventContradiction> fastContradictions`:
    if `writer == -1` (orphan read so far), it consults `firstXWriter` for a
    later writer (guarded by the self-exclusion check below); otherwise, if
    the resolved writer's `renderPassEvent` is greater than the reader's, it
    reports `DeclaredEventOrderDisagreesWithRealDependency`.
  - **Self-exclusion guard, implemented exactly as specified**: `laterWriter
    != -1 && laterWriter != i` — a pass that reads and writes the same
    resource itself (the "self-loop" shape) can legitimately BE that
    resource's own `firstXWriter` entry, and must never be reported as
    contradicting itself. **Verified this guard is actually load-bearing, not
    just defensive-looking dead code**: temporarily changed the condition to
    `laterWriter != -1` (guard removed), rebuilt, and re-ran
    `SelfExclusionGuardPreventsFalsePositiveContradictionForSelfLoopReadWriteOfNeverOtherwiseWrittenResource`
    — it crashed immediately with exactly the expected
    `fastContradictions.empty()` assert firing (stderr correctly named the
    `"DepthPass"` pass as contradicting itself). Restored the guard, rebuilt,
    and confirmed all 34 targeted tests pass again (see "Verification" below).
  - After Step 1's loop finishes (still strictly before Step 2's culling),
    `fastContradictions` is reported through the **exact same** stderr/assert
    code the old pre-pass used (unchanged wording, unchanged assert
    condition text) — observable behavior (stderr text, assert firing before
    culling/reordering ever happens) is byte-for-byte unchanged; only *where*
    in the function body this now happens changed.
  - `DetectRenderPassEventContradictions()` itself — its signature, its
    independent `O(P^2*R)` implementation, and every existing test that calls
    it directly — is **byte-for-byte unchanged**. Confirmed via `git diff`
    (the function body, lines 49-134, has zero net changes).

### 2. `RenderGraphCompiler.h` — doc-comment hygiene (Step 3.6)

- Rewrote `Compile()`'s doc comment describing the old "runs
  `DetectRenderPassEventContradictions()` once, at the very top" behavior —
  it now describes the inline fast-path/`firstXWriter` design and explicitly
  states the standalone function is no longer called at all from `Compile()`,
  while remaining fully intact and independently testable.
- The stale "Pass counts in this engine are single digits today... an
  `O(passCount^2)` adjacency matrix is the right, simplest tool here" comment
  (which sat directly above the now-deleted `edgeExists` declaration) was
  rewritten to describe the new adjacency-list shape and correct the premise
  (20-40 real passes per frame, per `PHASE0_MASTER_STRATEGY.md`'s own Step 2
  evidence), not "single digits".
- Final grep confirms **zero** remaining stale references: `edgeExists` only
  appears once, inside a comment explicitly describing what the OLD matrix
  scan used to do (framed as "REPLACES... with a direct walk of
  `predecessors[node]`" — explanatory, not a stale current-state claim); "at
  the very top" has zero matches in either `RenderGraphCompiler.h` or `.cpp`
  (its one remaining match anywhere in `src/Renderer/RenderGraph/` is an
  unrelated sentence in `RenderGraph.h` about GPU timing readback).

### 3. Tests (`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`)

Every pre-existing test case (33 `RenderGraphCompilerTest` cases before this
phase) is **completely unmodified** — not one assertion was touched, loosened,
or deleted. 12 new cases were added:

- `PassWithTwoReadsOfSameResourceResolvingToSameEarlierWriterOrdersCorrectlyWithNoDuplicateEdgeIssues`
  (duplicate-edge, read side).
- `PassWithTwoWritesToDifferentResourcesSharingTheSameEarlierWriterOrdersCorrectlyWithNoDuplicateEdgeIssues`
  (duplicate-edge, WAW side).
- `LongWriteAfterWriteChainOrdersAllWritersThenTheFinalReaderInSequence`
  (3-writer WAW chain).
- `LargeFanOutFanInGraphCompilesCorrectlyExercisingTheAdjacencyListPathAtScale`
  (52-pass fan-out/fan-in correctness stress test).
- `SelfExclusionGuardPreventsFalsePositiveContradictionForSelfLoopReadWriteOfNeverOtherwiseWrittenResource`
  (dedicated self-exclusion regression, verified load-bearing above).
- `ReadOfNeverWrittenResourceProducesNoContradictionFromEitherPath`
  (never-written-resource equivalence, standalone + `Compile()`).
- `RenderGraphCompilerDeathTest.FastPathDetectsOrphanReadWithLaterWriterAndAbortsJustLikeTheStandaloneFunctionWould`
  (the mandatory fast-path/standalone-function equivalence proof — see below).

#### Fast-path/standalone equivalence test — implementation choice (Step 3.4/3.5)

`fastContradictions` is `Compile()`'s own internal state with no public
accessor. Per the plan's own explicit allowance ("pick whichever is less
invasive... document the choice"), **indirect observation via `EXPECT_DEATH`
was chosen over an internal test hook** — this codebase already has an
established, idiomatic `#ifndef NDEBUG` / `EXPECT_DEATH` death-test
convention for exactly this situation (`RenderGraphBuilderTests.cpp`,
`RenderGraphBarrierPlannerTests.cpp`, `DynamicChainDetectionTests.cpp`), so
this required zero new public API surface and zero header change of any
kind — strictly less invasive than a friend-grant/test-hook, and it directly
proves the thing that matters: that a graph shape the standalone function
independently reports a contradiction for still makes `Compile()`'s own new
inline fast path abort, exactly as it did when `Compile()` called the
standalone function directly.

**Genuine finding, confirmed via `ask_questions` (see below)**: only the
`OrphanReadWithLaterWriter` contradiction kind could be given this equivalence
proof through a real `Compile()` call. `DeclaredEventOrderDisagreesWithRealDependency`
is **structurally unreachable** through any real end-to-end `Compile()` call,
both *before* and *after* this phase (a pre-existing fact discovered, not
created, by this phase): `Compile()`'s own effective-order walk only ever
resolves a writer as a real (non-orphan) "nearest writer" for a read if that
writer was walked *before* the read in effective order, which by construction
of the `(RenderPassEvent, original index)` stable sort always means
`writer.renderPassEvent <= reader.renderPassEvent` — so the
`writer.renderPassEvent > reader.renderPassEvent` contradiction condition can
never actually be observed from a real `Compile()` call. It remains
observable only from the standalone function's own direct unit test using a
hand-built, deliberately-inconsistent `processingOrder`
(`EdgeContradictingDeclaredEventOrderIsDetected`, unchanged, still passing) —
exactly the same class of "kept as genuinely correct but practically
unreachable through any real caller" situation as this same file's own
existing cycle-detection `throw`. **Confirmed via `ask_questions`**: only add
the death-test-based equivalence proof for the reachable
`OrphanReadWithLaterWriter` kind, and document this finding rather than
building an internal test hook just to reach an already-known-unreachable
code path.

## Duplicate-edge verification

Both duplicate-edge shapes named in the plan (Step 3.5) are covered by
dedicated new tests and pass. Manually traced through the bookkeeping for the
read-side case, per the plan's own "verify this reasoning yourself" flag,
using the de-duplicated design actually implemented: since duplicates are
filtered at `addEdge()` itself (via the `std::find()` check), `predecessors[to]`/
`successors[from]` never contain a repeated `(from, to)` pair at all — the
in-degree computation (`predecessors[to].size()`, restricted to kept
predecessors) and the main Kahn loop's successor decrement therefore see
each real edge exactly once, with no reliance on the "duplicate decrements
happen back-to-back within the same pop" self-cancellation argument the plan
described for the (rejected) non-de-duplicating alternative.

## Verification

- Fast, targeted incremental compile check (no full build required by this
  phase's own rules, but performed anyway for extra confidence, matching
  PHASE3's own precedent since this is one of the campaign's three
  highest-risk phases):
  - `cmake --build build --target gte_core` — succeeds, 0 errors/warnings.
  - `cmake --build build` (default target) — succeeds, 0 errors/warnings.
- **Targeted `RenderGraphCompilerTests` run** (this phase's own hard
  acceptance gate):
  - `tests\GreatTamanaEngineTests.exe --gtest_filter=RenderGraphCompiler*` —
    **34/34 tests pass** (33 pre-existing `RenderGraphCompilerTest` cases,
    completely unmodified, still passing with byte-identical assertions +
    1 new `RenderGraphCompilerDeathTest` case), confirming byte-identical
    `executionOrder` for every existing fixture.
  - Explicitly re-verified the self-exclusion guard is load-bearing (see
    above) by temporarily removing it, confirming the dedicated regression
    test crashes with exactly the expected assert message, then restoring it
    and re-confirming all 34 tests pass again.
- Live sanity check (`run_app_background` + `gte_send_request`):
  - `GET /get_logs?min_level=Warning` → `{"count":0,...}` both before and
    after the interactions below.
  - `GET /get_swapchain` — Editor renders normally, sky/atmosphere gradient
    visible in both Scene and Game panels, correct docked layout.
  - `GET /activate_tab?name=Render Graph` + `GET /get_swapchain` — the
    "Render Graph" panel shows every real pass (Atmosphere Transmittance/
    Multi-Scattering/SkyView/Aerial-Perspective LUTs ×2 views, RenderOpaque,
    etc.) with correct real GPU timing and Reads/Writes columns, in the
    correct order — byte-identical in shape to PHASE2/PHASE3's own reports,
    confirming `Compile()`'s rewritten algorithm produces the exact same real
    production execution order as before.
  - `stop_app_background` — clean shutdown.

## What was NOT touched

- `DetectRenderPassEventContradictions()`'s own signature, implementation,
  and every existing direct-call test — byte-for-byte unchanged.
- `Compile()`'s public signature, `CompiledGraph`'s shape, `ResourceLifetime`'s
  shape.
- The effective-order computation (stable sort by `(RenderPassEvent,
  original index)`) — unchanged.
- `RenderGraphBuilder.h`/`CompiledGraphInput`'s shape — still the OLD
  9-parallel-vector shape; PHASE5's job to collapse it, and PHASE5 will need
  to re-verify this phase's new adjacency-list code still compiles correctly
  against the new slot-vector shape once it lands.
- No `default:` case was introduced anywhere — the new `firstXWriter`
  prescan and the inline contradiction check both legitimately EXTEND the
  same pre-existing exhaustive three-way `switch (usage.kind)` pattern
  already used throughout this file; no new/independent switch construct was
  added anywhere.

## Next phase

`PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md` (item 2.1) — the 9-parallel-vector
collapse into `TextureSlot`/`BufferSlot`/`VolumeTextureSlot`. Per this
phase's own mandatory next step (`PHASE0_MASTER_STRATEGY.md`'s Locked Design
Decision 5), a dedicated `delegate_task` double-check pass for THIS phase
(PHASE4) is being spawned immediately, before PHASE5 begins.
