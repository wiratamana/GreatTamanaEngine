# PHASE2 (v2) — Applying Saved Joint Overrides at Instantiation Time

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
depends on: `PHASE1_GTA_METADATA_JOINT_OVERRIDE_DATA_MODEL.md`
required by: `PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md`,
`PHASE5_END_TO_END_ROUND_TRIP_REGRESSION_AND_BUILD_REGISTRATION.md`

> **v2 change from v1:** none to this phase's own technical content — it was
> re-verified line-by-line against the current source tree (`DynamicChainDefinition.h`,
> `PhysicsSystem.cpp`'s actual `RegisterDynamicChains()` body) and every quoted
> snippet/line reference below is accurate as-is. Only the header
> cross-references above changed, to reflect PHASE0 v2's phase renumbering
> (a new PHASE3 was inserted between this phase and the old PHASE3/PHASE4;
   this phase's own "required by" list now names the new PHASE3 and the
   renamed PHASE5 instead of the old PHASE3/PHASE4).

---

## Step 1: The Goal

Make the data PHASE1 taught the engine to load actually DO something: the
moment a model with saved `jointPhysicsOverrides` is spawned into a scene
(`Game::CreateMeshEntityFromGtaFile()` -> `PhysicsSystem::
RegisterDynamicChains()`), every joint named by one of those overrides must
have its live `DynamicJointSettings::damping/stiffness/mass` set to the SAVED
values instead of whatever `DetectDynamicChains()`'s pure defaults (or
matched-RigidBody seeding) would otherwise produce — **before** the resulting
chain list is cached in `DynamicChainRigCache` and **before** any entity's
`DynamicChainRig` component is attached. This is the exact behavior the user
asked for: *"whenever the model got instantiated into scene, it will load the
metadata and apply it [at] actual runtime."*

(This phase makes that true whenever `RegisterDynamicChains()` is handed
FRESH `jointPhysicsOverrides` data — which is always true for the FIRST spawn
of any given model path in a process's lifetime. Making it ALSO true for a
SECOND-or-later spawn of an already-loaded path within the SAME session is a
separate, independent concern, fully handled by PHASE3 — see
`PHASE0_MASTER_STRATEGY.md`, Step 2.5, for why these are genuinely two
different problems.)

## Step 2: The Situation

- `src/Physics/DynamicChainDefinition.h` already defines `DynamicJointSettings`
  and `DynamicChainDefinition{ jointBoneIndices, jointSettings, ... }` —
  `jointSettings` is index-aligned 1:1 with `jointBoneIndices` (see that
  file's own doc comment).
- `src/Physics/DynamicChainDetection.h`'s `DetectDynamicChains()` is the ONLY
  place `DynamicChainDefinition::jointSettings` is populated today — from
  `DynamicChainDetectionDefaults::defaultJointSettings` plus whatever the
  joint's own matched PMX `RigidBody` overrides (mass/damping — see that
  file's own Step G).
- `src/Game/Physics/PhysicsSystem.cpp`'s `RegisterDynamicChains()` is the one
  call site that runs `DetectDynamicChains()` and then packages the result
  into a `DynamicChainRigCache::ModelEntry`:
  ```cpp
  void PhysicsSystem::RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
  {
      const DynamicChainDetectionDefaults defaults{};
      DynamicChainDetectionResult detection
          = DetectDynamicChains(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr, defaults);
      ...
      DynamicChainRigCache::ModelEntry entry;
      entry.chains = std::move(detection.chains);
      ...
      m_rigCache.Register(absoluteGtaPath, std::move(entry));
  }
  ```
- As of PHASE1, `data` (a `SkinnedMeshData`) already carries
  `jointPhysicsOverrides` — this phase's whole job is to consume it, right
  here, between `DetectDynamicChains()` returning and `m_rigCache.Register()`
  being called.
- Per `AGENTS.md`'s own established discipline ("Design new logic to be
  Tier-1-testable whenever the underlying problem allows it" /
  `DynamicChainDefinition.h`'s existing `FindDynamicChainJointByBoneIndex()`
  precedent, itself deliberately "pure/free (no ECS, no Editor, no GPU)"), the
  actual apply logic must be its own small, pure, free function — NOT inlined
  directly into `PhysicsSystem::RegisterDynamicChains()` — so it can be
  unit-tested against hand-built `DynamicChainDefinition`s with zero
  `SkinnedMeshData`/PMX/detection machinery involved.

## Step 3: The Plan

### 3.1 — New pure function module: `src/Physics/JointPhysicsOverrideApplication.h`

```cpp
#pragma once
#include "DynamicChainDefinition.h"
#include "../Assets/PhysicsData.h"

#include <vector>

namespace gte {

// task_manager/verlet-integration-11, PHASE2 - applies previously-SAVED,
// user-edited joint tuning (JointPhysicsOverride, Assets/PhysicsData.h,
// PHASE1) on TOP of a freshly-DetectDynamicChains()-produced chain list,
// mutating each matching joint's DynamicJointSettings::damping/stiffness/mass
// in place. Matched purely by SKELETON BONE INDEX (DynamicChainDefinition::
// jointBoneIndices entries) - see PHASE0_MASTER_STRATEGY.md, Step 2.4, for
// why bone index is the one stable identity available at this point, before
// any chain/joint-in-chain position has even been computed for this load.
//
// Pure/free function - no ECS, no Editor, no GPU, no file I/O - mirrors
// DynamicChainDefinition.h's own FindDynamicChainJointByBoneIndex() in spirit
// (see that function's own doc comment for the same "shared, tested,
// non-duplicated" rationale). The ONE production call site is
// PhysicsSystem::RegisterDynamicChains() (Game/Physics/PhysicsSystem.cpp),
// called once per model load, strictly AFTER DetectDynamicChains() and
// strictly BEFORE the result is registered into DynamicChainRigCache.
//
// Silently ignores (never asserts/crashes on) an override whose boneIndex
// does not match ANY joint of ANY chain in `chains` - a stale override left
// over from a model whose skeleton/detected chains have since changed is an
// entirely normal, expected input, not an error (mirrors
// DynamicChainJointLocation's own "no match is normal" contract). If more
// than one override in `overrides` names the SAME boneIndex (should never
// happen for a list built by SaveJointPhysicsOverridesToGtaFile(), PHASE4,
// but a hand-edited/corrupted file could still produce one), the LAST
// matching entry in `overrides` wins - documented explicitly here so a
// future caller/test never has to guess.
void ApplyJointPhysicsOverrides(
    std::vector<DynamicChainDefinition>& chains, const std::vector<JointPhysicsOverride>& overrides);

} // namespace gte
```

### 3.2 — Implementation: `src/Physics/JointPhysicsOverrideApplication.cpp`

```cpp
#include "JointPhysicsOverrideApplication.h"

#include <unordered_map>

namespace gte {

void ApplyJointPhysicsOverrides(
    std::vector<DynamicChainDefinition>& chains, const std::vector<JointPhysicsOverride>& overrides)
{
    if (overrides.empty()) {
        return; // Overwhelmingly common case (a model that has never been saved) - avoid building a map for nothing.
    }

    // "Last one wins" on a duplicate boneIndex (see this function's own
    // header comment) - a plain insert_or_assign-shaped loop, never a
    // conditional insert.
    std::unordered_map<std::int32_t, const JointPhysicsOverride*> byBoneIndex;
    byBoneIndex.reserve(overrides.size());
    for (const JointPhysicsOverride& o : overrides) {
        byBoneIndex[o.boneIndex] = &o;
    }

    for (DynamicChainDefinition& chain : chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            const auto found = byBoneIndex.find(chain.jointBoneIndices[i]);
            if (found == byBoneIndex.end()) {
                continue; // No saved override for this joint - keep whatever DetectDynamicChains() already computed.
            }
            const JointPhysicsOverride& o = *found->second;
            DynamicJointSettings& settings = chain.jointSettings[i];
            settings.damping = o.damping;
            settings.stiffness = o.stiffness;
            settings.mass = o.mass;
            // Deliberately NOT touched: group/collisionMask/collisionRadius
            // (PMX-shape-derived, never user-editable) and anything at the
            // DynamicChainDefinition level (collisionEnabled, gravityScale,
            // ...) - see PHASE0_MASTER_STRATEGY.md's "What We Will NOT Do".
        }
    }
}

} // namespace gte
```

### 3.3 — Wire into `PhysicsSystem::RegisterDynamicChains()`: `src/Game/Physics/PhysicsSystem.cpp`

Add `#include "../../Physics/JointPhysicsOverrideApplication.h"` alongside the
existing `Physics/` includes, then insert exactly one call between detection
and caching:

```cpp
void PhysicsSystem::RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
{
    const DynamicChainDetectionDefaults defaults{};
    DynamicChainDetectionResult detection
        = DetectDynamicChains(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr, defaults);

    // task_manager/verlet-integration-11, PHASE2 - apply any previously-
    // SAVED, user-edited joint tuning (Editor "Save Joint Physics to Asset",
    // PHASE4) ON TOP of the pure/PMX-seeded defaults DetectDynamicChains()
    // just computed - MUST run strictly after detection (so it has real
    // chains/jointBoneIndices to match against) and strictly before this
    // result is cached (so every consumer of DynamicChainRigCache, including
    // the very first entity spawned from this path this session, sees the
    // saved values, never the pre-override defaults for even one frame).
    ApplyJointPhysicsOverrides(detection.chains, data.jointPhysicsOverrides);

    std::vector<ModelColliderDefinition> colliders
        = DetectModelColliders(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr);

    #ifndef NDEBUG
    { /* existing disjoint-jointBoneIndices assert, unchanged - still valid: ApplyJointPhysicsOverrides()
         never adds/removes/reorders any joint, only rewrites damping/stiffness/mass in place */ }
    #endif

    DynamicChainRigCache::ModelEntry entry;
    entry.chains = std::move(detection.chains);
    entry.skeleton = data.skeleton;
    entry.diagnostics = std::move(detection.diagnostics);
    entry.colliders = std::move(colliders);
    m_rigCache.Register(absoluteGtaPath, std::move(entry));
}
```

(Only the one new line plus its comment are added; every other line in this
method is unchanged. The existing `#ifndef NDEBUG` disjointness assert stays
exactly where it is and remains valid unmodified, since
`ApplyJointPhysicsOverrides()` never adds, removes, or reorders any joint —
only rewrites three floats on existing entries in place.)

### 3.4 — Tests

**New file — `tests/Physics/JointPhysicsOverrideApplicationTests.cpp`** (Tier
1, pure, no ECS/GPU/file I/O, mirrors `DynamicChainDefinitionTests.cpp`'s own
hand-built-fixture style):

```cpp
#include "Physics/JointPhysicsOverrideApplication.h"
#include <gtest/gtest.h>

namespace gte {
namespace {

DynamicChainDefinition BuildTwoJointChain()
{
    DynamicChainDefinition chain;
    chain.rootBoneIndex = 0;
    chain.jointBoneIndices = { 1, 2 };
    chain.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(2);
    chain.restLengths = { 1.0f, 1.0f };
    chain.jointSettings.assign(2, DynamicJointSettings{});
    return chain;
}

TEST(JointPhysicsOverrideApplicationTest, MatchingOverrideRewritesDampingStiffnessMass)
{
    std::vector<DynamicChainDefinition> chains{ BuildTwoJointChain() };
    const std::vector<JointPhysicsOverride> overrides{ { /*boneIndex=*/2, 0.9f, 0.05f, 3.0f } };

    ApplyJointPhysicsOverrides(chains, overrides);

    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].damping, DynamicJointSettings{}.damping); // untouched (bone 1)
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].damping, 0.9f); // bone 2 - overridden
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].stiffness, 0.05f);
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].mass, 3.0f);
}

TEST(JointPhysicsOverrideApplicationTest, OverrideWithNoMatchingBoneIndexIsSilentlyIgnored)
{
    std::vector<DynamicChainDefinition> chains{ BuildTwoJointChain() };
    const std::vector<JointPhysicsOverride> overrides{ { /*boneIndex=*/999, 0.9f, 0.05f, 3.0f } };

    ApplyJointPhysicsOverrides(chains, overrides);

    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].damping, DynamicJointSettings{}.damping);
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].damping, DynamicJointSettings{}.damping);
}

TEST(JointPhysicsOverrideApplicationTest, EmptyOverrideListLeavesEveryChainUnchanged)
{
    std::vector<DynamicChainDefinition> chains{ BuildTwoJointChain() };
    ApplyJointPhysicsOverrides(chains, {});
    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].mass, DynamicJointSettings{}.mass);
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].mass, DynamicJointSettings{}.mass);
}

TEST(JointPhysicsOverrideApplicationTest, DuplicateBoneIndexOverridesLastEntryWins)
{
    std::vector<DynamicChainDefinition> chains{ BuildTwoJointChain() };
    const std::vector<JointPhysicsOverride> overrides{
        { 1, 0.1f, 0.1f, 1.0f },
        { 1, 0.7f, 0.6f, 5.0f }, // same boneIndex as above - this one must win.
    };

    ApplyJointPhysicsOverrides(chains, overrides);

    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].damping, 0.7f);
    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].mass, 5.0f);
}

TEST(JointPhysicsOverrideApplicationTest, AppliesAcrossMultipleChainsIndependently)
{
    DynamicChainDefinition chainA = BuildTwoJointChain();
    DynamicChainDefinition chainB = BuildTwoJointChain();
    chainB.rootBoneIndex = 10;
    chainB.jointBoneIndices = { 11, 12 }; // Disjoint bone indices from chainA - matches real DetectDynamicChains() invariant.
    std::vector<DynamicChainDefinition> chains{ chainA, chainB };

    const std::vector<JointPhysicsOverride> overrides{ { 11, 0.5f, 0.5f, 2.0f } };
    ApplyJointPhysicsOverrides(chains, overrides);

    EXPECT_FLOAT_EQ(chains[1].jointSettings[0].damping, 0.5f); // chainB's first joint (bone 11)
    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].damping, DynamicJointSettings{}.damping); // chainA untouched
}

} // namespace
} // namespace gte
```

**Extend `tests/Game/Physics/PhysicsSystemTests.cpp`** (or add a new sibling
file `PhysicsSystemJointOverrideEndToEndTests.cpp` if that existing file is
already large/thematically focused elsewhere — check before choosing) with an
integration-shaped test proving the WIRING (not just the pure function) works:

```cpp
TEST(PhysicsSystemTest, RegisterDynamicChainsAppliesSavedJointOverridesOnTopOfDetectionDefaults)
{
    // Build a minimal SkinnedMeshData whose skeleton/physics is guaranteed
    // to make DetectDynamicChains() produce at least one chain with a known
    // bone index (reuse whatever helper DynamicChainDetectionTests.cpp/
    // PhysicsSystemTests.cpp already has for this - do not hand-roll a new
    // one; if none is shared/exported yet, promote the existing local helper
    // to a small shared test-only header under tests/Physics/ or
    // tests/Game/Physics/ rather than duplicating it).
    SkinnedMeshData data = BuildSkinnedMeshDataWithOneDynamicChain(/* ... */);
    const std::int32_t someJointBoneIndex = /* a bone index known to be a chain joint per the helper above */;
    data.jointPhysicsOverrides.push_back(JointPhysicsOverride{ someJointBoneIndex, 0.77f, 0.06f, 4.5f });

    PhysicsSystem physics;
    physics.RegisterDynamicChains("test.gta", data);

    const DynamicChainRigCache::ModelEntry* model = physics.GetDynamicChainRigCache().TryGet("test.gta");
    ASSERT_NE(model, nullptr);
    ASSERT_FALSE(model->chains.empty());

    bool found = false;
    for (const DynamicChainDefinition& chain : model->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == someJointBoneIndex) {
                EXPECT_FLOAT_EQ(chain.jointSettings[i].damping, 0.77f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].stiffness, 0.06f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].mass, 4.5f);
                found = true;
            }
        }
    }
    EXPECT_TRUE(found);
}
```

### What This Phase Deliberately Does NOT Do

- Does not add any way to actually PRODUCE a non-empty
  `jointPhysicsOverrides` list from the Editor yet (PHASE4) — this phase's own
  tests build one by hand.
- Does not change `DetectDynamicChains()` itself in any way — overrides are
  applied strictly AFTER it runs, as a separate, later, independently-testable
  step (this is precisely why it is its own function rather than a parameter
  threaded into `DetectDynamicChains()`'s own signature).
- Does not touch `Panels/InspectorPanel.cpp` or anything under `src/Editor/`.
- **(v2)** Does not address the "already-cached, second spawn in the same
  session sees stale data" gap (`PHASE0_MASTER_STRATEGY.md`, Step 2.5) — that
  is entirely a `MeshAssetGpuCatalog` cache-freshness problem, upstream of
  this phase's own function, and is PHASE3's job.
