# PHASE0 — Master Strategy: Render Graph Internal Refactor, P0+P1 (`render-pass-6`)

This document is the **orchestrator**. It does not itself contain implementation
steps — it defines the goal, the current situation, the locked design
decisions, and the map of child phase documents that carry out the actual code
changes, in order. Every child phase document follows the same three-step
shape (Goal / Situation / Plan) and must be executed in numeric order, since
each phase's code depends on the previous one existing and compiling.

Source material for this whole campaign (read these before starting):

- `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\render-graphs\REFACTOR_STRATEGY_SUMMARY.md`
  — the outside-in code review this entire campaign implements. **This
  campaign implements ONLY its Section 3 "P0" and "P1" items (items 1-6 of
  the Prioritized Refactor Plan) — P2 (2.8, `PassDesc` value type) and P3
  (2.5, 2.9, 2.10) are explicitly OUT OF SCOPE for `render-pass-6`.** Section
  4's "Guardrails" and Section 5's "Explicitly out of scope" both apply
  unchanged to every phase below.
- `task_manager/RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md` — the
  engine's own "never speculatively, only once a real, concrete need appears"
  discipline, which this campaign follows explicitly for PHASE1 (see its own
  Locked Design Decision below).
- `task_manager/render-pass-5/PHASE0_MASTER_STRATEGY.md` — the structural
  template this document mirrors (Goal/Situation/Plan shape, Phase Map table,
  dedicated double-check convention, Definition of Done shape).
- `AGENTS.md`'s "Render Pass System" section and "Testability & Regression
  Safety" section — both are binding conventions this campaign must not
  violate.

Read this file first. Then execute, in order:

- `PHASE1_SLOT_BUDGET_OVERFLOW_DIAGNOSTIC.md`
- `PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md`
- `PHASE3_PASSCONTEXT_PLAIN_RESOLVERS.md`
- `PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`
- `PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md`
- `PHASE6_RESOURCEKIND_DISPATCH_TABLE.md`
- `PHASE7_DOCS_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md`

Always re-read the previous phase's own completion report (each phase's
working agreement, mirrored from every other campaign in this repository, is
to write a short `PHASEn_COMPLETION_REPORT.md` next to this file once that
phase's code compiles) before starting the next one — it may record a
decision or a snag that changes a later phase's exact plan.

**Every implementation phase, and anything it further delegates, must use the
`ask_questions` tool whenever it hits a genuine ambiguity or a design choice
this document doesn't already pin down.** This rule propagates recursively:
if an implementation phase itself delegates a sub-task via `delegate_task`,
that delegation prompt must repeat this same instruction, verbatim, to
whatever it delegates to.

**Three of these phases are flagged as this campaign's highest-risk work —
PHASE4, PHASE5, and PHASE6 (see the Phase Map below) — and each gets its OWN
dedicated `delegate_task` double-check pass immediately after it lands,
BEFORE the single, whole-campaign second-iteration double-check.** Do not
fold those three dedicated checks into the general second-iteration review —
they happen first, individually. Each dedicated double-check must overwrite
its own phase's `.md` file in place (never create a new numbered file) if it
finds something worth fixing, exactly like the general second-iteration
review does for every other file.

Per the project owner's explicit instructions for this task: **only PHASE7
runs a full `cmake --build build` + full `ctest` pass.** PHASE1–6 (and their
own dedicated double-checks) do a fast, targeted incremental compile check
only — never a full regression run, to keep iteration fast on this machine.
Incremental build/debug/log-fetch via the engine's own HTTP endpoints
(`gte_send_request`) is encouraged at any phase to sanity-check behavior
live; a full build/ctest is not.

---

## Step 1: The Goal (Where are we going?)

Implement the **P0 and P1** items of `REFACTOR_STRATEGY_SUMMARY.md`'s
prioritized refactor plan against `src/Renderer/RenderGraph/*` — six
concrete code changes, landed as seven independently-compilable phases,
each provably **zero-behavior-change** (P0) or **behavior-preserving under an
explicit, testable determinism contract** (P1):

**P0 — zero-risk, pure-internal, immediate wins:**
1. **(2.4) Slot-budget overflow diagnostic.** `RenderGraphNameSlotTable::
   AssignOrGetSlot()` silently returns `kNoNameSlot` forever once its fixed
   budget (16 synchronous / 8 pipelined slots) is exhausted, and GPU timing
   for that pass silently, permanently disappears with zero signal anywhere.
   Add a one-time diagnostic (stderr + debug-build `assert`, mirroring
   `DetectRenderPassEventContradictions()`'s own established convention) plus
   a real boolean on `RenderGraphSnapshot` the Editor's "Render Graph" panel
   can show.
2. **(2.6) Extract `ExecuteCompiledGraph()`'s six concerns into named private
   methods.** `RenderGraph::ExecuteCompiledGraph()` (`RenderGraph.cpp`) is a
   ~430-line function interleaving GPU-timing readback, barrier application,
   timestamp bracketing, MRT attachment construction, draw-stat
   accumulation, and debug-texture-registry registration. Extract
   `BuildPassContext()`, `BuildColorAttachmentInfos()`,
   `BuildDepthAttachmentInfo()`, `RegisterDebugTextureSnapshots()`, and
   `RegisterDebugVolumeTextureSnapshots()` as private methods with zero
   behavior change.
3. **(2.7) `PassContext`: `std::function` → plain resolver struct.** Every
   pass, every `Execute()` call, currently gets six freshly-constructed
   `std::function` closures (`resolveReadTexture`/`resolveTexture`/
   `resolveBuffer`/`resolveVolumeTexture`/`recordDraw`/`recordIndirectDraw`).
   Replace the internals with plain non-owning pointers/references and
   ordinary member functions — zero change to any pass author's call site
   (`ctx.resolveReadTexture(handle)` etc. stays byte-for-byte identical).

**P1 — structural, moderate risk, high scaling payoff:**
4. **(2.3) `RenderGraphCompiler::Compile()` algorithmic rewrite.** Replace
   the `O(P^2)` adjacency-matrix (`std::vector<std::vector<bool>>`) with
   adjacency lists — `O(P + E)` for both the RAW/WAW edge scan and Kahn's
   algorithm — and make `DetectRenderPassEventContradictions()` reuse the
   same last-writer bookkeeping `Compile()` already builds instead of
   re-scanning from scratch (`O(P^2*R)` → `O(P*R)`). Zero change to the
   public `Compile()` signature; **byte-identical `executionOrder`** for
   every existing `RenderGraphCompilerTests.cpp` fixture is the hard
   acceptance gate.
5. **(2.1) Collapse the 9 parallel builder/`CompiledGraphInput` vectors into
   3 per-kind "slot" vectors.** `TextureSlot{ TextureDesc desc; const char*
   name; TextureImportInfo importInfo; }` (and `BufferSlot`/
   `VolumeTextureSlot` mirroring it exactly) replace the
   `xDescs`/`xNames`/`xImportInfo` trio kept in lockstep purely by
   convention today. Mechanical, but touches every `RenderGraphBuilder`/
   `RenderGraphCompiler`/`RenderGraph`/`RenderGraphSnapshot` call site that
   indexes these arrays.
6. **(2.2) Generic `ResourceKind` dispatch.** Replace the (confirmed, by
   direct grep) **seven** hand-duplicated `switch (usage.kind) { Texture /
   Buffer / VolumeTexture }` blocks (`RenderGraph.cpp`,
   `RenderGraphCompiler.cpp` ×5, `RenderGraphSnapshot.cpp`) with one small,
   generic per-kind dispatcher, naturally built on top of PHASE5's new slot
   vectors. **Must still fail to compile if a 4th `ResourceKind` is added
   without updating it** — this is the campaign's single highest-blast-radius
   change (a bug here silently miscompiles every resource kind's barrier/
   lifetime/culling logic at once), which is why it gets its own dedicated
   double-check (see Phase Map below).

This is deliberately **P0+P1 only** — `REFACTOR_STRATEGY_SUMMARY.md`'s P2
(2.8, `PassDesc` value type for `AddRenderPass()`) and P3 (2.5 resource-pool
indexed lookup, 2.9 `PassRecord::DebugMetadata` grouping, 2.10 barrier-branch
de-duplication) are explicitly NOT part of `render-pass-6` — P2 is
call-site-churn-heavy and P3 is opportunistic/low-urgency by the source
document's own explicit prioritization. Do not pull either forward
speculatively.

## Step 2: The Situation (Where are we now?)

Confirmed directly against the current source (2026-09-23):

- `src/Renderer/RenderGraph/` contains 23 files: `RenderGraph.h/.cpp`,
  `RenderGraphBuilder.h/.cpp`, `RenderGraphCompiler.h/.cpp`,
  `RenderGraphBarrierPlanner.h/.cpp`, `RenderGraphTypes.h/.cpp`,
  `RenderGraphNameSlotTable.h` (header-only), `RenderGraphResourcePool.h/.cpp`,
  `RenderGraphSnapshot.h/.cpp`, `RenderGraphTimestampPool.h/.cpp`,
  `RenderGraphDebugTextureRegistry.h/.cpp`,
  `RenderGraphDebugVolumeTextureRegistry.h/.cpp`, `RenderPipeline.h/.cpp`.
- `tests/Renderer/RenderGraph/` mirrors this with 10 test files, including
  `RenderGraphCompilerTests.cpp` (PHASE4's acceptance oracle),
  `RenderGraphBuilderTests.cpp`/`RenderGraphSnapshotTests.cpp` (PHASE5's
  acceptance oracles), `RenderGraphNameSlotTableTests.cpp` (PHASE1's), and
  `RenderGraphBarrierPlannerTests.cpp`/`RenderGraphTypesTests.cpp`.
- **Item 2.4 evidence**: `RenderGraphNameSlotTable::AssignOrGetSlot()`
  (`RenderGraphNameSlotTable.h`, lines ~61-76) returns `kNoNameSlot` with zero
  side effect the moment `m_names.size() >= m_slotBudget`. `RenderGraph.cpp`'s
  `WriteBegin()`/`WriteEnd()` call sites (via `m_timestampPool`) already
  silently no-op for `timingSlot == kNoNameSlot` — there is no log line, no
  assert, no snapshot flag anywhere in this path today.
- **Item 2.6 evidence**: `RenderGraph::ExecuteCompiledGraph()`
  (`RenderGraph.cpp`, lines 242-670, ~430 lines) is already partially
  decomposed (`EnsureTextureResolved`/`EnsureBufferResolved`/
  `ApplyUsageBarrierIfNeeded` are already private methods) but the per-pass
  loop body (lines 298-587) and the MRT attachment-building block (lines
  416-559) remain large, deeply-nested inline code building six
  `PassContext` lambdas (lines 360-414) and a `std::vector<
  VkRenderingAttachmentInfo>`/depth-attachment block inline.
- **Item 2.7 evidence**: `PassContext` (`RenderGraph.h`, lines 89-180) has
  six `std::function` fields (`resolveReadTexture`, `resolveTexture`,
  `resolveBuffer`, `resolveVolumeTexture`, `recordDraw`,
  `recordIndirectDraw`), each freshly constructed as a capturing lambda
  inside `ExecuteCompiledGraph()`'s per-pass loop, every pass, every
  `Execute()` call (twice per frame).
- **Item 2.3 evidence**: `RenderGraphCompiler.cpp`'s `Compile()` (lines
  158-531) builds `edgeExists` as a full `std::vector<std::vector<bool>>`
  (line 243) with nested `O(P^2)` loops for both the backward-reachability
  cull (lines 364-402) and Kahn's in-degree computation (lines 418-459).
  `DetectRenderPassEventContradictions()` (lines 49-134) does an independent
  backward-then-forward linear scan per read, per pass — `O(P^2*R)` worst
  case — with its own separate `sameResource` lambda duplicating the
  `switch (usage.kind)` pattern.
- **Item 2.1 evidence**: `CompiledGraphInput` (`RenderGraphBuilder.h`, lines
  119-152) carries `textureDescs`/`textureNames`/`textureImportInfo`,
  `bufferDescs`/`bufferNames`/`bufferImportInfo`,
  `volumeTextureDescs`/`volumeTextureNames`/`volumeTextureImportInfo` — 9
  vectors, kept in lockstep purely by "index i means the same resource"
  convention. `RenderGraphBuilder` (same file, lines 534-552) carries the
  builder-side mirror (`m_textureDescs`/`m_textureNames`/
  `m_textureImportInfo`, etc.) — another 9 vectors.
- **Item 2.2 evidence (confirmed by direct grep, corrects the source
  document's own "~6" estimate to exactly 7)**: `switch (usage.kind)` or an
  equivalent per-kind branch appears at: `RenderGraph.cpp:170`
  (`ApplyUsageBarrierIfNeeded`), `RenderGraphCompiler.cpp:59`
  (`DetectRenderPassEventContradictions`'s own `sameResource` lambda,
  written as `switch (a.kind)`), `RenderGraphCompiler.cpp:286` (RAW-edge
  scan), `RenderGraphCompiler.cpp:311` (WAW-edge scan),
  `RenderGraphCompiler.cpp:371` (root-marking scan),
  `RenderGraphCompiler.cpp:498` (the lifetime `touch()` lambda), and
  `RenderGraphSnapshot.cpp:64` (`ResourceUsageName()`) — **seven** independent
  hand-rolled three-way branches, not six.
- `AGENTS.md`'s "Testability & Regression Safety" section requires: every
  Tier-1 code change gets a matching test change; the actual test suite must
  be run (not just a successful build) before any change under `src/` is
  considered done; Tier-2 (GPU-dependent) code has no automated coverage and
  that is accepted, not a blocker.

## Step 3: The Plan (How do we get there?)

### Locked Design Decisions

Confirmed via `ask_questions` with the project owner before writing any child
phase document. MUST NOT be silently changed by a later phase without
updating this file first:

1. **Strategy `.md` files live in `task_manager/render-pass-6/`** (the
   already-given, empty folder for this campaign).
2. **Execution order within P0**: PHASE2 (2.6, pure extraction) runs BEFORE
   PHASE3 (2.7, the perf-sensitive `std::function` → plain-struct swap).
   Rationale (confirmed via `ask_questions`): extraction is zero-behavior-
   change and shrinks `BuildPassContext()` into a small, isolated,
   easy-to-diff method first; the actually-risky internals swap then lands
   as a small, single-concern diff against an already-stable extraction,
   never tangled inside the original 430-line function. This mirrors
   `REFACTOR_STRATEGY_SUMMARY.md`'s own Section 4 guardrail: "never bundle
   [two concerns] in the same change, so a regression can always be
   bisected to one concern."
3. **PHASE1 (2.4) is diagnostic-only — the fixed slot-budget constants
   (`kSynchronousTimingSlotBudget = 16`, `kPipelinedTimingSlotBudget = 8`,
   `RenderGraph.h`) are NOT bumped as part of this campaign.** Confirmed via
   `ask_questions`: this engine's own explicit "never speculatively, only
   once a real, concrete need appears" discipline (see
   `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`) applies here —
   there is no measured evidence of a real overflow at today's pass counts;
   the diagnostic's whole job is to produce that evidence, not to pre-empt
   it by guessing at a bigger number. A future campaign bumps the constants
   later, backed by an actual observed overflow log line.
4. **PHASE5 (2.1) new type shape — confirmed via `ask_questions`, use
   exactly this shape, these names**:
   ```cpp
   struct TextureSlot {
       TextureDesc desc;
       const char* name = nullptr;
       TextureImportInfo importInfo;
   };
   struct BufferSlot {
       BufferDesc desc;
       const char* name = nullptr;
       BufferImportInfo importInfo;
   };
   struct VolumeTextureSlot {
       VolumeTextureDesc desc;
       const char* name = nullptr;
       VolumeTextureImportInfo importInfo;
   };
   ```
   `CompiledGraphInput`/`RenderGraphBuilder` each replace their 9 parallel
   vectors with exactly 3: `std::vector<TextureSlot> textures`,
   `std::vector<BufferSlot> buffers`, `std::vector<VolumeTextureSlot>
   volumeTextures`. No backward-compatible aliasing of the old field names
   is kept — every call site is rewritten to the new shape (this is
   explicitly a P1 "single, atomic PR with full test-suite re-run" item per
   the source document, not something that needs a compatibility shim).
5. **Dedicated double-check phases: PHASE4, PHASE5, AND PHASE6 (three
   total)** — confirmed via `ask_questions`, expanding beyond the two
   render-pass-5 used as precedent. PHASE6 (2.2, the generic dispatch table)
   earns its own dedicated check specifically because it is a strictly
   higher blast-radius change than PHASE5's data-shape collapse: it replaces
   seven independently hand-verified, individually-audited
   `switch(ResourceKind)` blocks with one shared dispatcher every barrier/
   lifetime/culling code path now funnels through — a bug here silently
   miscompiles every resource kind at once. Each dedicated double-check
   overwrites its own phase's `.md` file in place (never creates a new
   numbered file) — see this document's own top-level instructions.
6. **Documentation**: the final phase (PHASE7) does NOT add a new AGENTS.md
   heading for this campaign — confirmed via `ask_questions`: every existing
   AGENTS.md section documents a shipped, feature-visible capability change;
   this campaign ships no new capability and no behavior change. PHASE7
   instead appends a short, honest note to the existing "Render Pass System"
   section ("internals reworked for scale/perf/readability, no behavior
   change") — never implying a new feature exists.
7. **Only PHASE7 runs a full build + full `ctest` pass.** PHASE1–6 (and
   their dedicated double-checks) do a fast, targeted incremental compile
   check only.
8. **No `std::cout`/`printf`-style ad hoc debugging.** Any phase that needs
   to inspect live engine behavior must use the engine's own internal
   debug/logging facilities (`GTE_LOG_DEBUG`/`INFO`/`WARNING`/`ERROR`, see
   AGENTS.md's "Logging" section) and fetch results via
   `gte_send_request` against `GET /get_logs`, `GET /get_swapchain`,
   `GET /get_game_view`, `GET /get_texture`, or the Frame Debugger endpoints
   — never `std::cout`/raw `fprintf` added as a *new* debugging aid (the
   pre-existing, deliberate `std::fprintf(stderr, ...)` calls in
   `DetectRenderPassEventContradictions()` are an established diagnostic
   convention this campaign extends in PHASE1, not a precedent for ad hoc
   `cout` debugging elsewhere).

### Non-Goals (explicitly out of scope for `render-pass-6`)

- **No P2 item (2.8, `PassDesc` value type for `AddRenderPass()`)** — a
  separate, larger, call-site-churn-heavy campaign of its own, per the
  source document's own prioritization ("schedule it after the internals
  have stabilized, not before").
- **No P3 items** (2.5 resource-pool indexed lookup, 2.9
  `PassRecord::DebugMetadata` field grouping, 2.10 Buffer/VolumeTexture
  barrier-branch de-duplication) — opportunistic/low-urgency by the source
  document's own explicit accounting; 2.10 is noted to mostly fall out for
  free once PHASE6 lands, but is not itself a deliverable of this campaign.
- **No change to `RenderGraph`'s public two-calls-per-frame `Execute()`
  contract.**
- **No change to `PassContext`'s call-site shape** — pass authors calling
  `ctx.resolveReadTexture(handle)`, `ctx.recordDraw(...)`, etc. see zero
  source change; only `PassContext`'s internals change (PHASE3).
- **No change to `AddPass()`/`AddComputePass()`/`AddRenderPass()`'s public
  signatures** — PHASE5's slot-vector collapse only touches
  `CompiledGraphInput`/`RenderGraphBuilder`'s internal storage and
  `RenderGraphCompiler`/`RenderGraph`/`RenderGraphSnapshot`'s internal
  indexing, never the public `PassBuilder`/`AddPass` API surface pass
  authors use.
- **No relaxation of the "no `default:` case, ever" exhaustive-switch
  discipline** — PHASE6's generic dispatcher must still fail to compile if a
  4th `ResourceKind` is added without updating it.
- **No change to `Compile()`'s determinism contract** — PHASE4 must produce
  byte-identical `executionOrder` for every existing
  `RenderGraphCompilerTests.cpp` fixture, before and after.

### Phase Map

| Phase | Item(s) | Deliverable | Dedicated double-check? |
|---|---|---|---|
| **1** | 2.4 | `RenderGraphNameSlotTable`/`RenderGraph`/`RenderGraphSnapshot` gain a real "timing budget exhausted" diagnostic (log-once + snapshot flag). Zero behavior change to anything else. | No |
| **2** | 2.6 | `BuildPassContext()`/`BuildColorAttachmentInfos()`/`BuildDepthAttachmentInfo()`/`RegisterDebugTextureSnapshots()`/`RegisterDebugVolumeTextureSnapshots()` extracted as private `RenderGraph` methods. Zero behavior change. | No |
| **3** | 2.7 | `PassContext`'s six `std::function` fields replaced by plain non-owning pointers + ordinary member functions. Zero call-site change for pass authors. | No |
| **4** | 2.3 | `RenderGraphCompiler::Compile()` rewritten to adjacency lists (`O(P+E)`); `DetectRenderPassEventContradictions()` reuses `Compile()`'s own last-writer maps. Byte-identical `executionOrder` verified against the full existing test suite. | ⚠️ **Yes** |
| **5** | 2.1 | `TextureSlot`/`BufferSlot`/`VolumeTextureSlot` replace the 9 parallel vectors across `RenderGraphBuilder`/`CompiledGraphInput`/every consuming call site. | ⚠️ **Yes** |
| **6** | 2.2 | Generic `ResourceKind` dispatcher replacing all 7 hand-duplicated switches, built on PHASE5's slot vectors. | ⚠️ **Yes** |
| **7** | — | Docs (`AGENTS.md`'s "Render Pass System" section gets a short note); full `cmake --build build`; full `ctest`; a live, HTTP-driven smoke test; `CAMPAIGN_COMPLETION_REPORT.md`. | No (full build/test IS this phase's own job) |

### Definition of Done for the whole campaign

- `cmake --build build` succeeds (default configuration, `GTE_ENABLE_EDITOR`
  ON).
- `ctest` passes, including every updated test file/case this campaign
  touches, with **zero regressions** against the baseline test count
  recorded at the start of PHASE7.
- `RenderGraphCompilerTests.cpp`'s full existing suite produces
  byte-identical `executionOrder` results before and after PHASE4's
  algorithmic rewrite (verified explicitly, not merely assumed passing).
- A live scene's Game/Scene View rendering is visually IDENTICAL before and
  after this whole campaign (pure internal refactor, zero rendering-behavior
  change) — confirmed via `GET /get_game_view`/`GET /get_swapchain`
  before/after comparison, and the Editor's "Render Graph" panel/Frame
  Debugger still showing every pass's correct reads/writes/stats.
- The slot-budget overflow diagnostic (PHASE1) is proven correct by its own
  Tier-1 test suite (`RenderGraphNameSlotTableTests.cpp`'s `JustOverflowed()`
  cases, `RenderGraphSnapshotTests.cpp`'s `timingSlotBudgetExhausted`
  pass-through case) — a genuine LIVE overflow is not required as proof and
  is not expected to be reproducible at this engine's current real pass
  counts, since Locked Design Decision 3 deliberately forbids bumping
  `kSynchronousTimingSlotBudget`/`kPipelinedTimingSlotBudget` as part of this
  campaign. PHASE7's own live smoke test instead confirms the OPPOSITE,
  equally important case: the new diagnostic does NOT fire spuriously during
  ordinary operation at today's real pass counts.
- Every exhaustive `switch` this campaign touches or replaces still has NO
  `default:` case — confirmed by deliberately checking that adding a 4th
  `ResourceKind` (a scratch, throwaway local edit, reverted before
  committing) fails to compile at every one of PHASE6's dispatcher call
  sites.
- Validation layers report zero new warnings/errors.
- `AGENTS.md`'s "Render Pass System" section carries a short, honest note
  about this internal refactor — no new heading, no claim of new capability.

### Regression / build commands (reference — see each phase for exact use)

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Per Locked Design Decision 7: **only `PHASE7` runs a full build + full
`ctest` pass.** Phases 1–6 (and their dedicated double-checks) do a fast,
targeted incremental compile check only.
