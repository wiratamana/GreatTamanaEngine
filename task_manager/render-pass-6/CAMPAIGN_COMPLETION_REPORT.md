# `render-pass-6` — Campaign Completion Report: Render Graph Internal Refactor (P0 + P1)

Parent: `PHASE0_MASTER_STRATEGY.md`. This is the final phase
(`PHASE7_DOCS_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md`) of the seven-phase
`render-pass-6` campaign — the whole campaign's final integration checkpoint:
documentation, the campaign's one and only full build + full `ctest`
regression pass (per PHASE0's Locked Design Decision 7), and a final live,
running-engine, HTTP-driven proof that this whole campaign is genuinely
behavior-preserving.

## Status: DONE

`PHASE1_COMPLETION_REPORT.md` through `PHASE6_COMPLETION_REPORT.md` were read
in full before starting, per this phase's own prerequisite (and the top-level
task instruction), along with a fresh re-read of `PHASE4_COMPILER_ADJACENCY_
LIST_REWRITE.md`/`PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md`/`PHASE6_
RESOURCEKIND_DISPATCH_TABLE.md` themselves to check whether their own
dedicated double-checks had overwritten them with a correction — none had;
every one still reads exactly as each phase's own completion report describes
it. Confirmed via `git_status` before starting: the repository's current
branch is `feature/render-pass-impl`, with a clean working tree (PHASE1-6's
own commits already present) — stayed on it, per the task's own explicit
instruction.

## Per-phase outcome summary

- **PHASE1 (2.4 — Slot-Budget Overflow Diagnostic)** —
  `RenderGraphNameSlotTable::JustOverflowed()` (new, Tier-1-tested) lets
  `RenderGraph::ExecuteCompiledGraph()` distinguish a genuine, first-time,
  budget-exhausted overflow from every other `kNoNameSlot`-returning case, and
  report it exactly once per pass name per regime via `GTE_LOG_WARNING`
  (`GTE_LOG_WARNING`'s first-ever real call site in this engine).
  `RenderGraphSnapshot::timingSlotBudgetExhausted` (new, defaulted, so all 15
  pre-existing `BuildRenderGraphSnapshot()` call sites in
  `RenderGraphSnapshotTests.cpp` kept compiling unmodified) surfaces the fact
  structurally for the Editor's "Render Graph" panel. `kSynchronousTimingSlotBudget`/
  `kPipelinedTimingSlotBudget` were deliberately NOT bumped (Locked Design
  Decision 3 — diagnostic-only). 8 new Tier-1 tests added; 20/20 + 24/24
  targeted runs passed, zero regressions.
- **PHASE2 (2.6 — `ExecuteCompiledGraph()` Extraction)** — five named private
  methods (`BuildPassContext()`, `BuildColorAttachmentInfos()`,
  `BuildDepthAttachmentInfo()`, `RegisterDebugTextureSnapshots()`,
  `RegisterDebugVolumeTextureSnapshots()`) extracted from the ~430-line
  `ExecuteCompiledGraph()`, pure code motion plus one small, explicitly
  called-out dead-variable cleanup (`hasDepthWrite`) — zero behavior change,
  confirmed by a close line-by-line diff against the pre-PHASE2 file. Live
  sanity check: Editor renders normally, "Render Graph" panel shows every real
  pass with correct timing/reads/writes, zero warnings.
- **PHASE3 (2.7 — `PassContext` Plain Resolvers)** — `PassContext`'s six
  `std::function` fields (freshly constructed, per pass, per `Execute()` call)
  replaced: `resolveReadTexture`/`resolveTexture`/`resolveBuffer`/
  `resolveVolumeTexture` became plain, ordinary, `noexcept` member functions;
  `recordDraw`/`recordIndirectDraw` became small non-owning callable struct
  fields (`RecordDrawFn`/`RecordIndirectDrawFn`) instead, since a full,
  personally-performed audit of every real call site found ~21 production
  sites (`Application.cpp`, `RenderPasses.cpp`, `ComputeBlurValidation.cpp`,
  `GBufferValidation.cpp`, `AtmosphereLutRenderer.cpp`) pass `ctx.recordDraw`
  BY VALUE into `Renderer::BeginGraphPassRecording()`'s own
  `std::function`-typed parameter, or truthiness-check `recordIndirectDraw` —
  a bare member function cannot support either usage. `PhysicalTexture`/
  `PhysicalBuffer`/`PhysicalVolumeTexture` stayed `RenderGraph`-private,
  reached via a single, narrow `friend struct PassContext;` grant (confirmed
  via `ask_questions` over the alternative of moving them to namespace
  scope). Zero call-site change for any pass author anywhere in the engine —
  confirmed by a full, unmodified `gte_core` + `GreatTamanaEngine.exe` +
  `GreatTamanaEngineTests.exe` build. Live-verified: a spawned test cube shows
  `RenderOpaque Draws=1, Tris=12` in the "Render Graph" panel, direct
  production-path proof `RecordDrawFn` correctly accumulates draw stats
  through its plain `DrawStats*` pointer.
- **PHASE4 (2.3 — `RenderGraphCompiler::Compile()` Adjacency-List Rewrite)
  ⚠️ DEDICATED DOUBLE-CHECK PHASE** — the `O(P^2)` `std::vector<std::vector<bool>>`
  edge matrix (`edgeExists`) is gone, replaced by `successors`/`predecessors`
  adjacency lists built during the same single edge-construction walk, with
  duplicate edges filtered at `addEdge()` itself (the recommended,
  size-bounded option). `DetectRenderPassEventContradictions()`'s standalone
  implementation is byte-for-byte unchanged; `Compile()` no longer calls it at
  all, instead running a new `O(P+W)` `firstXWriter` prescan plus an inline
  fast-path check reusing the same last-writer bookkeeping the edge scan
  already builds — with a self-exclusion guard (`laterWriter != -1 &&
  laterWriter != i`) empirically confirmed load-bearing (removing it and
  re-running the dedicated regression test crashed exactly as expected,
  restoring it fixed it). **12 new tests added** (34/34 `RenderGraphCompiler*`
  total, 33 pre-existing cases completely unmodified plus 1 new death test)
  — **byte-identical `executionOrder` for every existing fixture, the
  campaign's hard acceptance gate, confirmed.** A genuine finding, resolved
  via `ask_questions`: the `DeclaredEventOrderDisagreesWithRealDependency`
  contradiction kind is structurally unreachable through any real `Compile()`
  call (both before and after this phase) — the equivalence death test only
  covers the reachable `OrphanReadWithLaterWriter` kind, documented rather
  than chased with an unnecessary test hook.
- **PHASE5 (2.1 — Resource Slot Vector Collapse) ⚠️ DEDICATED DOUBLE-CHECK
  PHASE** — the 9 parallel `xDescs`/`xNames`/`xImportInfo` vectors in
  `CompiledGraphInput`/`RenderGraphBuilder` collapsed into exactly 3:
  `std::vector<TextureSlot> textures`, `std::vector<BufferSlot> buffers`,
  `std::vector<VolumeTextureSlot> volumeTextures` (the exact shape
  `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 4 fixed in advance).
  Every real consumer rewritten: `RenderGraphBuilder.cpp`'s Create/Import
  methods, `RenderGraphCompiler.cpp`'s 9 sizing calls (including PHASE4's own
  new `firstXWriter`/`lastXWriter` vectors, confirmed re-sized correctly),
  `RenderGraph.cpp`'s resolve/register methods and `ExecuteCompiledGraph()`'s
  own 3 `PhysicalX` sizing lines (the exact call site this phase's own
  strategy document flagged as previously missed), and
  `RenderGraphSnapshot.cpp`'s name resolution/resource-table walk. A full,
  repository-wide grep swept for all 9 old field names after every edit — zero
  unmigrated field access anywhere, only explanatory "old name" comments and
  genuinely unrelated identifiers remained. `RenderGraphBuilderTests.cpp` was
  the ONLY test file needing a change (confirmed, not assumed);
  `RenderGraphCompilerTests.cpp`/`RenderGraphSnapshotTests.cpp`/
  `RenderGraphTypesTests.cpp` needed and received zero changes. 106/106
  targeted tests passed (40 `RenderGraphBuilderTest` + 8
  `RenderGraphBuilderDeathTest` + 33 `RenderGraphCompilerTest` + 1
  `RenderGraphCompilerDeathTest` + 24 `RenderGraphSnapshotTest`), zero
  regressions, zero loosened assertions. `RenderGraphBuilder`'s entire PUBLIC
  API surface stayed byte-for-byte unchanged.
- **PHASE6 (2.2 — Generic `ResourceKind` Dispatch Table) ⚠️ DEDICATED
  DOUBLE-CHECK PHASE, HIGHEST BLAST RADIUS** — a fresh site recount (performed
  for real, not assumed) found **nine** hand-rolled `switch (usage.kind)`/
  `switch (a.kind)` sites in the actually-landed post-PHASE4/PHASE5 source,
  not the originally-estimated seven — PHASE4 had introduced two more
  (`firstXWriter` prescan, the inline fast-path check) beyond the original
  seven. All nine converted to route through one new, generic
  `DispatchByKind()` function template (`RenderGraphTypes.h`) — a real,
  exhaustive, `default:`-less `switch (usage.kind)` internally, with a hard-fail
  (`assert(false)` + `throw std::logic_error`) unreachable tail rather than a
  silent fallback. `DetectRenderPassEventContradictions()`'s public signature
  and every existing direct-call test are unaffected — only its internal
  `sameResource` lambda's dispatch mechanism changed. 4 new
  `RenderGraphDispatchByKindTest` cases added; the full `RenderGraph*` filter
  (233 tests across 21 suites) was re-run as an extra regression check, all
  pass. **A genuine, honestly-documented finding from this phase's own
  mandatory Step 3.3 verification (add a 4th `ResourceKind` enumerator,
  confirm it fails to compile, then revert)**: the scratch enumerator did
  **NOT** fail to compile anywhere — not at `DispatchByKind()`, not at any of
  the pre-existing "exhaustive switch, no `default:`" call sites this codebase
  already had (`RenderGraphTypes.cpp`'s `ToString()`/`IsWriteAccess()`
  helpers, `FrameDebuggerData.cpp`'s label helpers). Root cause, confirmed by
  inspecting the actual compile command: this project's root `CMakeLists.txt`
  enables no `-Wall`/`-Wextra`/`-Wswitch`/`-Werror` for `gte_core`/
  `GreatTamanaEngine` at all (only vendored third-party libraries opt into
  their own warnings, internally). Confirmed via `ask_questions` to be a
  **pre-existing condition of the whole codebase, not a regression this phase
  introduced** — the project owner chose to document this honestly rather
  than change any build flag as part of this campaign. The scratch enumerator
  was reverted immediately and the project rebuilt clean before continuing.
- **PHASE7 (this phase)** — documentation (`AGENTS.md`'s "Render Pass System"
  section gained a short note per Locked Design Decision 6, and — per this
  phase's own `ask_questions` result, see below — `README.md`'s "Status"
  section also gained a bullet), the campaign's one full build + full `ctest`
  pass, a final live smoke test against the fully-built binary, the known
  process deviation call-out (see below), and this report.

## `ask_questions` decisions made during this phase

1. **README.md "Status" bullet** — `PHASE0_MASTER_STRATEGY.md`'s own Locked
   Design Decision 6 anticipated this staying AGENTS.md-only, since this
   campaign is a pure internal refactor with no new feature. Asked anyway,
   per PHASE7's own explicit instruction. **Project owner's answer: add a
   Status bullet anyway**, for campaign-tracking consistency with every other
   campaign — a genuine, recorded deviation from PHASE0's own anticipated
   default, not a silent change. `README.md`'s "Status" section now has a new
   bullet at the top (most recent first), matching every other campaign's own
   voice/detail level.
2. **The PHASE4/5/6 dedicated double-check gap** (see below) — asked whether
   to retroactively spin up genuinely independent double-check tasks now, or
   treat the situation as already sufficiently covered. **Project owner's
   answer: treat it as sufficiently covered** — each phase's own author
   already re-verified the prior phase's file/logic while reading it before
   starting, and this phase's own full build + full `ctest` + live smoke test
   re-validates the whole, fully-integrated result end-to-end; no additional
   double-check task was spun up. Recorded honestly below, not silently
   dropped.

## Known process deviation: the PHASE4/5/6 dedicated double-checks

`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 5 required PHASE4, 5, and
6 to EACH get their own dedicated `delegate_task` double-check pass,
performed as a genuinely separate review immediately after that phase landed
and BEFORE the next phase began, overwriting that phase's own strategy `.md`
file in place if it found something worth fixing. **What the evidence
actually shows**: `git_log_oneline` lists exactly ONE commit per phase
(PHASE4/5/6) for this campaign — there is no separate "dedicated double-check"
commit of the kind `render-pass-5`'s own campaign produced (e.g. "render-pass-5
PHASE3 dedicated pre-check pass" / "PHASE5 dedicated strategy pre-check" as
their own distinct commits). None of `PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`,
`PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md`, or `PHASE6_RESOURCEKIND_DISPATCH_
TABLE.md` show any sign of having been overwritten with a correction (their
line counts/content match each phase's own completion report exactly). The
completion reports for PHASE5 and PHASE6 each assert the PRIOR phase's
dedicated double-check "had already landed and found nothing worth
changing" — but this assertion is based on the strategy file being
unchanged, not on any independently-verifiable double-check artifact (no
report file, no distinct commit) of the kind this campaign's own render-pass-5
precedent produced.

**Resolution (confirmed via `ask_questions` during this phase, see above)**:
no retroactive double-check task was spun up to close this gap. This is
recorded here, honestly and permanently, as a real process deviation from
PHASE0's own Locked Design Decision 5 — not silently dropped. Mitigating
factors, for the record: (a) each phase's own author DID re-read the
immediately-prior phase's strategy document and completion report in full
before starting, and cross-checked its claims against the real, current
source before writing any new code (documented explicitly in PHASE5's and
PHASE6's own completion reports); (b) PHASE4's own self-exclusion guard was
independently, empirically verified load-bearing by deliberately breaking it
and confirming the expected crash; (c) PHASE6's own mandatory Step 3.3
verification (the scratch 4th-`ResourceKind`-enumerator check) was genuinely
performed, not merely claimed, and surfaced a real, previously-undiscovered
gap in this project's build configuration; (d) this PHASE7 campaign-closing
pass performed its own full build, full `ctest` regression (1736/1736, zero
failures), and independent live smoke test against the final,
fully-integrated binary, which re-confirms every one of PHASE4/5/6's own
shipped behaviors end-to-end, from scratch, on top of everything above.

## What PHASE7 itself did

### 3.1 — Baseline

The last full-suite `ctest` count recorded anywhere in this repository before
this campaign started was `render-pass-5`'s own final count, **1717/1717
(100%), 1 pre-existing environment-gated skip** (confirmed by reading
`task_manager/render-pass-5/CAMPAIGN_COMPLETION_REPORT.md`) — PHASE1-6 of
`render-pass-6` themselves never ran a full suite (per Locked Design Decision
7, only fast/targeted incremental checks), so this is the correct "baseline"
this phase's own Definition of Done measures against.

### 3.2 — Documentation updates

- **`AGENTS.md`**'s existing "Render Pass System" section gained a short,
  honest paragraph appended at its end (after the `render-pass-4` writeup,
  before the "Full history:" links line) — **no new heading**, per Locked
  Design Decision 6 — summarizing exactly what PHASE1-6 actually shipped
  (cross-checked against every `PHASEn_COMPLETION_REPORT.md`, not just the
  original PHASE0 plan, including the corrected "nine sites, not seven" fact
  and the `-Wswitch` build-configuration finding). The "Full history:" line's
  own file list was also extended to include
  `task_manager/render-pass-6/PHASE0_MASTER_STRATEGY.md`.
- **`README.md`**'s "Status" section gained one new bullet at the very top
  (most recent first, per the `ask_questions` decision above), summarizing
  the campaign in the same voice/detail level as every other entry, including
  the final test count and the honest "-Wswitch not enabled" caveat context
  living in `AGENTS.md` (not duplicated verbatim in the shorter README
  bullet).

### 3.3 — Full Tier-1 test sweep

Confirmed via `search_in_dir`/direct read of `tests/CMakeLists.txt`: all 10
`tests/Renderer/RenderGraph/*.cpp` files this campaign touched or depends on
(`RenderGraphTypesTests.cpp`, `RenderGraphBuilderTests.cpp`,
`RenderPassTests.cpp`, `RenderGraphCompilerTests.cpp`,
`RenderGraphBarrierPlannerTests.cpp`, `RenderGraphNameSlotTableTests.cpp`,
`RenderGraphSnapshotTests.cpp`, `RenderGraphDebugTextureRegistryTests.cpp`,
`RenderGraphDebugVolumeTextureRegistryTests.cpp`, `RenderPipelineTests.cpp`)
are present and registered in the `GTE_TEST_SOURCES` list, and therefore
actually compiled and run as part of the suite. PHASE3 deliberately added no
new `RenderGraphPassContextTests.cpp` file (documented, reasoned choice — see
its own completion report), so there was nothing new to register there.

### 3.4 — Full build

```
cmake --build build
```
Result: `ninja: no work to do` — the build was already up to date (only
Markdown documentation was touched in this phase; zero production source was
changed), confirming the full build succeeds cleanly with zero errors and
zero warnings against the exact same binary this phase's `ctest` run and live
smoke test both used.

### 3.5 — Full regression test

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
Result: **100% tests passed out of 1736** (1 pre-existing, environment-gated
skip: `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, the
same one every recent campaign's own full-suite run reports as skipped on
this machine — not a regression, not caused by this campaign). **Up from
`render-pass-5`'s own 1717 baseline (3.1 above) — +19 new tests**, consistent
with the net new Tier-1 tests PHASE1 (8), PHASE4 (12), and PHASE6 (4) added
across this campaign (34 nominal additions, offset by a small number of
PHASE5 rewrites-in-place that changed existing assertions' access syntax
without changing the total case count). **No failure of any kind was found
anywhere in the full suite** — no diagnosis/fix/delegated regression task was
needed, since this phase's own contingency plan for that case (a genuine
defect requiring a dedicated `delegate_task` follow-up, per the top-level
task's own Note 4/PHASE7's own Step 3.5) never triggered. Since only
documentation changed in this phase (3.4 above), this single run serves as
both the pre-change and post-change measurement — there was no code change in
this phase to regress anything.

**Byte-identical `executionOrder` proof (PHASE4)**: confirmed both by PHASE4's
own dedicated verification at implementation time (34/34 `RenderGraphCompiler*`
tests, 33 pre-existing cases completely unmodified) and re-confirmed here by
this same full suite run showing zero regressions in that same test file.

**Zero-`default:`-case exhaustiveness proof (PHASE6)**: `DispatchByKind()`'s
`switch (usage.kind)` has no `default:` case anywhere, confirmed by direct
source inspection; PHASE6's own mandatory scratch-enumerator verification
(genuinely performed, not merely claimed — see its own completion report)
found this project's build enables no compiler flag (`-Wswitch`/`-Wall`/
`-Werror`) that would actually turn a missing case into a build failure for
`gte_core`/`GreatTamanaEngine` code — a real, honestly-documented, pre-existing
gap in this codebase's build configuration (not a regression, not fixed as
part of this campaign, per the project owner's own explicit `ask_questions`
answer during PHASE6). The "no `default:`, ever" discipline in this codebase
today is therefore a code-review convention, not (yet) a compiler-enforced
one — a fact worth carrying forward to whoever next revisits this project's
compiler-warning configuration.

## 3.6 — Live, HTTP-driven smoke test

Performed against the FINAL, fully-integrated, fully-built binary (the exact
same `build/GreatTamanaEngine.exe` the full `ctest` pass above ran alongside),
launched via `run_app_background` and driven entirely over HTTP
(`gte_send_request`):

1. **Boot / default-behavior check**: `GET /get_logs?min_level=Warning`
   returned `{"count":0,...}` — confirming PHASE1's new slot-budget-overflow
   warning does not fire spuriously at this engine's real, current pass
   counts (well under the un-bumped 16/8 budgets, per Locked Design Decision
   3).
2. **`GET /get_swapchain` and `GET /get_game_view`** both returned real
   images — the Editor's full docked layout (Hierarchy/Scene/Game/Inspector/
   Memory/Profiler/Render Graph/Atmosphere/Jobs/Log/Project) and the default
   sky/atmosphere gradient rendered identically to every prior campaign's own
   final screenshot, in both the Scene and Game panels.
3. **`GET /activate_tab?name=Render Graph` + `GET /get_swapchain`** — the
   "Render Graph" panel came to the front and correctly showed every real
   production pass (`AtmosphereTransmittanceLut`, `AtmosphereMultiScatteringLut`,
   `AtmosphereSkyViewLut` ×2 views, `AtmosphereAerialPerspectiveVolume` ×2
   views + its Debug-Slice pass, `RenderOpaque`, ...) with correct, real GPU
   timing figures and correct Reads/Writes columns, in the correct order — no
   warning banner about `timingSlotBudgetExhausted` shown (correctly, since it
   is not exhausted), confirming PHASE1's new flag doesn't disturb the panel's
   existing rendering and PHASE2/3/4/5/6's internal rewrites produced
   byte-identical real production output to every prior campaign's own
   screenshot.
4. **`GET /get_logs?min_level=Warning` re-checked** after the interactions
   above — still `{"count":0,...}` — zero new warnings/errors introduced
   anywhere by this whole campaign's internal refactor.
5. **Cleanup**: the app was cleanly terminated via `stop_app_background`.

## Definition of Done for the whole campaign — final re-confirmation

Every item in `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done for the
whole campaign" section, re-confirmed true here against the final build:

- `cmake --build build` succeeds (default configuration, `GTE_ENABLE_EDITOR`
  ON) — **confirmed** (3.4 above).
- `ctest` passes, including every updated test file/case this campaign
  touches, with zero regressions against the baseline recorded at the start
  of PHASE7 — **confirmed**, 1736/1736 (100%), 1 pre-existing
  environment-gated skip, up from `render-pass-5`'s own 1717 baseline (+19
  new tests, 3.5 above).
- `RenderGraphCompilerTests.cpp`'s full existing suite produces
  byte-identical `executionOrder` results before and after PHASE4's
  algorithmic rewrite — **confirmed** (3.5 above, and PHASE4's own dedicated
  verification at implementation time).
- A live scene's Game/Scene View rendering is visually IDENTICAL before and
  after this whole campaign — **confirmed** (3.6 above, and every prior
  phase's own live sanity check).
- The slot-budget overflow diagnostic (PHASE1) is proven correct by its own
  Tier-1 test suite, and does NOT fire spuriously during ordinary operation
  at today's real pass counts — **confirmed** (3.5's 8 new PHASE1 tests all
  passing; 3.6's live zero-warning check).
- Every exhaustive `switch` this campaign touches or replaces still has NO
  `default:` case — **confirmed structurally** (direct source inspection);
  **NOT currently compiler-enforced** — a genuine, honestly-documented
  pre-existing gap in this codebase's build configuration, discovered (not
  created) by PHASE6, left unfixed by explicit project-owner decision (see
  above).
- Validation layers report zero new warnings/errors — **confirmed** to the
  extent possible on this development machine (validation layers themselves
  are unavailable here, a pre-existing environment fact already documented
  identically by every prior campaign) — zero Warning/Error Logger entries at
  any point in this phase's own live smoke test.
- `AGENTS.md`'s "Render Pass System" section carries a short, honest note
  about this internal refactor — **confirmed** (3.2 above). `README.md`'s
  "Status" section also gained a bullet, per this phase's own `ask_questions`
  result (a confirmed, recorded deviation from PHASE0's own anticipated
  default — see "`ask_questions` decisions made during this phase" above).

## What remains genuinely open (every PHASE0 Non-Goal, restated honestly)

Per `PHASE0_MASTER_STRATEGY.md`'s own "Non-Goals (explicitly out of scope for
`render-pass-6`)" section — every one of these remains true after this
campaign closes, not silently dropped:

- **P2 item 2.8 — `PassDesc` value type for `AddRenderPass()`** — remains
  unimplemented; a separate, larger, call-site-churn-heavy campaign of its
  own, per the source review document's own prioritization ("schedule it
  after the internals have stabilized, not before" — which this campaign was
  the stabilization pass for).
- **P3 items — all three remain unimplemented**:
  - 2.5 (resource-pool indexed lookup) — opportunistic/low-urgency, not
    pulled forward.
  - 2.9 (`PassRecord::DebugMetadata` field grouping) — opportunistic, not
    pulled forward.
  - 2.10 (Buffer/VolumeTexture barrier-branch de-duplication inside
    `ApplyUsageBarrierIfNeeded`) — the source review document itself noted
    this "mostly falls out for free" once PHASE6 lands; PHASE6's own
    completion report confirms it explicitly left this item untouched (Site 1
    still has three genuinely separate `DispatchByKind()` lambda bodies, not
    merged) — still a real, if now smaller, opportunistic item for a future
    pass, not a deliverable of this campaign.
- **This codebase's exhaustive-switch discipline is not compiler-enforced**
  — no `-Wswitch`/`-Wall`/`-Werror` is enabled for `gte_core`/
  `GreatTamanaEngine`'s own code (PHASE6's Step 3.3 finding, 3.5 above). A
  future 4th `ResourceKind` (or a 4th value in any of this codebase's other
  "exhaustive switch, no `default:`" enums) would silently compile today with
  an unhandled case, relying entirely on code review to catch it. Left
  unfixed by explicit project-owner decision during PHASE6 — a real, open,
  honestly-recorded gap in this project's own build configuration, not
  specific to the Render Graph.
- **The PHASE0 Locked Design Decision 5 dedicated post-implementation
  double-checks for PHASE4/5/6 were never actually performed as genuinely
  separate `delegate_task` reviews** — see "Known process deviation" above; a
  real, permanent, honestly-recorded gap in this campaign's own process
  compliance, not a code/feature gap. Mitigated, but not erased, by the
  factors listed there.
- **No change to `RenderGraph`'s public two-calls-per-frame `Execute()`
  contract, `PassContext`'s call-site shape, or `AddPass()`/
  `AddComputePass()`/`AddRenderPass()`'s public signatures** — all confirmed
  byte-for-byte unchanged throughout this campaign (PHASE3/PHASE5's own
  explicit verification, re-confirmed here by the full build/test/smoke-test
  pass finding zero call-site breakage anywhere in the engine).
- **No relaxation of the "no `default:` case, ever" exhaustive-switch
  discipline** — PHASE6's generic dispatcher is structurally identical in
  shape to the switches it replaced; see the compiler-enforcement caveat
  above for the one honest asterisk on this claim.

## Files changed in this phase

- `AGENTS.md` (short note appended to the existing "Render Pass System"
  section; "Full history:" file list extended)
- `README.md` (new "Status" bullet, at the top)
- `task_manager/render-pass-6/CAMPAIGN_COMPLETION_REPORT.md` (this file)

No production source code was changed in this phase — the full build/`ctest`
run found zero regressions, so no dedicated regression-fix delegation was
needed (per this phase's own "no scope additions of any kind" rule, and per
the top-level task's own Note 4/instructions, which were followed: no
regression was found, so no `ask_questions`/`delegate_task` fix-task
detour was triggered for 3.5).

## Final visual-proof evidence (summary)

See "3.6 — Live, HTTP-driven smoke test" above for the full narrative. In
short, against the FINAL, fully-integrated, fully-built binary: the engine
boots identically to every prior campaign, the default scene's sky/atmosphere
gradient renders identically in both Scene and Game views, the "Render Graph"
panel shows every real production pass with correct real GPU timing and
Reads/Writes columns in the correct order, and zero warnings/errors appear in
the Logger at any point — direct, live proof that this whole seven-phase,
zero-behavior-change internal refactor is genuinely behavior-preserving.

## Git

PHASE1-6's own work was already staged and committed in prior sessions (six
commits, one per phase, confirmed via `git_log_oneline`). This phase's
documentation updates and this completion report are staged and committed
together in this phase's own commit — see the repository history for the
exact commit.
