# PHASE4 — Runtime Integration, Cache Updates, and Cross-Codebase Test Fixture Repair (v2)

Part of the `verlet-integration-6` campaign — read `PHASE0_MASTER_STRATEGY.md`
through `PHASE3_SKELETON_ALIGNED_CHAIN_DETECTION_REWRITE.md` first; Phase 3
must already compile and pass its own tests before starting this phase. This
phase's job is purely PLUMBING: make the rest of the engine build and behave
correctly against Phase 1's new `DynamicChainDefinition` shape and Phase 3's
new `DetectDynamicChains()` return type — no new detection logic, no new
solver logic.

## Step 1: The Goal (Where are we going?)

`PhysicsSystem::RegisterDynamicChains()` calls the new
`DetectDynamicChains()`, stores its diagnostics alongside its chains in
`DynamicChainRigCache::ModelEntry`, and the disjointness invariant Phase 3's
construction relies on (`PHASE0_MASTER_STRATEGY.md`, Culprit E) gets a real,
debug-time defensive check so any future regression here is caught loudly.
Every pre-existing test file across the codebase that hand-builds a
`DynamicChainDefinition` compiles and passes again.

## Step 2: The Situation / The Problem (Where are we now?)

Exactly one real (non-test) call site exists for `DetectDynamicChains()`:
`src/Game/Physics/PhysicsSystem.cpp`, inside
`PhysicsSystem::RegisterDynamicChains()` (lines 129-146):

```cpp
void PhysicsSystem::RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
{
    const DynamicChainDetectionDefaults defaults{};
    std::vector<DynamicChainDefinition> chains
        = DetectDynamicChains(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr, defaults);

    DynamicChainRigCache::ModelEntry entry;
    entry.chains = std::move(chains);
    entry.skeleton = data.skeleton;
    m_rigCache.Register(absoluteGtaPath, std::move(entry));
}
```

This no longer compiles once Phase 3 changes `DetectDynamicChains()`'s return
type to `DynamicChainDetectionResult`. `DynamicChainRigCache::ModelEntry`
(`src/Game/Physics/DynamicChainRigCache.h`, lines 30-33) has no field to
receive the new `diagnostics` output at all. Five test files across the
codebase hand-build `DynamicChainDefinition` objects and will not compile
once `parentJointIndex` is a required, non-defaulted-to-"linear" member
(Phase 1 already updated three of these directly as part of its OWN scope —
`DynamicChainDefinitionTests.cpp`, `BoneChainPhysicsResolverTests.cpp`,
`DynamicChainSolverTests.cpp` — this phase re-confirms those three still
compile cleanly against Phase 3's REAL output shape, not just Phase 1's
hand-rolled fixtures, and fixes the remaining two Phase 1 did not touch:
`tests/Game/Physics/DynamicChainRigCacheTests.cpp` (line 24) and
`tests/Game/Physics/PhysicsSystemParallelTests.cpp` (line 66).

## Step 3: The Plan

### 3.1 `src/Game/Physics/DynamicChainRigCache.h` — new diagnostics field

Add one new field to `ModelEntry`:

```cpp
struct ModelEntry {
    std::vector<DynamicChainDefinition> chains;
    SkeletonData skeleton;
    // task_manager/verlet-integration-6, Phase 3/4 - carried straight from
    // DetectDynamicChains()'s own DynamicChainDetectionResult::diagnostics,
    // unmodified - PHASE5's Editor visualization reads
    // diagnostics.orphanedDynamicBoneIndices to render a non-simulated rigid
    // body distinctly (see this campaign's PHASE0_MASTER_STRATEGY.md).
    DynamicChainDetectionDiagnostics diagnostics;
};
```

Update this file's own header comment (lines 19-27, the "PHASE3 STUB" note)
— it is stale even before this campaign (chain detection has been real since
`verlet-integration-1`'s own Phase 4); replace it with a short, accurate
note that `Register()`/`TryGet()`/`TryGetMutable()` are unchanged by this
campaign, only `ModelEntry`'s own shape gained a field.

### 3.2 `src/Game/Physics/PhysicsSystem.cpp` — call-site update + defensive assertion

```cpp
void PhysicsSystem::RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
{
    const DynamicChainDetectionDefaults defaults{};
    DynamicChainDetectionResult detection
        = DetectDynamicChains(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr, defaults);

#ifndef NDEBUG
    // task_manager/verlet-integration-6, Phase 4 - defensive re-verification
    // of the disjoint-jointBoneIndices invariant PhysicsSystem::Update()'s
    // own parallel dispatch path relies on (see this file's own comment
    // above DynamicChainBatchContext) - Phase 3's construction is SUPPOSED
    // to guarantee this by construction (every participating bone is
    // assigned to exactly one chain), but this is cheap, debug-only
    // insurance against a future regression silently corrupting a shared
    // pose buffer under the parallel path instead of failing loudly here.
    {
        std::unordered_set<std::int32_t> seenBoneIndices;
        for (const DynamicChainDefinition& chain : detection.chains) {
            for (std::int32_t boneIndex : chain.jointBoneIndices) {
                assert(seenBoneIndices.insert(boneIndex).second
                    && "DetectDynamicChains() produced two chains sharing a bone index - parallel dispatch is unsafe.");
            }
        }
    }
#endif

    DynamicChainRigCache::ModelEntry entry;
    entry.chains = std::move(detection.chains);
    entry.skeleton = data.skeleton;
    entry.diagnostics = std::move(detection.diagnostics);
    m_rigCache.Register(absoluteGtaPath, std::move(entry));
}
```

(Add `#include <unordered_set>` and `#include <cassert>` to this file's own
include list if not already present — confirm before editing.) Update this
function's own existing comment (lines 131-137) to drop the stale
"`Bone::deformAfterPhysics`/`RigidBody::motionType`" wording and describe the
new RigidBody/Joint-graph-driven algorithm in one or two sentences,
cross-referencing `task_manager/verlet-integration-6/
PHASE0_MASTER_STRATEGY.md` the same way this codebase's other campaign
cross-references already work.

No other line in `PhysicsSystem.cpp` needs to change — `Update()`'s own
per-chain stepping code (`StepDynamicChainRange()`,
`RunDynamicChainBatchJob()`) only ever reads `DynamicChainDefinition`/
`DynamicChainRuntimeState` fields that Phase 1 already made resolver/solver-
compatible; it never itself assumes `i-1`.

### 3.3 `tests/Game/Physics/DynamicChainRigCacheTests.cpp` — fixture repair

Line 24's fixture (`chain.jointBoneIndices = { 1 };`) needs
`.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(1)`
added (a single-joint chain — `MakeLinearParentIndices(1)` returns `{-1}`).
No other change needed in this file — it only exercises
`DynamicChainRigCache::Register()`/`TryGet()`/`TryGetMutable()`'s own
storage/lookup contract, which Phase 3's new `diagnostics` field does not
affect (a test that wants to also assert `diagnostics` round-trips through
`Register()`/`TryGet()` may optionally add one small new test,
`RegisteredDiagnosticsRoundTripThroughTryGet`, but this is not required for
correctness — `ModelEntry` is a plain struct copy either way).

### 3.4 `tests/Game/Physics/PhysicsSystemParallelTests.cpp` — fixture repair

Line 66's fixture (`chain.jointBoneIndices = { jointIndex };`) needs the
identical one-line addition:
`chain.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(1);`
This file's own header comment (lines 31-39) already documents the
"deliberately disjoint rootBoneIndex/jointBoneIndices per chain" invariant
this whole file exists to exercise under real parallel dispatch — leave that
prose as-is (still accurate); only the fixture construction needs the new
field.

### 3.5 Re-confirm Phase 1's own three fixture files against Phase 3's REAL output shape

Phase 1 already added `.parentJointIndex = DynamicChainDefinition::
MakeLinearParentIndices(...)` to every hand-built fixture in
`DynamicChainDefinitionTests.cpp`, `BoneChainPhysicsResolverTests.cpp`, and
`DynamicChainSolverTests.cpp` as part of ITS OWN scope (these three files
test the DATA MODEL and RUNTIME in isolation, never `DetectDynamicChains()`
itself, so they do not depend on Phase 3 at all to compile or pass) — this
phase's own job here is simply to run the FULL test suite once more after
Phase 3 lands and confirm nothing about Phase 3's real-world output shape
(e.g. an actual populated `extraConstraints` list) breaks any assumption
those three files' own tests make. No code changes are expected in this
step; it exists purely as an explicit, named verification checkpoint in this
phase's own plan (per this campaign's "focus strictly on programming"
instruction, a step that turns up nothing to fix requires no code change and
is correctly a no-op here, NOT a wasted phase — the checkpoint itself is
what confirms Phases 1-4 integrate correctly as a whole before Phase 5
starts consuming this phase's own new `diagnostics` field).

### 3.6 Build wiring

No new files are added in this phase — `RigidBodyJointGraph.h/.cpp`/tests
(Phase 2) and `DynamicChainDetection.h/.cpp`'s rewritten body (Phase 3) are
already wired into `CMakeLists.txt` by their own respective phases. This
phase only edits the bodies of already-registered files
(`PhysicsSystem.cpp`, `DynamicChainRigCache.h`, and the two test files in
3.3/3.4).

## Revision Notes (v2)

Phase 3's v2 revision added one new field to `DynamicChainDetectionDiagnostics`
(`duplicateBoneRigidBodyAssignmentsDropped`) and changed how
`orphanedDynamicBoneIndices` is populated internally (now unioned from two
sources instead of one) — **neither change requires any code edit in this
phase.** `PhysicsSystem.cpp`'s `entry.diagnostics = std::move(detection.diagnostics);`
(3.2 above) already moves the WHOLE struct by value; any field Phase 3 adds
to `DynamicChainDetectionDiagnostics` round-trips through
`DynamicChainRigCache::ModelEntry`/`Register()`/`TryGet()` automatically,
with zero additional plumbing here. This phase's own plan (3.1-3.6) was
independently re-verified against the current source tree during the v2
review and required no other change.

One recommendation upgraded from optional to encouraged: 3.3's own
optional `RegisteredDiagnosticsRoundTripThroughTryGet` test is worth actually
adding now (still not strictly required for correctness — `ModelEntry` is a
plain struct copy either way) — Phase 5 v2 starts reading
`crossChainJointsDropped` and `duplicateBoneRigidBodyAssignmentsDropped`
directly out of the cache for the first time (v1 only ever read
`orphanedDynamicBoneIndices`), so a small, cheap round-trip test covering
all four `DynamicChainDetectionDiagnostics` fields at once gives Phase 5 a
little more confidence its own new data source is wired correctly before it
starts drawing anything with it.
