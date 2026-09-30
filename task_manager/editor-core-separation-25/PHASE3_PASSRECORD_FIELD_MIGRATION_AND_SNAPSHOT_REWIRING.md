# PHASE3 — PassRecord Field Migration + Snapshot Rewiring (THE hot-swap)

Parent: `PHASE0_MASTER_STRATEGY.md` — **MUST READ FIRST**. Also read
`PHASE1_CORE_DEBUG_METADATA_INTERFACES.md`, `PHASE2_EDITOR_FRAME_DEBUGGER_PASS_METADATA_RECORDER.md`,
and BOTH of their completion reports before starting.

**This is the single heaviest, highest-risk phase in this campaign.**
Locked Decision 8 (`PHASE0`) explicitly flags this phase as the most
likely candidate for a `position: "next"` `delegate_task` self-double-check
before writing its own completion report — strongly consider using it here,
after finishing the implementation below but before writing
`PHASE3_COMPLETION_REPORT.md`.

## Step 1: The Goal

In ONE sitting (these are not independently buildable — see
`PHASE0_MASTER_STRATEGY.md`, Section 3.2):

1. Remove `category`/`drawKind`/`tags` from `PassRecord`
   (`RenderGraphTypes.h`). `PassRecord::kind`/`::viewScope` are NOT touched
   in type/position — only their neighbors move.
2. Widen `ViewScope`'s underlying type to `std::uint8_t` (TR6, folded in
   here per `PHASE0`'s Locked Decision 3).
3. Give `RenderGraphBuilder` a `SetDebugMetadataSink(IPassDebugMetadataSink*)`
   setter, and rewrite `AddRenderPass()` to call the installed sink instead
   of stamping the three removed fields onto `PassRecord`.
4. Give `BuildRenderGraphSnapshot()`/`BuildPassSnapshot()` a new,
   trailing, DEFAULTED parameter — a metadata-lookup callable, mirroring
   `statsLookup`'s own existing shape exactly — and rewire them to read
   `category`/`drawKind`/`tags` from THAT instead of `PassRecord`.
5. Rewrite the 4 existing `RenderGraphSnapshotTests.cpp` tests that
   currently rely on `PassRecord` still carrying these fields, add a small,
   test-local, dual-interface fake sink, and add new regression coverage
   for the sink-wiring behavior itself (both in `RenderGraphBuilderTests.cpp`
   and `RenderGraphSnapshotTests.cpp`).

When this phase is done, the WHOLE engine still compiles, every existing
test still passes (rewritten where required), and `sizeof(PassRecord)` is
measurably smaller than before this phase.

## Step 2: The Situation

- **Confirmed by direct, fresh grep of the entire `src/` tree** (done
  during this campaign's own strategy-writing, re-confirm it still holds
  before you start): the ONLY two places that read or write
  `PassRecord::category`/`::drawKind`/`::tags` **directly on a
  `PassRecord`** are:
  - `RenderGraphBuilder::AddRenderPass()` (`RenderGraphBuilder.h`, inside the
    4-line stamping block `m_passes.back().category = category; .drawKind =
    ...; .renderPassEvent = ...; .tags = ...;` — only 3 of those 4
    assignments are this document's concern, ~lines 534-537) — the WRITE side.
  - `BuildPassSnapshot()` (`RenderGraphSnapshot.cpp`, ~lines 94, 95, 98,
    `snapshot.category = pass.category;` etc., where `pass` is a
    `const PassRecord&`) — the READ side.
  Every OTHER file that appears to read `.category`/`.drawKind`/`.tags`
  (`FrameDebuggerData.cpp`, `FrameDebuggerCoverageChecker.cpp`,
  `RenderGraphMetadata.cpp`) operates on a `RenderGraphPassSnapshot`/
  `RenderGraphPassMetadata` value, **never** `PassRecord` — confirmed by
  checking each call site's own local variable's declared type. **Do not
  touch these three files in this phase** — if your own re-grep finds a
  DIFFERENT direct `PassRecord::category`/`::drawKind`/`::tags` reader
  than the two listed above, STOP and use `ask_questions` before
  proceeding; that means this campaign's own understanding of the codebase
  has a gap that must be resolved before continuing.
- `RenderGraphTypes.h`'s `PassRecord` struct (~line 717) has `category`
  at ~line 793, `drawKind` at ~line 802, `tags` at ~line 858 — all three
  documented as "read by NOTHING in RenderGraph.cpp/RenderGraphCompiler.cpp/
  RenderGraphBarrierPlanner.cpp". `ViewScope` (the enum, ~line 355) is
  currently `enum class ViewScope { Shared, GameView, SceneView };` with
  no explicit underlying type (defaults to `int`).
- `RenderGraphBuilder.h`'s `AddRenderPass()` (the 9-argument overload,
  ~line 525, and the 7-argument convenience overload ~line 556) both
  eventually reach the 4-line stamping block at ~534-537 (`category`/
  `drawKind`/`renderPassEvent`/`tags`, in that order — `renderPassEvent`
  is NOT part of this migration and stays a direct `PassRecord` assignment,
  see Step 3.2 below). Neither
  overload's PUBLIC signature changes in this phase — `category`/
  `drawKind`/`tags` are still accepted as parameters exactly as today
  (FR1/FR2 of the source document — the goal is to stop STORING them on
  `PassRecord`, not to stop ACCEPTING them at declaration time).
- `RenderGraphSnapshot.h`'s `BuildRenderGraphSnapshot()` (~line 224)
  currently takes `(compiled, input, statsLookup, timingSlotBudgetExhausted = false)`.
  `RenderGraphSnapshot.cpp`'s internal, anonymous-namespace
  `BuildPassSnapshot(const PassRecord& pass, const CompiledGraphInput& input, bool isCulled, const std::function<PassGpuStats(const char*)>& statsLookup)`
  (~line 87) is called from two loops inside `BuildRenderGraphSnapshot()`
  (~lines 134-140 for surviving passes, keyed by `handle.index`; ~lines
  144-149 for culled passes, iterated directly over `input.passes` with
  NO index tracking today).
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` has 4 tests
  that must be rewritten (confirmed exact current bodies by direct read):
  `DrawKindIsCopiedThroughForSurvivingPass` (~line 445),
  `DrawKindIsCopiedThroughForCulledPass` (~line 471),
  `TagsIsCopiedThroughForSurvivingPass` (~line 504),
  `TagsIsCopiedThroughForCulledPass` (~line 530). All 4 call
  `builder.AddRenderPass(...)` then `builder.Finish()` then
  `BuildRenderGraphSnapshot(compiled, input, {})` with NO sink installed —
  after this phase, that exact call pattern would leave
  `category`/`drawKind`/`tags` at their own struct defaults forever
  (since nothing stores/looks anything up), so all 4 MUST be updated to
  install a fake sink and pass a metadata-lookup callable, per Step 3.5
  below.
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` has NO test
  today that asserts on `category`/`drawKind`/`tags` at all (confirmed
  grep) — this phase ADDS new coverage there instead (Step 3.6), since
  `AddRenderPass()`'s own sink-wiring behavior (calls the sink exactly
  once, with the exact right `declarationIndexThisFrame`; never calls it
  when no sink is installed; `AddPass()`/`AddComputePass()` never call it
  at all) is brand-new behavior with no prior test to "fix", only new
  ground to cover (`AGENTS.md`'s own "every phase must add a Tier-1 test
  wherever the underlying problem allows it" rule).

## Step 3: The Plan

### 3.1 — `RenderGraphTypes.h`: `PassRecord` field removal + `ViewScope` widening

`ViewScope` (~line 355) — change from:

```cpp
enum class ViewScope {
    Shared,
    GameView,
    SceneView,
};
```

to:

```cpp
enum class ViewScope : std::uint8_t {
    Shared,
    GameView,
    SceneView,
};
```

(Purely mechanical, zero-risk, additive — no existing comparison or
arithmetic anywhere in the engine depends on this enum's size or
signedness, per the source document's own TR6. Update this enum's own doc
comment to note the widening, mirroring how `PassKind`/`RenderPassCategory`/
`RenderPassDrawKind`'s own doc comments already read.)

`PassRecord` (~line 717) — DELETE these 3 fields entirely (and their doc
comments — do not leave orphaned comments referring to fields that no
longer exist):

```cpp
    RenderPassCategory category = RenderPassCategory::General;      // DELETE
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;      // DELETE
    RenderPassTagMask tags = 0;                                      // DELETE
```

`PassRecord::kind` and `PassRecord::viewScope` stay EXACTLY as they are —
do not move, rename, retype, or reorder either one. Add ONE short doc
comment immediately where `category`/`drawKind`/`tags` used to sit,
explaining they moved to the sink (mirrors how `GpuMemoryTracker.h` itself
documents "name/string data now lives on the Editor side" at the point
where such a field used to be) — this is important context for the next
person reading this struct, not optional decoration.

`#include "RenderGraphDebugMetadataSink.h"` is NOT needed in
`RenderGraphTypes.h` itself — `PassRecord` no longer references
`PassDebugMetadata`/`IPassDebugMetadataSink` at all; only
`RenderGraphBuilder.h` (Step 3.2) and `RenderGraphSnapshot.h`/`.cpp`
(Step 3.3) need that include.

### 3.2 — `RenderGraphBuilder.h`: sink wiring

Add, as a new public method (near `Finish()`, or immediately after the
class's other setters if any exist — pick a spot that reads naturally,
this is a style choice, not a correctness one) and a new private member:

```cpp
    // editor-core-separation-25 campaign - optional, nullable, zero-cost-
    // when-absent. Forwarded into this builder by RenderGraph::Execute()'s
    // own template body (PHASE4), immediately after constructing a fresh
    // RenderGraphBuilder, before that call's own build(builder) callback
    // runs. A test may also call this directly (see RenderGraphSnapshotTests.cpp's
    // own updated tests, PHASE3) - RenderGraphBuilder itself has no
    // opinion about who installs this or how often; it is a plain,
    // unconditional setter.
    void SetDebugMetadataSink(IPassDebugMetadataSink* sink) noexcept { m_debugMetadataSink = sink; }
```

```cpp
private:
    ...
    IPassDebugMetadataSink* m_debugMetadataSink = nullptr;
```

Add `#include "RenderGraphDebugMetadataSink.h"` to this file's own include
list (alongside the existing `#include "RenderGraphTypes.h"`).

Inside `AddRenderPass()` (the 9-argument overload, ~line 525) — replace
the 4-line stamping block:

```cpp
        m_passes.back().category = category;
        m_passes.back().drawKind = drawKind; // Frame Debugger Pass-Ownership campaign (render-pass-2), PHASE1
        m_passes.back().renderPassEvent = renderPassEvent; // render-pass-3 campaign, PHASE1
        m_passes.back().tags = tags; // render-pass-7 campaign, PHASE1
```

with (note: `renderPassEvent` is UNCHANGED and still stamped directly onto
`PassRecord` — it is real, load-bearing ordering input read by
`RenderGraphCompiler::Compile()`, explicitly NOT part of this migration,
see the source document's own Section 1):

```cpp
        m_passes.back().renderPassEvent = renderPassEvent; // render-pass-3 campaign, PHASE1 - UNCHANGED, still real ordering input, not migrated
        // editor-core-separation-25 campaign - category/drawKind/tags no
        // longer stored on PassRecord (see RenderGraphTypes.h's own
        // PassRecord doc comment) - forwarded to the installed sink
        // instead, exactly once per declared pass, only if a sink is
        // actually installed (a headless/Player build's builder never has
        // one - this is the ONE branch that build pays for this feature).
        if (m_debugMetadataSink != nullptr) {
            m_debugMetadataSink->OnPassDeclared(m_passes.size() - 1, category, drawKind, tags);
        }
```

The 7-argument convenience overload (~line 556) needs NO changes — it
already forwards into the 9-argument overload above and never touched
`PassRecord` fields directly itself.

`AddPass()`/`AddComputePass()` (~lines 408, 452, 428, 463) — **NO CHANGES
AT ALL**. Confirm this by re-reading them after you're done — if you find
yourself wanting to add a sink call to either one, STOP: that would be
exactly the mistake the source document's own Section 2 warns against
(double-firing the sink for a pass declared via `AddRenderPass()`, which
calls into these two internally first).

### 3.3 — `RenderGraphSnapshot.h`/`.cpp`: metadata-lookup rewiring

`RenderGraphSnapshot.h` — add `#include "RenderGraphDebugMetadataSink.h"`,
then change `BuildRenderGraphSnapshot()`'s declaration from:

```cpp
RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph& compiled, const CompiledGraphInput& input,
    const std::function<PassGpuStats(const char*)>& statsLookup, bool timingSlotBudgetExhausted = false);
```

to (one new, trailing, DEFAULTED parameter — mirrors exactly how
`timingSlotBudgetExhausted` itself was added as a trailing defaulted
parameter by an earlier campaign, so every pre-existing 3- and
4-argument call site across the whole engine keeps compiling completely
unmodified):

```cpp
// editor-core-separation-25 campaign - NEW, trailing, DEFAULTED parameter.
// Resolves ONE declared pass's category/drawKind/tags by declarationIndex
// (the SAME index space CompiledGraphInput::passes already uses - see
// RenderGraphBuilder::AddRenderPass()'s own OnPassDeclared() call,
// PHASE3). Returns false (or is left empty/unset) to mean "no metadata
// available for this index" - BuildPassSnapshot() leaves
// RenderGraphPassSnapshot::category/::drawKind/::tags at their own struct
// defaults in that case, exactly matching this function's pre-existing
// behavior for every call site that does not care about these 3 fields
// (including most existing tests). In production, RenderGraph::
// ExecuteCompiledGraph() (PHASE4) supplies a real lookup backed by its own
// installed IPassDebugMetadataProvider*; a test can supply any stand-in
// (e.g. a lambda closing over a small local table), which is exactly what
// keeps this function itself Tier-1-testable with no live sink/RenderGraph
// at all.
RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph& compiled, const CompiledGraphInput& input,
    const std::function<PassGpuStats(const char*)>& statsLookup, bool timingSlotBudgetExhausted = false,
    const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup = {});
```

`RenderGraphSnapshot.cpp` — change the anonymous-namespace
`BuildPassSnapshot()` from:

```cpp
RenderGraphPassSnapshot BuildPassSnapshot(const PassRecord& pass, const CompiledGraphInput& input, bool isCulled,
    const std::function<PassGpuStats(const char*)>& statsLookup)
{
    RenderGraphPassSnapshot snapshot;
    snapshot.name = pass.name != nullptr ? pass.name : "";
    snapshot.isCulled = isCulled;
    snapshot.kind = pass.kind;
    snapshot.category = pass.category;
    snapshot.drawKind = pass.drawKind;
    snapshot.viewScope = pass.viewScope;
    snapshot.renderPassEvent = pass.renderPassEvent;
    snapshot.tags = pass.tags;
    ...
```

to (note: `declarationIndex` is a NEW parameter this function needs —
`kind`/`viewScope`/`renderPassEvent` are STILL read directly off
`PassRecord`, completely unchanged, per the source document's own Section
2/Section 5):

```cpp
RenderGraphPassSnapshot BuildPassSnapshot(const PassRecord& pass, std::size_t declarationIndex,
    const CompiledGraphInput& input, bool isCulled, const std::function<PassGpuStats(const char*)>& statsLookup,
    const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup)
{
    RenderGraphPassSnapshot snapshot;
    snapshot.name = pass.name != nullptr ? pass.name : "";
    snapshot.isCulled = isCulled;
    snapshot.kind = pass.kind; // UNCHANGED - still read directly off PassRecord, see source doc Section 2
    snapshot.viewScope = pass.viewScope; // UNCHANGED - same reason
    snapshot.renderPassEvent = pass.renderPassEvent; // UNCHANGED - same reason

    // editor-core-separation-25 campaign - category/drawKind/tags no
    // longer live on PassRecord. snapshot.category/.drawKind/.tags start
    // at RenderGraphPassSnapshot's own struct defaults (General/DrawMesh/0)
    // and are only overwritten when metadataLookup is supplied AND
    // actually has an entry for this exact declarationIndex - this is the
    // graceful, zero-behavior-change fallback for a headless build (no
    // sink ever installed) and for every pre-existing test/call site that
    // does not pass a metadataLookup at all.
    if (metadataLookup) {
        PassDebugMetadata metadata;
        if (metadataLookup(declarationIndex, metadata)) {
            snapshot.category = metadata.category;
            snapshot.drawKind = metadata.drawKind;
            snapshot.tags = metadata.tags;
        }
    }
    ...
```

(everything after this point — `readNames`/`readKinds`/`writeNames`/
`writeKinds`/`stats` — is UNCHANGED, do not touch it).

`BuildRenderGraphSnapshot()` itself — thread `declarationIndex` and
`metadataLookup` into both call sites:

```cpp
RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph& compiled, const CompiledGraphInput& input,
    const std::function<PassGpuStats(const char*)>& statsLookup, bool timingSlotBudgetExhausted,
    const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup)
{
    RenderGraphSnapshot snapshot;
    snapshot.timingSlotBudgetExhausted = timingSlotBudgetExhausted;
    snapshot.passesInExecutionOrder.reserve(compiled.executionOrder.size() + input.passes.size());

    // Surviving passes first, in real execution order. handle.index IS
    // this pass's own declarationIndex - the SAME index space
    // CompiledGraphInput::passes already uses (PassHandle::index), so no
    // new correlation problem is introduced.
    for (const PassHandle& handle : compiled.executionOrder) {
        if (handle.index >= input.passes.size()) {
            continue;
        }
        snapshot.passesInExecutionOrder.push_back(BuildPassSnapshot(
            input.passes[handle.index], handle.index, input, /*isCulled=*/false, statsLookup, metadataLookup));
    }

    // Culled passes appended afterwards, in their original declaration
    // order - the loop index IS this pass's own declarationIndex (this is
    // a NEW explicit index loop, replacing the old range-for that had no
    // index to give BuildPassSnapshot()).
    for (std::size_t i = 0; i < input.passes.size(); ++i) {
        const PassRecord& pass = input.passes[i];
        if (!pass.isCulled) {
            continue;
        }
        snapshot.passesInExecutionOrder.push_back(
            BuildPassSnapshot(pass, i, input, /*isCulled=*/true, statsLookup, metadataLookup));
    }

    // ... everything below (resources loop) is UNCHANGED, do not touch it.
```

### 3.4 — Re-audit: any OTHER `BuildRenderGraphSnapshot()`/`BuildPassSnapshot()` call site?

`BuildPassSnapshot()` is `static`/anonymous-namespace, so it has exactly
the two call sites shown above and no others — confirm this with a fresh
grep before moving on. `BuildRenderGraphSnapshot()` itself has exactly one
production call site (`RenderGraph.cpp`'s `ExecuteCompiledGraph()`,
~line 789 — **do not touch this call site in this phase**, it is PHASE4's
job specifically, since it needs `RenderGraph`'s own new
`m_debugMetadataProvider` member which does not exist until PHASE4) plus
every existing test call site across `RenderGraphSnapshotTests.cpp` (15+
of them per that file's own header comment) — confirm every one of those
still compiles with the new trailing defaulted parameter left unsupplied
(it should, by construction — a defaulted parameter never breaks an
existing shorter call), except the 4 that must be rewritten (Step 3.5).

### 3.5 — Rewrite the 4 affected tests in `RenderGraphSnapshotTests.cpp`

Add, once, near the top of the file (in the file's own existing anonymous
namespace, or a new one immediately after the existing includes — match
whatever this file's own existing helper-declaration style already is,
e.g. `NoOpExecute`/`MakeTextureDesc()`):

```cpp
// editor-core-separation-25 campaign - a minimal, test-local, dual-
// interface fake sink, mirroring FrameDebuggerPassMetadataRecorder's own
// contract exactly (PHASE2) but kept LOCAL to this test file rather than
// `#include`-ing src/Editor/FrameDebuggerPassMetadataRecorder.h from a
// Renderer/RenderGraph-tier test file - this keeps the same one-way
// gte_core -> gte_editor layering this whole campaign exists to preserve
// visible at the TEST level too (note: GreatTamanaEngineTests, the one
// unified test binary, already links gte_editor for unrelated reasons -
// see tests/CMakeLists.txt - so this is a source-layering/clarity choice,
// never a real link-time requirement).
class FakeMetadataSink : public rg::IPassDebugMetadataSink, public rg::IPassDebugMetadataProvider {
public:
    void OnPassDeclared(std::size_t declarationIndexThisFrame, rg::RenderPassCategory category,
        rg::RenderPassDrawKind drawKind, rg::RenderPassTagMask tags) override
    {
        ASSERT_EQ(declarationIndexThisFrame, m_table.size());
        m_table.push_back(rg::PassDebugMetadata{ category, drawKind, tags });
    }

    void BeginFrame() override { m_table.clear(); }

    bool QueryPassDebugMetadata(std::size_t declarationIndex, rg::PassDebugMetadata& outMetadata) const override
    {
        if (declarationIndex >= m_table.size()) {
            return false;
        }
        outMetadata = m_table[declarationIndex];
        return true;
    }

    // Bind-able directly as BuildRenderGraphSnapshot()'s own metadataLookup
    // parameter.
    std::function<bool(std::size_t, rg::PassDebugMetadata&)> AsLookup()
    {
        return [this](std::size_t declarationIndex, rg::PassDebugMetadata& out) {
            return QueryPassDebugMetadata(declarationIndex, out);
        };
    }

private:
    std::vector<rg::PassDebugMetadata> m_table;
};
```

Then, in EACH of the 4 tests (`DrawKindIsCopiedThroughForSurvivingPass`,
`DrawKindIsCopiedThroughForCulledPass`, `TagsIsCopiedThroughForSurvivingPass`,
`TagsIsCopiedThroughForCulledPass`):

1. Construct a `FakeMetadataSink sink;` right after `RenderGraphBuilder builder;`.
2. Call `builder.SetDebugMetadataSink(&sink);` immediately after
   constructing the builder (mirrors what `RenderGraph::Execute()` will do
   in production, PHASE4 — the builder must have the sink installed
   BEFORE any `AddRenderPass()` call, since that is the only place
   `OnPassDeclared()` ever fires).
3. Leave every existing `builder.AddRenderPass(...)` call in each test
   COMPLETELY UNCHANGED — the sink observes it passively via
   `AddRenderPass()`'s own new call, per Step 3.2.
4. **REQUIRED, NOT OPTIONAL, exception for `DrawKindIsCopiedThroughForCulledPass`
   and `TagsIsCopiedThroughForCulledPass` ONLY**: both of these two tests'
   own current bodies declare their "keep-alive" pass via a PLAIN
   `builder.AddPass("Survivor", [&](...){ pass.WriteColorAttachment(output); }, NoOpExecute);`
   call, THEN the actual pass-under-test via `builder.AddRenderPass(...)`.
   Once a sink is installed on this builder, this exact call ORDER breaks
   the sink's own load-bearing invariant and MUST be changed — do not
   leave it as-is:
   `OnPassDeclared()` is called with `declarationIndexThisFrame ==
   m_passes.size() - 1` — the pass's ABSOLUTE position among EVERY pass
   this builder has declared so far, sink-observed or not (see
   `RenderGraphBuilder::AddRenderPass()`'s own new call, Step 3.2) — while
   `FakeMetadataSink`/`FrameDebuggerPassMetadataRecorder` alike only ever
   `push_back()` an entry when `OnPassDeclared()` actually fires (i.e.
   `AddPass()`/`AddComputePass()` are silently invisible to it, per Locked
   Decision 4). The plain `AddPass("Survivor", ...)` call above declares
   `m_passes[0]` without ever touching the sink, so when the very next
   call, `AddRenderPass("Culled...Pass", ...)`, fires
   `OnPassDeclared(1, ...)`, the sink's own table is still at size 0 —
   `assert(declarationIndexThisFrame == m_table.size())` (`1 == 0`) FAILS
   immediately (and `FakeMetadataSink`'s own `ASSERT_EQ` mirrors this
   exact assert, so the test itself fails, not just a debug-build crash).
   **The fix**: change ONLY the "Survivor" pass's own declaration, in
   these two tests, from `builder.AddPass("Survivor", ...)` to
   `builder.AddRenderPass("Survivor", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(output); }, NoOpExecute);`
   (the 4-argument convenience overload — `viewScope`/`category`/`drawKind`/
   `renderPassEvent`/`tags` all stay at their own defaults, which is
   exactly what `AddPass()` alone already produced, so this changes
   NOTHING the test actually asserts on) — this alone restores the dense,
   gap-free index space the sink requires, since now BOTH passes in the
   builder go through `AddRenderPass()` and both fire `OnPassDeclared()`
   in the same order they are declared. This mirrors PHASE0's own
   confirmed, real-world invariant (Locked Decision 4: zero production
   call site ever mixes `AddPass()`/`AddComputePass()` with
   `AddRenderPass()` in the same builder) — it is not a new rule invented
   here, only these two tests accidentally violating it the moment a sink
   is retrofitted onto them. Every other test in this file (including the
   two SURVIVING-pass tests above, which declare only ONE pass total, and
   `ViewScopeIsCopiedThroughForSurvivingAndCulledPasses`, which never
   installs a sink at all) needs no such change.
5. Change the `BuildRenderGraphSnapshot(compiled, input, {})` call in each
   test to `BuildRenderGraphSnapshot(compiled, input, {}, false, sink.AsLookup())`.
6. Leave every existing `EXPECT_EQ(pass.drawKind, ...)`/
   `EXPECT_EQ(pass.tags, ...)`/`EXPECT_EQ(culledPass.drawKind, ...)`/
   `EXPECT_EQ(culledPass.tags, ...)` assertion COMPLETELY UNCHANGED — they
   should now pass again, sourced from the fake sink's own table instead
   of `PassRecord`.

Additionally, add ONE brand-new test proving the acceptance-criteria item
about multiple sequential declaration cycles sharing one sink (source
document, Section 8, the "single `SetDebugMetadataSink()` call is
sufficient for every later call" checkbox) — since there is no
`RenderGraphTests.cpp`/live `RenderGraph` object to test this against
directly (`PHASE0`'s own Situation notes), prove it at THIS layer instead,
which is fully sufficient to cover the real risk (index-reset correctness
across a `BeginFrame()` boundary):

```cpp
// editor-core-separation-25 campaign - proves one shared sink correctly
// resets its own declaration-index space across two independent
// "declare a graph, BeginFrame(), declare a DIFFERENT graph" cycles - the
// exact sequence RenderGraph::Execute() drives in production (PHASE4),
// simulated here at the builder/snapshot layer (no live RenderGraph/
// VkDevice needed).
TEST(RenderGraphSnapshotTest, SharedSinkAcrossTwoDeclarationCyclesNeverLeaksStaleEntries)
{
    FakeMetadataSink sink;

    // --- Cycle 1: one pass, DrawQuad / tag 0x1.
    {
        sink.BeginFrame();
        RenderGraphBuilder builder;
        builder.SetDebugMetadataSink(&sink);
        const TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());
        builder.AddRenderPass(
            "CycleOnePass", PassKind::Graphics, ViewScope::GameView, RenderPassCategory::General,
            [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(output); }, NoOpExecute,
            RenderPassDrawKind::DrawQuad, RenderPassEvent::Opaques, RenderPassTagMask{ 0x1u });

        CompiledGraphInput input = builder.Finish();
        const TextureHandle finalOutputs[] = { output };
        const CompiledGraph compiled = Compile(input, finalOutputs);
        const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {}, false, sink.AsLookup());

        ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 1u);
        EXPECT_EQ(snapshot.passesInExecutionOrder[0].drawKind, RenderPassDrawKind::DrawQuad);
        EXPECT_EQ(snapshot.passesInExecutionOrder[0].tags, RenderPassTagMask{ 0x1u });
    }

    // --- Cycle 2: a DIFFERENT single pass, DrawMesh / tag 0x2 - must NOT
    // see cycle 1's stale DrawQuad/0x1 entry, even though it also declares
    // exactly one pass at the same declarationIndex (0).
    {
        sink.BeginFrame();
        RenderGraphBuilder builder;
        builder.SetDebugMetadataSink(&sink);
        const TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());
        builder.AddRenderPass(
            "CycleTwoPass", PassKind::Graphics, ViewScope::GameView, RenderPassCategory::General,
            [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(output); }, NoOpExecute,
            RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques, RenderPassTagMask{ 0x2u });

        CompiledGraphInput input = builder.Finish();
        const TextureHandle finalOutputs[] = { output };
        const CompiledGraph compiled = Compile(input, finalOutputs);
        const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {}, false, sink.AsLookup());

        ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 1u);
        EXPECT_EQ(snapshot.passesInExecutionOrder[0].drawKind, RenderPassDrawKind::DrawMesh);
        EXPECT_EQ(snapshot.passesInExecutionOrder[0].tags, RenderPassTagMask{ 0x2u });
    }
}
```

(Adjust exact helper names/signatures — `NoOpExecute`, `MakeTextureDesc()`
— to match whatever this file's own existing helpers are actually called;
re-read the top of `RenderGraphSnapshotTests.cpp` before writing this, do
not guess.)

### 3.6 — New coverage in `RenderGraphBuilderTests.cpp`

Add a small, new, dedicated test group proving `AddRenderPass()`'s own new
sink-wiring behavior (there is no pre-existing test to "fix" here — this
is brand-new ground, per `AGENTS.md`'s testing rule):

1. `AddRenderPassCallsSinkExactlyOnceWithCorrectDeclarationIndex` — install
   a small local fake sink (reuse the same shape as `FakeMetadataSink`
   above, or a trivial counting stub — your choice, whichever reads more
   naturally in THIS file's own existing style), declare 2 passes via
   `AddRenderPass()`, confirm the sink recorded exactly 2 calls, at
   declaration indices 0 and 1, with the exact category/drawKind/tags
   values passed.
2. `AddRenderPassNeverCallsSinkWhenNoneInstalled` — declare a pass via
   `AddRenderPass()` on a builder that never called
   `SetDebugMetadataSink()` at all; confirm this does not crash (the
   null-pointer-guarded branch) — a simple "it compiles and runs to
   completion" proof is sufficient here, there is nothing else to assert.
3. `AddPassAndAddComputePassNeverCallTheSink` — install a fake sink, call
   `builder.SetDebugMetadataSink(&sink)`, then declare passes via PLAIN
   `AddPass()`/`AddComputePass()` (never `AddRenderPass()`); confirm the
   sink recorded ZERO calls — this is the direct, regression-proof
   confirmation of the source document's own Section 2 scope boundary
   (`AddPass()`/`AddComputePass()` are permanently out of scope for this
   whole campaign).

### 3.7 — CMake registration

No NEW files in this phase — `RenderGraphSnapshotTests.cpp`/
`RenderGraphBuilderTests.cpp` are already registered. Confirm
`RenderGraphDebugMetadataSink.h`'s own include is reachable from both test
files (it is — both already sit under `tests/Renderer/RenderGraph/`, same
include-path root as the production headers).

### 3.8 — Verification (this phase)

1. **Incremental compile check**: `cmake --build build` succeeds — this
   is the single most important gate in this whole campaign, since this
   phase touches the hottest, most-included struct in the render graph.
   If it fails, read the FIRST error only (later ones are often noise
   cascading from it), fix, rebuild — do not try to fix every error at
   once from a single wall of output.
2. **Targeted `ctest`**:
   `ctest -C Debug -R "RenderGraphSnapshotTest|RenderGraphBuilderTest" --output-on-failure`
   — confirm the 4 rewritten tests pass, the new multi-cycle test passes,
   the 3 new builder tests pass, and every OTHER pre-existing test in both
   suites still passes unmodified (this is the literal FR2 promise —
   confirm it, do not assume it).
3. Confirm, by direct code inspection, `sizeof(PassRecord)` is smaller
   than before this phase — the simplest concrete proof: temporarily add
   a `static_assert(sizeof(PassRecord) <= <some number you compute by hand
   from the struct's new field list>, "...");` in a scratch/local build,
   confirm it compiles, then remove the scratch assert before committing
   (do not leave a hardcoded magic-number `static_assert` in committed
   code — struct padding varies by compiler/platform and would make this
   assert fragile for zero ongoing benefit; the ONE-TIME manual
   confirmation during this phase is what matters, not a permanent
   compile-time guard).
4. Confirm, via `ask_questions`-free direct grep, that `AddPass()`/
   `AddComputePass()` still contain ZERO reference to
   `m_debugMetadataSink`/`OnPassDeclared()` anywhere in their bodies.
5. `git_status` to confirm exactly the expected files changed:
   `RenderGraphTypes.h`, `RenderGraphBuilder.h`, `RenderGraphSnapshot.h`,
   `RenderGraphSnapshot.cpp`, `RenderGraphSnapshotTests.cpp`,
   `RenderGraphBuilderTests.cpp` — nothing else.

**Strongly consider a `position: "next"` `delegate_task` self-double-check
here** (Locked Decision 8) before writing this phase's own completion
report — instruct it to independently re-read this whole phase's diff,
re-run the targeted `ctest` filter itself, and re-confirm points 3.8.1-3.8.5
above, reporting back inline (never its own separate report file). It
must ALSO be instructed to use `ask_questions` for any ambiguity it
personally finds.

### 3.9 — This phase's own completion report

Write `PHASE3_COMPLETION_REPORT.md` into this same folder covering: every
file changed and a summary of the diff, the exact `ctest` filter used and
its full output, the `sizeof(PassRecord)` before/after confirmation
method and result, and (if used) a summary of the `position: "next"`
self-double-check's own findings. Commit both the code change and the
report together (`git_add` + `git_commit`).

If you discover ANY ambiguity not already resolved by this file or
`PHASE0_MASTER_STRATEGY.md` — use `ask_questions` before proceeding. If you
delegate any part of this phase to a sub-task, that sub-task MUST also be
instructed to use `ask_questions` for its own ambiguities.
