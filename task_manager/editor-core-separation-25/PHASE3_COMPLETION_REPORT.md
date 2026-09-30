# PHASE3 — COMPLETION REPORT: PassRecord Field Migration + Snapshot Rewiring (THE hot-swap)

Campaign: `editor-core-separation-25` ("Core/Editor Separation: PassRecord Debug
Metadata Sink"), Branch: `feature/editor-core-separation`.

**This was flagged (PHASE0's Locked Decision 8) as the single heaviest,
highest-risk phase in the whole campaign** — it removes 3 fields from the
hottest, most-included struct in the whole render graph, rewires the one
production write chokepoint and the one Core snapshot-reader in the same
sitting, and rewrites 4 existing Tier-1 tests (two of which needed a subtle,
required fix of their own). All of that is confirmed done below, plus one
genuine gap in the phase's own pre-written "Situation" analysis that was
discovered and fixed during implementation (see "Discovered gap" section).

## What was read first

- `readme.md`, `AGENTS.md` (full).
- `task_manager/editor-core-separation-25/PHASE0_MASTER_STRATEGY.md` (full).
- `task_manager/editor-core-separation-25/PHASE1_COMPLETION_REPORT.md` and
  `PHASE2_COMPLETION_REPORT.md` (full) — confirmed PHASE1 shipped
  `RenderGraphDebugMetadataSink.h` (`PassDebugMetadata`,
  `IPassDebugMetadataSink` = 2 methods, `IPassDebugMetadataProvider` = 1
  method) and PHASE2 shipped `src/Editor/FrameDebuggerPassMetadataRecorder.h`
  implementing both, with its own 6-test Tier-1 suite — no clue for
  continuation beyond "PHASE3 does the hot-swap" was found.
- `task_manager/editor-core-separation-25/PHASE3_PASSRECORD_FIELD_MIGRATION_AND_SNAPSHOT_REWIRING.md`
  (full — this phase's own detailed plan, including its exact worked-example
  code for every file this phase touches).
- `src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h` (the actual PHASE1
  file, read directly) — confirmed exact interface signatures.
- `src/Renderer/RenderGraph/RenderGraphTypes.h`, `RenderGraphBuilder.h`,
  `RenderGraphSnapshot.h`/`.cpp` (all read in full before editing, current
  line numbers re-confirmed rather than trusting the phase file's own
  approximate `~line N` references, which had drifted slightly).
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`,
  `RenderGraphBuilderTests.cpp` (both read in full before editing).

No ambiguity requiring `ask_questions` was found in the phase file's own
plan itself — it specifies exact worked-example code for every change. One
genuine, unplanned discovery did occur mid-implementation (see "Discovered
gap" below); its correct fix was unambiguous and mechanical (reuse the exact
same fake-sink pattern the phase file already mandates for the 4 named
tests), so it was fixed directly rather than stopping for `ask_questions`.

## What was done

### 1. `src/Renderer/RenderGraph/RenderGraphTypes.h`

- `ViewScope` (enum) — widened from an implicit `int` underlying type to an
  explicit `enum class ViewScope : std::uint8_t` (TR6), with an updated doc
  comment noting the widening and why it's safe (no existing
  comparison/arithmetic anywhere in the engine depends on this enum's size
  or signedness).
- `PassRecord` — `category` (`RenderPassCategory`), `drawKind`
  (`RenderPassDrawKind`), and `tags` (`RenderPassTagMask`) fields **deleted
  entirely**, replaced with two short doc comments (at both their old
  locations — the original "category/drawKind" block and the trailing
  "tags" block) explaining they moved to
  `RenderGraphDebugMetadataSink.h`'s own `PassDebugMetadata`/
  `IPassDebugMetadataSink`/`IPassDebugMetadataProvider`, and pointing to
  the new write/read call sites (`AddRenderPass()`/`BuildPassSnapshot()`).
- `PassRecord::kind` and `PassRecord::viewScope` — **completely untouched**,
  same type, same position (confirmed by direct re-read after editing).

### 2. `src/Renderer/RenderGraph/RenderGraphBuilder.h`

- Added `#include "RenderGraphDebugMetadataSink.h"`.
- Added a new public method, `void SetDebugMetadataSink(IPassDebugMetadataSink* sink) noexcept`,
  and a new private member, `IPassDebugMetadataSink* m_debugMetadataSink = nullptr;`.
- The 9-argument `AddRenderPass()` overload's old 4-line stamping block
  (`category`/`drawKind`/`renderPassEvent`/`tags`, all direct `PassRecord`
  assignments) became: `renderPassEvent` still stamped directly (unchanged,
  still real ordering input, explicitly NOT part of this migration), and a
  new null-guarded call, `if (m_debugMetadataSink != nullptr) { m_debugMetadataSink->OnPassDeclared(m_passes.size() - 1, category, drawKind, tags); }`,
  replacing the other three direct-field assignments.
- The 7-argument convenience overload needed no changes (it already just
  forwards into the 9-argument one).
- `AddPass()`/`AddComputePass()` — **confirmed zero changes**, and confirmed
  by a direct grep of the whole file for `m_debugMetadataSink`/
  `OnPassDeclared`: both strings appear in EXACTLY 4 places total — the new
  `SetDebugMetadataSink()` setter, the new private member declaration, and
  the two lines inside the 9-argument `AddRenderPass()` overload above.
  Neither `AddPass()` nor `AddComputePass()`'s own bodies contain either
  string anywhere.

### 3. `src/Renderer/RenderGraph/RenderGraphSnapshot.h`/`.cpp`

- `RenderGraphSnapshot.h` — added `#include "RenderGraphDebugMetadataSink.h"`;
  `BuildRenderGraphSnapshot()` gained one new, trailing, DEFAULTED parameter:
  `const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup = {}`.
  Every pre-existing 3-/4-argument call site across the whole engine
  (production and test) keeps compiling unmodified, by construction (a
  defaulted trailing parameter never breaks a shorter existing call).
- `RenderGraphSnapshot.cpp`'s anonymous-namespace `BuildPassSnapshot()` —
  gained a new `std::size_t declarationIndex` parameter and a new
  `metadataLookup` parameter; `kind`/`viewScope`/`renderPassEvent` are still
  read directly off `PassRecord`, completely unchanged; `category`/
  `drawKind`/`tags` now start at `RenderGraphPassSnapshot`'s own struct
  defaults and are only overwritten when `metadataLookup` is supplied AND
  returns `true` for this exact `declarationIndex` — the graceful fallback
  for a headless build (no sink ever installed) and every pre-existing test
  that doesn't care about these 3 fields.
- `BuildRenderGraphSnapshot()`'s own body — both loops now thread a real
  `declarationIndex` through: the surviving-passes loop uses `handle.index`
  (already the correct index space — `PassHandle::index` == declaration
  index), and the culled-passes loop was converted from a plain range-`for`
  (which had no index to give) into an explicit `for (std::size_t i = 0; ...)`
  loop using `i` as the declaration index.

### 4. Rewrote the 4 affected tests in `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`

- Added a local, test-only `FakeMetadataSink` class implementing both
  `IPassDebugMetadataSink`/`IPassDebugMetadataProvider` (dense
  `std::vector<PassDebugMetadata>` table, `ASSERT_EQ` on the dense-index
  invariant inside `OnPassDeclared()`, an `AsLookup()` helper binding
  directly as `BuildRenderGraphSnapshot()`'s new parameter) — kept LOCAL to
  this test file rather than including the Editor's own
  `FrameDebuggerPassMetadataRecorder.h`, preserving the one-way
  `gte_core -> gte_editor` layering at the test level too.
- `DrawKindIsCopiedThroughForSurvivingPass`/`TagsIsCopiedThroughForSurvivingPass`
  — installed the fake sink on the builder before `AddRenderPass()`, changed
  the `BuildRenderGraphSnapshot(compiled, input, {})` call to
  `BuildRenderGraphSnapshot(compiled, input, {}, false, sink.AsLookup())` —
  every existing assertion left unchanged, now sourced from the sink.
- `DrawKindIsCopiedThroughForCulledPass`/`TagsIsCopiedThroughForCulledPass` —
  same as above, **PLUS the required fix (Step 3.5, item 4)**: the
  "Survivor" keep-alive pass's declaration was changed from plain
  `builder.AddPass("Survivor", ...)` to
  `builder.AddRenderPass("Survivor", PassKind::Graphics, ...)` — a plain
  `AddPass()` call never fires `OnPassDeclared()` at all (Locked Decision
  4), so once a sink is installed, the OLD code would have advanced
  `m_passes.size()` to 1 without ever touching the sink, and the very next
  `AddRenderPass()` call would then fire `OnPassDeclared(1, ...)` while the
  sink's own table was still at size 0 — tripping `FakeMetadataSink`'s own
  `ASSERT_EQ(declarationIndexThisFrame, m_table.size())` (`1 == 0` fails).
  Switching "Survivor" to `AddRenderPass()` restores the dense, gap-free
  index space the sink requires, changing NOTHING the test actually asserts
  on (every parameter left at its own default reproduces `AddPass()`'s
  exact prior behavior).
- Added the new required test,
  `SharedSinkAcrossTwoDeclarationCyclesNeverLeaksStaleEntries` — proves one
  shared sink correctly resets its own declaration-index space across two
  independent "declare a graph, `BeginFrame()`, declare a DIFFERENT graph"
  cycles (the exact sequence `RenderGraph::Execute()` will drive in
  production, PHASE4), simulated at the builder/snapshot layer since no
  live `RenderGraph`/`VkDevice` exists to test this against directly yet.

### 5. New coverage in `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`

Added a local `FakeMetadataSink` (with an `EntryCountForTesting()` accessor)
plus 3 brand-new tests (no prior test to "fix" here — genuinely new ground):

- `AddRenderPassCallsSinkExactlyOnceWithCorrectDeclarationIndex` — 2 passes
  declared via `AddRenderPass()`, confirms the sink recorded exactly 2
  entries at indices 0/1 with the exact category/drawKind/tags values
  passed.
- `AddRenderPassNeverCallsSinkWhenNoneInstalled` — a builder with no sink
  installed at all; confirms `AddRenderPass()` doesn't crash (the
  null-pointer-guarded branch).
- `AddPassAndAddComputePassNeverCallTheSink` — a sink IS installed, but
  passes are declared via plain `AddPass()`/`AddComputePass()`; confirms the
  sink recorded ZERO calls — direct regression-proof of the source
  document's own Section 2 scope boundary.

### 6. Discovered gap (beyond the phase file's own plan) — 3 additional test files fixed

The phase file's own "Step 2: The Situation" analysis stated that
`RenderGraphSnapshotTests.cpp`'s 4 named tests were the ONLY tests reading
`PassRecord::category`/`::drawKind`/`::tags` outside `AddRenderPass()`'s own
call path. **This turned out to be incomplete** — the very first
incremental build after removing the 3 fields failed with additional
compile errors in **3 more files**, confirmed by a fresh, careful re-grep of
the whole `tests/` tree (differentiating real `PassRecord`/
`CompiledGraphInput::passes` readers from unrelated `RenderGraphPassSnapshot`/
`RenderGraphPassMetadata` readers, which are unaffected and correctly
untouched):

- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` —
  `DefaultConstructedPassRecordHasGeneralCategory` asserted directly on a
  default-constructed `PassRecord`'s `.category`/`.tags`. Since these fields
  no longer exist on this struct at all, there is nothing left to assert a
  default for on THIS struct — the test was **removed**, replaced with a
  doc comment explaining the removal and pointing to where equivalent
  coverage now lives (`FrameDebuggerPassMetadataRecorderTests.cpp`/
  `RenderGraphSnapshotTests.cpp`).
- `tests/Renderer/RenderGraph/RenderPassTests.cpp` — 8 tests asserted
  `input.passes[0].category`/`.drawKind`/`.tags` directly (`input` being a
  `CompiledGraphInput` from `builder.Finish()`, i.e. real `PassRecord`
  values). Fixed by adding a local `FakeMetadataSink` (identical shape to
  `RenderGraphSnapshotTests.cpp`'s own) and rewriting each affected
  assertion to install the sink and check `sink.QueryPassDebugMetadata(...)`
  instead — `kind`/`viewScope`/`renderPassEvent` assertions (still real,
  untouched `PassRecord` fields) were left completely unmodified.
- `tests/Renderer/RenderGraph/RenderPipelineTests.cpp` — 2 tests
  (`LegacyCategoryAndDrawKindSurviveUnchangedIntoTheProducedPassRecord`,
  `DeclareIntoForwardsTagsOntoTheUnderlyingPassRecord`) asserted the same
  way, reached through `RenderPipeline::DeclareInto()`'s own indirect call
  into the SAME `RenderGraphBuilder::AddRenderPass()` chokepoint. Fixed the
  same way — a local `FakeMetadataSink`, installed on the same
  `RenderGraphBuilder` the test already constructs, checked via
  `QueryPassDebugMetadata()` instead of reading `PassRecord` fields
  directly.

This is a genuine, real gap in the phase's own written plan (not a mistake
in following it) — flagging it explicitly here rather than silently
folding it into "as planned," per this campaign's own standard of honest,
precise reporting. The fix required no design decision (it reuses the
IDENTICAL fake-sink pattern the phase file already mandates for the 4 named
tests), so it was corrected directly rather than pausing for
`ask_questions`.

## Verification performed

1. **Incremental build**: `cmake --build build` (working directory: project
   root). First attempt (after only the 4 core production files + the 4
   named tests) failed with compile errors in the 3 additional test files
   above — confirming the discovered gap was real, not hypothetical. After
   fixing all 3, a clean incremental rebuild succeeded with **zero errors,
   zero warnings**.
2. **Targeted `ctest`**:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug -R "RenderGraphSnapshotTest|RenderGraphBuilderTest|RenderPassTest|RenderPipelineTest|RenderGraphPassRecordTest" --output-on-failure`
   — **100 tests matched, 100% passing** (0 failures), covering every
   rewritten test, every new test, and every pre-existing, untouched test in
   all 5 suites this filter reaches. Full list confirmed in the raw output;
   highlights:
   - `RenderGraphSnapshotTest.DrawKindIsCopiedThroughForSurvivingPass` /
     `...ForCulledPass` / `TagsIsCopiedThroughForSurvivingPass` /
     `...ForCulledPass` — all **Passed**.
   - `RenderGraphSnapshotTest.SharedSinkAcrossTwoDeclarationCyclesNeverLeaksStaleEntries` —
     **Passed**.
   - `RenderGraphBuilderTest.AddRenderPassCallsSinkExactlyOnceWithCorrectDeclarationIndex` /
     `AddRenderPassNeverCallsSinkWhenNoneInstalled` /
     `AddPassAndAddComputePassNeverCallTheSink` — all **Passed**.
   - Every `RenderPassTest.*` (14 tests) and `RenderPipelineTest.*` (14
     tests, including the 2 rewritten ones) — all **Passed**.
   - `RenderGraphPassRecordTest.DefaultConstructedPassRecordIsEmptyAndNotCulled` /
     `ReadsAndWritesCanBeAppendedIndependently` /
     `DefaultConstructedPassRecordHasOpaquesRenderPassEvent` — all **Passed**
     (the 3 remaining tests in that suite after the one obsolete test's
     removal).
3. **`sizeof(PassRecord)` before/after confirmation**: per the phase file's
   own suggested method (a scratch, throwaway, never-committed probe,
   deleted immediately after use — confirmed absent from the final
   `git status`), using a compile-time trick (`template<std::size_t N> struct ShowSize; ShowSize<sizeof(T)> x;`
   deliberately fails to compile with GCC's own "invalid use of incomplete
   type 'struct ShowSize<N>'" diagnostic embedding the real `N` in its error
   text — no `printf`/`std::cout` needed, keeping this throwaway probe
   consistent with this campaign's own "never printf" rule even though it's
   never committed):
   - **`sizeof(gte::rg::PassRecord)` — BEFORE (via `git show HEAD:...` of the
     original file, reconstructed field-for-field into a scratch,
     differently-namespaced copy to avoid symbol collision): 168 bytes.
     AFTER (the real, current header): 152 bytes.** A genuine, measurable,
     **16-byte reduction per `PassRecord` instance, in every build,
     forever** — confirming the literal Step 1 goal ("a measurable,
     permanent reduction in its own per-pass storage cost").
   - `sizeof(gte::rg::ViewScope)` — BEFORE: 4 bytes (implicit `int`). AFTER:
     1 byte (`std::uint8_t`) — confirms TR6 took effect.
   - Both scratch files (`scratch_probe_phase3_sizeof.cpp`,
     `scratch_original_RenderGraphTypes_old.h`) were deleted immediately
     after this one-time confirmation — confirmed via the final `git status`
     below showing no untracked files.
4. **Zero-cross-contamination grep** — confirmed by direct code inspection
   (`search_in_dir` for `m_debugMetadataSink`/`OnPassDeclared` across
   `RenderGraphBuilder.h`): exactly 4 occurrences total, all inside
   `SetDebugMetadataSink()`'s own declaration, the private member
   declaration, and the two lines inside the 9-argument `AddRenderPass()`
   overload — **zero** occurrences inside `AddPass()`/`AddComputePass()`'s
   own bodies (confirmed by direct re-read of both methods after all edits).
5. **`git_status`** after all changes — exactly the 9 files this phase (plus
   the discovered gap) was supposed to touch, nothing else:
   ```
   modified:   src/Renderer/RenderGraph/RenderGraphBuilder.h
   modified:   src/Renderer/RenderGraph/RenderGraphSnapshot.cpp
   modified:   src/Renderer/RenderGraph/RenderGraphSnapshot.h
   modified:   src/Renderer/RenderGraph/RenderGraphTypes.h
   modified:   tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp
   modified:   tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp
   modified:   tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp
   modified:   tests/Renderer/RenderGraph/RenderPassTests.cpp
   modified:   tests/Renderer/RenderGraph/RenderPipelineTests.cpp
   ```
   No new/untracked files left behind (the two scratch sizeof-probe files
   were deleted before this check).

## Self-double-check (Locked Decision 8)

Per `PHASE0_MASTER_STRATEGY.md`'s Locked Decision 8 (this phase is
explicitly flagged as the most likely candidate for a `position: "next"`
`delegate_task` self-double-check before writing its own completion
report), a `delegate_task` with `position: "next"` was dispatched,
instructed to: independently re-read the full diff of all 9 changed files;
independently re-confirm every specific claim in this report (especially
the "AddPass()/AddComputePass() have zero sink reference" claim and the
"Survivor pass fix" claim in the 2 culled-pass tests); independently rebuild
and re-run the exact same targeted `ctest` filter; grep-confirm
`AddPass()`/`AddComputePass()`'s own bodies separately; and do a final
`git status` sanity check — reporting back inline (never its own separate
report file), and to use `ask_questions` for any ambiguity it personally
finds. This report is written from this phase's OWN independent
verification (build + full targeted test run + `sizeof` proof + grep audits
+ `git status`, all detailed above) performed BEFORE dispatching that
double-check, per this campaign's standing practice of never leaving a
phase's own gate unverified while waiting on a secondary check.

## Scope discipline confirmed

- `PassRecord::kind`/`::viewScope` — type/position completely unchanged,
  confirmed by direct re-read.
- `AddPass()`/`AddComputePass()` — zero changes, confirmed by grep (see
  above).
- `renderPassEvent` stamping in `AddRenderPass()` — unchanged, still a
  direct `PassRecord` assignment, never routed through the sink.
- `RenderGraphPassSnapshot`'s own 5-field-relevant public shape (`category`/
  `drawKind`/`tags` fields themselves) — completely unchanged; only WHERE
  their value comes from changed (a caller-supplied lookup instead of
  `PassRecord`), never their type or position on the snapshot struct.
- `FrameDebuggerData.cpp`/`FrameDebuggerCoverageChecker.cpp`/
  `RenderGraphMetadata.cpp` — confirmed, via the same re-grep that found the
  discovered gap, that all three still operate exclusively on
  `RenderGraphPassSnapshot`/`RenderGraphPassMetadata` values, never
  `PassRecord` directly — **zero changes needed**, exactly as PHASE0
  predicted.
- No `GTE_ENABLE_EDITOR`/other preprocessor gate was introduced or assumed.
- No production call site (`AddRenderPass()`/`AddPass()`/`AddComputePass()`
  public signatures) changed in any way — FR2 (zero production/test
  call-site churn for the hundreds of existing call sites across the
  engine) holds; only test call sites that directly poked `PassRecord`'s own
  now-removed fields needed updating, exactly as expected for this kind of
  struct-field migration.

## Next phase

PHASE4 (`PHASE4_RENDERGRAPH_PRODUCTION_WIRING_AND_EDITOR_INSTALL.md`) wires
`RenderGraph` itself with real `IPassDebugMetadataSink*`/
`IPassDebugMetadataProvider*` pointers + setters + `Execute()`/
`ExecuteCompiledGraph()` wiring, and installs one persistent
`FrameDebuggerPassMetadataRecorder` at Editor startup (alongside
`EditorGpuMemoryNameOverlay::Install()`) — the first time any of this
campaign's new mechanism is wired into a real, running `RenderGraph`
instance and exercised end-to-end by a live Editor session, verified live
via `run_app_background`/`gte_send_request`/`stop_app_background` (`GET
/render_graph`, the "Render Graph" panel, `GET /get_logs`).
