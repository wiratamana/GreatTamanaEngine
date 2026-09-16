# PHASE1 — COMPLETION REPORT: RenderGraph Compute-Dispatch Choke-Point Infrastructure

## Parent -> `PHASE0_MASTER_STRATEGY.md`

Status: **DONE**. Branch: `feature/frame-debugger-impl` (unchanged, no new branch created).

## Summary

`RenderGraphBuilder::AddComputePass()` is no longer a "purely cosmetic" no-op alias of `AddPass()` — it now
stamps a real, structurally-tracked `PassRecord::isComputePass = true` flag, which survives all the way
through `RenderGraphCompiler::Compile()` into `RenderGraphSnapshot::RenderGraphPassSnapshot::isComputePass`,
for both surviving and culled passes. `RenderGraphPassSnapshot` also gained two new parallel vectors,
`readKinds`/`writeKinds` (`std::vector<ResourceKind>`), so a consumer can tell a texture read/write from a
buffer one from a volume-texture one without guessing or probing multiple registries. As a required
companion fix (a pre-existing, independent bug living in the exact function this phase's own metadata
depends on), `ResourceUsageName()` was rewritten from a plain two-way `if (kind==Texture){...} else
{assume Buffer}` shape into a real, exhaustive three-way `switch` — fixing a confirmed, already-shipping bug
where every `VolumeTexture`-kind read/write (e.g. the Aerial Perspective froxel volume's own write/read)
silently resolved to an empty-string name in `readNames`/`writeNames` (and therefore in the already-shipped
"Render Graph" Editor panel too).

**Zero behavior change to actual rendering** — no barrier, no culling decision, no `vkCmdBeginRendering`/
`vkCmdDispatch` call was touched anywhere. This is pure, additive, read-only-by-everyone-else metadata plus
one bug fix confined to a display-string helper.

## Exact diff shape at each touch point

### 1. `src/Renderer/RenderGraph/RenderGraphTypes.h` — `PassRecord::isComputePass`

Added one new field directly after the existing `isCulled` field inside `struct PassRecord` (~line 409-424),
with a doc comment explaining what writes it (`RenderGraphBuilder::AddComputePass()`) and what reads it
(`RenderGraphPassSnapshot::isComputePass`, consumed by the Editor's Frame Debugger):

```cpp
bool isCulled = false;

// frame-debugger-5 campaign, PHASE1 ...
bool isComputePass = false;
```

No other field in `PassRecord` was touched.

### 2. `src/Renderer/RenderGraph/RenderGraphBuilder.h` — `AddComputePass()` sets the flag

Replaced the old body (and its now-stale "PURELY COSMETIC" doc comment) with:

```cpp
template <typename SetupFn, typename ExecuteFn>
void AddComputePass(const char* name, SetupFn&& setup, ExecuteFn&& execute)
{
    AddPass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
    m_passes.back().isComputePass = true;
}
```

The doc comment was rewritten to no longer claim "zero behavioral difference from `AddPass()`" — it now
explicitly documents this method as the one real choke point that stamps `isComputePass`. `m_passes.back()`
is safe because `AddPass()` (called first, unconditionally) always `push_back`s exactly one new `PassRecord`
before returning.

### 3. `src/Renderer/RenderGraph/RenderGraphSnapshot.h` — `RenderGraphPassSnapshot` grows three new fields

Added `bool isComputePass = false;` plus `std::vector<ResourceKind> readKinds;` /
`std::vector<ResourceKind> writeKinds;` to `struct RenderGraphPassSnapshot`, positioned between the existing
`writeNames` field and the existing `PassGpuStats stats;` field. `ResourceKind` was already visible in this
file transitively (`RenderGraphSnapshot.h` includes `RenderGraphBuilder.h` includes `RenderGraphTypes.h`,
confirmed at implementation time) — no new `#include` was required.

### 4. `src/Renderer/RenderGraph/RenderGraphSnapshot.cpp` — `BuildPassSnapshot()` copies the new fields through

```cpp
snapshot.isComputePass = pass.isComputePass; // frame-debugger-5, PHASE1

snapshot.readNames.reserve(pass.reads.size());
snapshot.readKinds.reserve(pass.reads.size());          // frame-debugger-5, PHASE1
for (const ResourceUsage& usage : pass.reads) {
    snapshot.readNames.push_back(ResourceUsageName(usage, input));
    snapshot.readKinds.push_back(usage.kind);           // frame-debugger-5, PHASE1
}
// ... symmetric for writeNames/writeKinds
```

Copied through for BOTH a surviving pass and a culled pass (the existing `if (!isCulled && statsLookup)`
guard is unchanged and still ONLY gates `stats`, exactly as before) — confirmed by the new
`CulledComputePassStillReportsIsComputePassTrueAndCorrectWriteKind` test below.

### 3.4b (companion fix, same file) — `ResourceUsageName()` rewritten to a real three-way switch

Before (pre-existing, independently-shipping bug — confirmed live before this phase touched anything):

```cpp
std::string ResourceUsageName(const ResourceUsage& usage, const CompiledGraphInput& input)
{
    if (usage.kind == ResourceKind::Texture) {
        if (usage.texture.index < input.textureNames.size()) { ... }
        return "";
    }
    if (usage.buffer.index < input.bufferNames.size()) { ... } // silently used for VolumeTexture too!
    return "";
}
```

A `VolumeTexture`-kind `ResourceUsage` always has `usage.buffer.index == kInvalidIndex`, so this always fell
through to `return ""` for a volume-texture read/write — confirmed to affect two real, currently-shipping
production passes (`AtmosphereLutRenderer.cpp`'s `AddAerialPerspectiveVolumePass()`'s
`WriteVolumeTexture()` and `AddAerialPerspectiveCompositePass()`'s `ReadVolumeTexture()`), and therefore
also the already-shipped "Render Graph" Editor panel's Reads/Writes columns for those two rows.

After:

```cpp
std::string ResourceUsageName(const ResourceUsage& usage, const CompiledGraphInput& input)
{
    std::string name;
    switch (usage.kind) {
    case ResourceKind::Texture:
        if (usage.texture.index < input.textureNames.size() && input.textureNames[usage.texture.index] != nullptr) {
            name = input.textureNames[usage.texture.index];
        }
        break;
    case ResourceKind::Buffer:
        if (usage.buffer.index < input.bufferNames.size() && input.bufferNames[usage.buffer.index] != nullptr) {
            name = input.bufferNames[usage.buffer.index];
        }
        break;
    case ResourceKind::VolumeTexture:
        if (usage.volumeTexture.index < input.volumeTextureNames.size()
            && input.volumeTextureNames[usage.volumeTexture.index] != nullptr) {
            name = input.volumeTextureNames[usage.volumeTexture.index];
        }
        break;
    }
    return name;
}
```

An exhaustive `switch` with no `default:` — mirrors `RenderGraphCompiler.cpp`/`RenderGraph.cpp`'s own
established convention, so a future fourth `ResourceKind` fails to compile here too, until this function is
updated to match. This is a pure display-string helper — never read by `RenderGraph.cpp`/
`RenderGraphCompiler.cpp` themselves — so this fix changes zero rendering behavior.

## Full list of new test names

`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` (existing test extended, per the phase document's own
preferred option — no new near-duplicate test added):
- `RenderGraphPassRecordTest.DefaultConstructedPassRecordIsEmptyAndNotCulled` — one new
  `EXPECT_FALSE(record.isComputePass);` line added to the existing test body.

`tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` (3 new tests; 1 existing test renamed, its
assertions unchanged — see below):
- `RenderGraphBuilderTest.AddPassRecordsIsComputePassFalse` (new)
- `RenderGraphBuilderTest.AddComputePassRecordsIsComputePassTrue` (new)
- `RenderGraphBuilderTest.AddComputePassWithMixedReadWriteKindsRecordsEachCorrectly` (new — texture, buffer,
  AND volume-texture read/write kinds all declared on one `AddComputePass()` call)
- `RenderGraphBuilderTest.AddComputePassStillRunsSetupAndCapturesExecuteLikeAddPass` — **renamed** from
  `AddComputePassBehavesIdenticallyToAddPass` (its own section header comment above it was also reworded),
  per the phase document's own explicitly-optional cosmetic suggestion (3.5) since "purely cosmetic alias"
  stopped being an accurate description the moment this phase landed. **No assertion inside this test was
  changed** — same body, same expectations, only the test name/section header text changed.

`tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` (4 new tests):
- `RenderGraphSnapshotTest.IsComputePassIsCopiedThroughForSurvivingGraphicsAndComputePasses`
- `RenderGraphSnapshotTest.CulledComputePassStillReportsIsComputePassTrueAndCorrectWriteKind`
- `RenderGraphSnapshotTest.ReadKindsAndWriteKindsMatchDeclaredResourceKindsIncludingVolumeTextureNames`
  (covers the general readKinds/writeKinds parallel-vector contract for a mixed texture/buffer/volume-texture
  pass, AND doubles as regression coverage for the `ResourceUsageName()` volume-texture name fix)
- `RenderGraphSnapshotTest.WriteVolumeTextureAndReadVolumeTextureProduceNonEmptyRealNames` (dedicated,
  minimal regression test explicitly requested by the phase document's Step 3.5, isolating just the
  `ResourceUsageName()` bug fix itself, independent of the broader mixed-kind test above)

**Total: 7 new `TEST()` entries + 1 extended existing test + 1 renamed-but-behaviorally-identical existing
test.**

## Explicit confirmation: no pre-existing RenderGraph test assertion needed to change

Confirmed by running the full RenderGraph-scoped `ctest` suite after landing every change above:

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure -R RenderGraph
...
100% tests passed, 0 tests failed out of 166
```

Every one of the 166 tests in this run passed, including every single pre-existing test in
`RenderGraphTypesTests.cpp`, `RenderGraphBuilderTests.cpp`, `RenderGraphSnapshotTests.cpp`,
`RenderGraphCompilerTests.cpp`, `RenderGraphBarrierPlannerTests.cpp`,
`RenderGraphDebugTextureRegistryTests.cpp`, `RenderGraphDebugVolumeTextureRegistryTests.cpp`, and
`RenderGraphNameSlotTableTests.cpp` — none of which needed any assertion changed. The only tests that are new
in this run's output relative to before this phase are the 7 new `TEST()` entries listed above (test IDs
649-652 for the new `RenderGraphSnapshotTest` cases, 726-728 for the new `RenderGraphBuilderTest` cases).

Also confirmed via full-repository grep before editing: only `src/Editor/FrameDebuggerData.cpp` and
`src/Editor/Panels/RenderGraphPanel.cpp` consume `RenderGraphPassSnapshot::readNames`/`writeNames` in
production, and both only ever join them into a display string — neither constructs a
`RenderGraphPassSnapshot`/`PassRecord` via positional/aggregate initializer syntax anywhere in the
repository (confirmed via a second grep across both `src/` and `tests/`), so adding new trailing fields to
both structs is guaranteed non-breaking for every existing call site.

## Fast compile check (as run this phase)

```
cmake --build build --target GreatTamanaEngineTests
```
→ succeeded, zero warnings/errors introduced.

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure -R RenderGraph
```
→ 100% tests passed, 166/166.

No full build and no full `ctest` regression were run this phase, per the phase document's own explicit
"No Full Build"/"Fast Compile Check" rules (PHASE5 is the only phase in this campaign that does a full clean
build + full regression + live smoke test).

## Deviations from the phase document

None. Every live file/line reference in the phase document (`RenderGraphTypes.h` `PassRecord` at ~line 392,
`RenderGraphBuilder.h` `AddComputePass()` at ~line 387, `RenderGraphSnapshot.h`
`RenderGraphPassSnapshot` at ~line 63, `RenderGraphSnapshot.cpp`'s `BuildPassSnapshot()`/
`ResourceUsageName()`) was re-verified against the live source before editing and found to match exactly —
no surprises, no design-decision forks encountered that the phase document (or `PHASE0_MASTER_STRATEGY.md`'s
Locked Design Decisions) hadn't already resolved. `ask_questions` was not needed this phase.

## Recommendation for a dedicated PHASE1-only double-check

Per this phase's own "Risk level: HIGH" framing and `PHASE0_MASTER_STRATEGY.md`'s Step 3.5 recommendation:
**a dedicated, isolated double-check of this phase specifically, before PHASE2 begins consuming
`isComputePass`/`readKinds`/`writeKinds`, is worth doing**, for two concrete reasons specific to this
change's blast radius:

1. This is the only phase in the whole `frame-debugger-5` campaign that touches core
   `src/Renderer/RenderGraph/` files consumed by **every** render pass in the engine (all 8 real
   `AddComputePass()` call sites plus every plain `AddPass()` graphics pass) — a subtle mistake here (e.g. a
   copy-paste that accidentally sets `isComputePass` on the wrong branch, or an off-by-one in the new
   `readKinds`/`writeKinds` parallel-vector population) would be a silent, engine-wide correctness bug for
   PHASE2's tree-building logic to build directly on top of, not a narrow, easily-isolated Editor-only one.
2. The `ResourceUsageName()` companion fix (3.4b) is a genuine, independently-shipping bug fix bundled into
   this phase (not scope creep — it lives in the exact function this phase's own metadata depends on to be
   meaningful) — worth an isolated second look specifically confirming it truly changes nothing for the
   `Texture`/`Buffer` cases (both branches were deliberately kept behaviorally identical to before, just
   reshaped into `switch` cases) and only adds new, previously-dead functionality for the `VolumeTexture`
   case.

Both risks are already mitigated by: (a) the full RenderGraph-scoped test suite passing 100% with zero
pre-existing assertions touched, (b) new test coverage added specifically isolating both the
`isComputePass` copy-through (surviving AND culled) and the `readKinds`/`writeKinds`/`ResourceUsageName()`
fix in dedicated tests. A human/second-agent read-through of this report plus the 4 touch-point diffs above
should be a fast, low-effort confirmation pass given that mitigation already in place.
