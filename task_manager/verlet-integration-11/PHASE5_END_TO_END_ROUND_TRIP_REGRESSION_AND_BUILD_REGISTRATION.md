# PHASE5 (v2, renamed from v1's PHASE4) — Build Registration + End-to-End Round-Trip Regression Tests

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
depends on: `PHASE1_GTA_METADATA_JOINT_OVERRIDE_DATA_MODEL.md`,
`PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md`,
`PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md`,
`PHASE4_EDITOR_SAVE_TO_ASSET_PERSISTENCE.md`
required by: nothing (final phase)

> **v2 changes from v1's PHASE4:** (1) a SECOND end-to-end test is added
> (Step 3.4) proving the "later in the same session" half of this campaign's
> promise (PHASE3's fix), using the SAME `PhysicsSystem` instance twice rather
> than two independent ones. (2) Step 3.5 ("cross-check every earlier phase")
> gained one new bullet for PHASE3. (3) the build-registration lists (3.1/3.2)
> gained the new files PHASE1's 3.9 and PHASE3 introduced. Everything else is
> unchanged from v1 — re-verified against the current source tree (exact
> `CMakeLists.txt`/`tests/CMakeLists.txt` line numbers for every "place next
> to" instruction below were re-confirmed via direct search against the
> CURRENT files and are accurate as of this writing).

## Step 1: The Goal

Three things, all mandatory before this campaign is actually "done":

1. **Every new source file PHASE1-4 introduced is actually compiled and
   linked** — this codebase lists every source file explicitly in
   `CMakeLists.txt`/`tests/CMakeLists.txt` (no glob), so a new `.cpp` that is
   never added to either file compiles nothing and links nothing, silently.
2. **One real, automated test proves the entire user-facing story end-to-end
   for a fresh process** — edit -> save -> (simulate a fresh load) reload ->
   the RELOADED simulation actually uses the saved values, not the original
   auto-detected defaults.
3. **(v2) A second automated test proves the SAME thing holds for a model
   respawned LATER IN THE SAME RUNNING SESSION** (no process restart) —
   without this, PHASE3's own fix (the single most important addition in v2)
   would be unverified by anything beyond manual inspection.

Every earlier phase's own tests prove one LINK of this chain in isolation
(PHASE1: bytes round-trip, and a re-import preserves them; PHASE2: applying a
hand-built override list works; PHASE3: the "never cached" no-op branch
returns false and touches nothing; PHASE4: saving a hand-built chain list to a
real file works) — none of them, alone, proves the FULL chain holds together
when composed, for either the "fresh process" or "same session" case.

## Step 2: The Situation

- `CMakeLists.txt` (repo root) lists every `gte_core` source file explicitly —
  confirmed by direct search against the CURRENT file:
  `src/Physics/DynamicChainDefinition.cpp` appears at line 240,
  `src/Game/Physics/PhysicsSystem.cpp` at line 418 — both as single explicit
  entries in a flat list, no `file(GLOB ...)`/`file(GLOB_RECURSE ...)`
  anywhere in this project.
- `tests/CMakeLists.txt` mirrors this exactly for `GreatTamanaEngineTests` —
  confirmed by direct search: `Physics/DynamicChainDefinitionTests.cpp`
  appears at line 1588 as a single explicit entry, with its own campaign
  provenance documented in a comment block at line 952 — the pattern to
  extend for this campaign's own new test files too.
- Per `AGENTS.md`'s "Testability & Regression Safety": *"Run the actual test
  suite before considering any change to `gte_core` done - a successful build
  is not enough."* This phase's own end-to-end tests are what actually
  discharge that obligation for this whole campaign.

## Step 3: The Plan

### 3.1 — `CMakeLists.txt` additions (repo root, `gte_core` target)

Add these lines, next to their most closely related existing neighbors (keep
the file's existing per-folder grouping/ordering convention — i.e.
`src/Physics/...` entries stay grouped together, `src/Game/Physics/...`
entries stay grouped together):

```
src/Physics/JointPhysicsOverrideApplication.cpp
```
— placed immediately next to the existing `src/Physics/DynamicChainDefinition.cpp`
entry (line 240, same folder, closely related module).

```
src/Game/Physics/DynamicChainPhysicsPersistence.cpp
```
— placed immediately next to the existing `src/Game/Physics/PhysicsSystem.cpp`
entry (line 418).

**(v2) No new `.cpp` from PHASE1's 3.9 or PHASE3** needs a CMake entry:
PHASE1's 3.9 only edits the ALREADY-registered `src/Assets/AssetImporter.cpp`;
PHASE3 only edits the ALREADY-registered `src/Game/Instantiation/MeshAssetGpuCatalog.cpp`/
`.h`, `src/Game/Instantiation/MeshInstantiationSystem.h`, `src/Game/Game.h`,
`src/Editor/Panels/InspectorPanel.h`/`.cpp`, and `src/Editor/ImGuiEditorLayer.cpp`
— every one of those files is already a build source today.

(`.h`-only additions — `JointPhysicsOverride` inside the already-registered
`src/Assets/PhysicsData.h`, and `JointPhysicsOverrideApplication.h`/
`DynamicChainPhysicsPersistence.h` themselves, which sit alongside their own
already-listed `.cpp` — need no separate CMake entry; this project's
convention, confirmed by inspection of the existing list, is to list `.cpp`
files only, with matching `.h` files picked up automatically via each
target's include directories.)

### 3.2 — `tests/CMakeLists.txt` additions

Add, next to their most closely related existing neighbors, following the
exact same "one explicit line per test file" convention:

```
Physics/JointPhysicsOverrideApplicationTests.cpp
```
— placed next to `Physics/DynamicChainDefinitionTests.cpp` (line 1588).

```
Game/Physics/DynamicChainPhysicsPersistenceTests.cpp
```
— placed next to `Game/Physics/PhysicsSystemTests.cpp` (line 1560).

```
Game/Physics/PhysicsSystemJointOverrideEndToEndTests.cpp
```
— (this phase's own new file, see 3.3 below) — placed next to the same
`Game/Physics/PhysicsSystemTests.cpp` neighbor.

**(v2, NEW)**
```
Game/Instantiation/MeshAssetGpuCatalogJointOverrideRefreshTests.cpp
```
— PHASE3's own new test file (its "never cached" no-op branch, the one piece
genuinely Tier-1-testable without a live Renderer) — placed next to whatever
existing `Game/Instantiation/...` test file this project already lists (check
the file's own existing `Game/Instantiation/` grouping and insert
alphabetically/thematically alongside it).

**(v2, NEW)** Extend `tests/Assets/AssetImporterTests.cpp`'s own EXISTING
CMake entry is a no-op (the file itself already exists and is already listed
— PHASE1's 3.9 only ADDS test cases to it, never a new file) — nothing to add
here for that specific change.

If PHASE2's own "extend `PhysicsSystemTests.cpp`" step (see that phase's Step
3.4) was instead done by adding a NEW sibling file (its own text already flags
this as a "check before choosing" decision), add that file's own explicit line
here too, e.g. `Game/Physics/PhysicsSystemJointOverrideApplicationTests.cpp`.

Also extend the existing top-of-file comment block (line 952) that documents
each Tier-1 test file's own campaign provenance, with one new entry for each
new file added above, matching that block's existing one-line-per-file style,
e.g.:

```
#   Physics/JointPhysicsOverrideApplicationTests.cpp - task_manager/verlet-integration-11,
#     PHASE2 - pure ApplyJointPhysicsOverrides() unit tests.
#   Game/Physics/DynamicChainPhysicsPersistenceTests.cpp - task_manager/verlet-integration-11,
#     PHASE4 - SaveJointPhysicsOverridesToGtaFile() real-file round-trip tests.
#   Game/Instantiation/MeshAssetGpuCatalogJointOverrideRefreshTests.cpp - task_manager/
#     verlet-integration-11, PHASE3 - RefreshCachedJointPhysicsOverridesFromDisk()'s
#     Tier-1-testable "never cached" no-op branch.
#   Game/Physics/PhysicsSystemJointOverrideEndToEndTests.cpp - task_manager/verlet-integration-11,
#     PHASE5 - full edit -> save -> reload end-to-end regression, both across a
#     fresh process AND within the same running session.
```

### 3.3 — The "fresh process" end-to-end test: new file `tests/Game/Physics/PhysicsSystemJointOverrideEndToEndTests.cpp`

This is the first of two money tests for the whole campaign — it must NOT
reuse any in-memory shortcut (it must genuinely go through `ReadGtaFile()`/
`DecodeRigDataFromBytes()` a second time, against the SAME file on disk, using
a SECOND, freshly-constructed `PhysicsSystem`, to honestly simulate "a later
session/a fresh spawn after a full restart").

```cpp
// End-to-end regression for task_manager/verlet-integration-11 (PHASES 1-4):
// proves a user's live joint-physics edit survives a real save-to-*.gta ->
// reload-from-*.gta round trip and is genuinely re-applied by
// PhysicsSystem::RegisterDynamicChains() on the NEXT load, not merely held in
// the SAME PhysicsSystem's own in-memory cache. No GPU/SDL/ImGui involved -
// real disk I/O only (matches GtaFileTests.cpp/RigFileTests.cpp's own
// established "Tier 1 + real temp file" precedent).

#include "Game/Physics/PhysicsSystem.h"
#include "Game/Physics/DynamicChainPhysicsPersistence.h"
#include "Assets/GtaFile.h"
#include "Assets/MeshFile.h"
#include "Assets/RigFile.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace gte {
namespace {

// Builds and writes a real, minimal but genuinely chain-detectable Mesh
// *.gta to `path`: a mesh payload (EncodeMeshDataToBytes) plus a RigFileData
// (EncodeRigDataToBytes) whose skeleton + PhysicsData::rigidBodies/joints are
// shaped so DetectDynamicChains() (Physics/DynamicChainDetection.h) produces
// AT LEAST ONE chain with a known, asserted-on bone index - e.g. one Static
// RigidBody anchor bone plus one Dynamic RigidBody bone connected by one
// Joint, mirroring DynamicChainDetectionTests.cpp's own minimal fixture shape
// (reuse/promote that file's existing helper rather than reinventing one, if
// it is already suitable and exported/promotable).
void WriteDetectableSkinnedMeshGtaFile(const std::filesystem::path& path, std::int32_t* outJointBoneIndex);

// Reads `path` fresh off disk and builds a SkinnedMeshData exactly the way
// MeshAssetGpuCatalog::EnsureMeshAsset() would (read the file, decode the
// rig, copy across the same fields) - shared by BOTH tests below so the
// "fresh process" and "same session" scenarios build their input identically.
SkinnedMeshData LoadSkinnedMeshDataFromDisk(const std::filesystem::path& path)
{
    const std::optional<GtaFileData> gta = ReadGtaFile(path);
    EXPECT_TRUE(gta.has_value());
    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    EXPECT_TRUE(mesh.has_value());
    const std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    EXPECT_TRUE(rig.has_value());

    SkinnedMeshData data;
    data.bindPositions = mesh->positions;
    data.bindNormals.resize(mesh->positions.size(), Vec3::Up());
    data.uvs.resize(mesh->positions.size(), Vec2::Zero());
    data.skinWeights = rig->skinWeights;
    data.skeleton = rig->skeleton;
    data.physics = rig->physics;
    data.jointPhysicsOverrides = rig->jointPhysicsOverrides;
    return data;
}

TEST(PhysicsSystemJointOverrideEndToEndTest, EditedJointPhysicsSurvivesSaveAndReloadAcrossAFreshProcess)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "JointOverrideEndToEndTest.gta";
    std::int32_t jointBoneIndex = -1;
    WriteDetectableSkinnedMeshGtaFile(path, &jointBoneIndex);
    ASSERT_GE(jointBoneIndex, 0);

    // --- "First load": register with the auto-detected defaults - proves
    // a NEVER-edited model gets pure defaults, nothing pre-seeded. ---
    PhysicsSystem firstLoadPhysics;
    firstLoadPhysics.RegisterDynamicChains(path.string(), LoadSkinnedMeshDataFromDisk(path));

    const DynamicChainRigCache::ModelEntry* firstModel = firstLoadPhysics.GetDynamicChainRigCache().TryGet(path.string());
    ASSERT_NE(firstModel, nullptr);
    ASSERT_FALSE(firstModel->chains.empty());

    // Sanity: before ANY save, the loaded joint's settings are just the pure
    // defaults (DynamicChainDetectionDefaults{}.defaultJointSettings), NOT
    // the values this test is about to save below - guards against a false
    // pass if some earlier step accidentally already wrote overrides.
    const DynamicJointSettings defaults{};
    bool foundBeforeSave = false;
    for (const DynamicChainDefinition& chain : firstModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                EXPECT_FLOAT_EQ(chain.jointSettings[i].damping, defaults.damping);
                foundBeforeSave = true;
            }
        }
    }
    ASSERT_TRUE(foundBeforeSave);

    // --- Simulate an Editor drag-edit directly on the live cache (exactly
    // what Panels/InspectorPanel.cpp's DragFloat callbacks do via
    // TryGetMutable()), then Save. ---
    DynamicChainRigCache::ModelEntry* mutableModel = firstLoadPhysics.GetDynamicChainRigCache().TryGetMutable(path.string());
    ASSERT_NE(mutableModel, nullptr);
    for (DynamicChainDefinition& chain : mutableModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                chain.jointSettings[i].damping = 0.93f;
                chain.jointSettings[i].stiffness = 0.04f;
                chain.jointSettings[i].mass = 6.25f;
            }
        }
    }
    std::string saveError;
    ASSERT_TRUE(SaveJointPhysicsOverridesToGtaFile(path.string(), mutableModel->chains, &saveError)) << saveError;

    // --- "A future session, after a full restart": a BRAND NEW
    // PhysicsSystem (proves nothing survives via in-memory state), re-reading
    // the SAME file from disk from scratch (LoadSkinnedMeshDataFromDisk()
    // always reads the file fresh - it has no cache of its own, unlike
    // MeshAssetGpuCatalog), exactly as Game::CreateMeshEntityFromGtaFile()
    // would for a freshly-spawned entity after an engine restart. ---
    PhysicsSystem secondLoadPhysics;
    secondLoadPhysics.RegisterDynamicChains(path.string(), LoadSkinnedMeshDataFromDisk(path));

    const DynamicChainRigCache::ModelEntry* secondModel = secondLoadPhysics.GetDynamicChainRigCache().TryGet(path.string());
    ASSERT_NE(secondModel, nullptr);

    bool foundAfterReload = false;
    for (const DynamicChainDefinition& chain : secondModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                EXPECT_FLOAT_EQ(chain.jointSettings[i].damping, 0.93f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].stiffness, 0.04f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].mass, 6.25f);
                foundAfterReload = true;
            }
        }
    }
    EXPECT_TRUE(foundAfterReload);
}

} // namespace
} // namespace gte
```

### 3.4 — NEW in v2: the "same session" end-to-end test (same file, new `TEST`)

This is the second money test, and the one that specifically proves PHASE3's
fix (`PHASE0_MASTER_STRATEGY.md`, Step 2.5) actually closes the gap it was
written for. The key difference from 3.3's test: it reuses the SAME
`PhysicsSystem` instance for both registrations (mirroring "two entities of
the same model, spawned into the same running scene, one after the other"),
and it explicitly models what PHASE3's `RefreshCachedJointPhysicsOverridesFromDisk()`
does — re-reading the file fresh a SECOND time within the same simulated
session, rather than trusting a stale in-memory copy — which is exactly the
step that was MISSING before PHASE3/PHASE4 existed.

```cpp
TEST(PhysicsSystemJointOverrideEndToEndTest, EditedJointPhysicsIsPickedUpByASecondSpawnWithinTheSameSession)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "JointOverrideSameSessionTest.gta";
    std::int32_t jointBoneIndex = -1;
    WriteDetectableSkinnedMeshGtaFile(path, &jointBoneIndex);
    ASSERT_GE(jointBoneIndex, 0);

    // ONE PhysicsSystem for the whole test - mirrors ONE running Editor/game
    // session/process, never restarted. This is the crucial difference from
    // 3.3's test above (which deliberately uses TWO separate PhysicsSystem
    // instances to simulate a restart).
    PhysicsSystem physics;

    // --- "First spawn" (entity A) - registers with defaults, exactly like
    // 3.3's test. ---
    physics.RegisterDynamicChains(path.string(), LoadSkinnedMeshDataFromDisk(path));
    DynamicChainRigCache::ModelEntry* mutableModel = physics.GetDynamicChainRigCache().TryGetMutable(path.string());
    ASSERT_NE(mutableModel, nullptr);
    for (DynamicChainDefinition& chain : mutableModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                chain.jointSettings[i].damping = 0.55f;
                chain.jointSettings[i].stiffness = 0.07f;
                chain.jointSettings[i].mass = 3.5f;
            }
        }
    }
    std::string saveError;
    ASSERT_TRUE(SaveJointPhysicsOverridesToGtaFile(path.string(), mutableModel->chains, &saveError)) << saveError;

    // --- What PHASE4's Editor button handler does immediately after a
    // successful save: this test does not have a real MeshAssetGpuCatalog
    // (that class needs a live Renderer - Tier 2, see PHASE3's own Step 3.5),
    // so it directly re-creates what RefreshCachedJointPhysicsOverridesFromDisk()
    // conceptually guarantees - a FRESH read of the file's jointPhysicsOverrides
    // - by simply calling LoadSkinnedMeshDataFromDisk() again, honestly
    // proving the DOWNSTREAM half of PHASE3's fix (PhysicsSystem correctly
    // re-applies whatever fresh data it is given, a second time, on the SAME
    // instance) even though the UPSTREAM half (MeshAssetGpuCatalog's own
    // cache-refresh call) is Tier-2/manually-verified only (see PHASE3's own
    // Step 3.5 for why that half cannot be automated here). ---
    const SkinnedMeshData refreshedData = LoadSkinnedMeshDataFromDisk(path);
    ASSERT_FALSE(refreshedData.jointPhysicsOverrides.empty());

    // --- "Second spawn" (entity B), SAME running PhysicsSystem, SAME
    // session, fed the freshly-refreshed data - this is the exact call
    // Game::CreateMeshEntityFromGtaFile() makes for every spawn,
    // unconditionally, regardless of cache hit/miss upstream. ---
    physics.RegisterDynamicChains(path.string(), refreshedData);

    const DynamicChainRigCache::ModelEntry* secondSpawnModel = physics.GetDynamicChainRigCache().TryGet(path.string());
    ASSERT_NE(secondSpawnModel, nullptr);

    bool foundOnSecondSpawn = false;
    for (const DynamicChainDefinition& chain : secondSpawnModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                EXPECT_FLOAT_EQ(chain.jointSettings[i].damping, 0.55f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].stiffness, 0.07f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].mass, 3.5f);
                foundOnSecondSpawn = true;
            }
        }
    }
    EXPECT_TRUE(foundOnSecondSpawn);
}
```

**Why this test is honest about what it does and does not cover:** it proves
`PhysicsSystem::RegisterDynamicChains()` correctly re-applies fresh
`jointPhysicsOverrides` on a second call against the same instance (the
downstream half of the fix), and it proves the file genuinely contains the
saved values readable a second time. What it CANNOT automate (per PHASE3's
own Step 3.5) is `MeshAssetGpuCatalog::RefreshCachedJointPhysicsOverridesFromDisk()`
itself actually being called and actually mutating a real, GPU-backed cache
entry — that one remaining link is verified manually (PHASE3's own checklist,
cross-checked in Step 3.5 below), consistent with this whole class's
pre-existing, accepted "Tier 2, no automated coverage yet" status.

### 3.5 — Cross-check every earlier phase's own test-file decisions actually landed

Before considering this campaign complete, re-open and confirm (a quick
read, not a re-derivation):

- PHASE1: `tests/Assets/RigFileTests.cpp` has the 4 new/extended cases from
  that phase's Step 3.8, and every PRE-EXISTING test in that file still passes
  unmodified. **(v2)** `tests/Assets/AssetImporterTests.cpp` has the 2 new
  cases from that phase's Step 3.9 (re-import preserves overrides; first-time
  import has nothing to preserve).
- PHASE2: `tests/Physics/JointPhysicsOverrideApplicationTests.cpp` exists with
  all 5 cases from that phase's Step 3.4, and whichever
  `PhysicsSystemTests.cpp`-adjacent integration test that phase's Step 3.4
  called for was actually added (not merely sketched).
- **(v2)** PHASE3: `MeshAssetGpuCatalog.h`/`.cpp` has the new
  `RefreshCachedJointPhysicsOverridesFromDisk()` method;
  `MeshInstantiationSystem.h`/`Game.h` have their new forwarding accessors;
  `InspectorPanel.h`/`.cpp` and `ImGuiEditorLayer.cpp` compile with the new
  `MeshInstantiationSystem&` parameter threaded through BOTH
  `BuildInspectorPanel()` overloads (`GTE_ENABLE_PROJECT_PANEL` ON and OFF —
  build BOTH configurations, since this parameter is NOT gated behind that
  switch); `tests/Game/Instantiation/MeshAssetGpuCatalogJointOverrideRefreshTests.cpp`
  exists with its one automated case, and PHASE3's own manual Editor
  verification checklist (its Step 3.5) has actually been walked through at
  least once.
- PHASE4: `tests/Game/Physics/DynamicChainPhysicsPersistenceTests.cpp` exists
  with all 4 cases from that phase's Step 3.4, and
  `Panels/InspectorPanel.cpp`'s new button/feedback text (including the
  PHASE3 refresh call) compiles under `GTE_ENABLE_EDITOR` (this is the one
  piece of this whole campaign with no automated test coverage, per
  `AGENTS.md`'s own accepted "Tier 2, no automated coverage yet" bucket for
  ImGui-touching code — a manual Editor smoke-check of the button is
  reasonable here, but is explicitly NOT a substitute for the automated
  `DynamicChainPhysicsPersistenceTests.cpp` coverage of the underlying
  `SaveJointPhysicsOverridesToGtaFile()` function itself, which IS fully
  automated).

### 3.6 — Full test suite build + run

Build `GreatTamanaEngineTests` **in both `GTE_ENABLE_PROJECT_PANEL` ON and OFF
configurations** (v2: this is now load-bearing, since PHASE3's new
`MeshInstantiationSystem&` parameter was added to `BuildInspectorPanel()`'s
signature in BOTH the `#if`/`#else` branches — a mistake threading it through
only one branch would compile fine in whichever configuration is built by
default and fail only in the other) and run the test suite in full (not just
the new files) — per `AGENTS.md`'s "Testability & Regression Safety": *"a
successful build is not enough."* Any pre-existing test that starts failing
because of this campaign's changes (most likely candidates: anything in
`tests/Assets/RigFileTests.cpp` if PHASE1's cursor-guard was implemented
incorrectly, anything in `tests/Assets/AssetImporterTests.cpp` if PHASE1's
3.9 re-import preservation logic has a bug, or anything in
`tests/Game/Physics/` if PHASE2's insertion point inside
`RegisterDynamicChains()` was placed wrong relative to the existing
`#ifndef NDEBUG` disjointness assert) is a real regression to fix before this
campaign is done, never something to loosen the assertion on without first
understanding exactly why it changed.

### What This Phase Deliberately Does NOT Do

- Does not add any NEW production behavior of its own — every behavior this
  phase's own tests exercise was already fully implemented by PHASE1-4; this
  phase only wires the build and proves composition.
- Does not attempt headless/Tier-2 GPU verification of the actual rendered
  jiggle behavior in a live window, nor of `MeshAssetGpuCatalog::
  RefreshCachedJointPhysicsOverridesFromDisk()`'s own cache-mutating branch —
  out of scope per `AGENTS.md`'s own "Tier 2... has no automated test
  coverage yet... never a blocker for landing changes" policy; the full
  `DynamicJointSettings` -> simulated motion pipeline this campaign feeds into
  is already covered by that pipeline's own pre-existing Tier-1 tests
  (`DynamicChainSolverTests.cpp`, `BoneChainPhysicsResolverTests.cpp`, etc.)
  and is unmodified by this campaign.
