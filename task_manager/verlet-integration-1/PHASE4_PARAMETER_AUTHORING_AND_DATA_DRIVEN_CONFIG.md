# PHASE4 — Parameter Authoring and Data-Driven Configuration (v4 — ownership on `PhysicsSystem`, generic naming)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit A, plus the brief's own explicit
"some parameters are global/world, some are local/model-or-joint"
requirement). Depends on: Phase 3 v4 (`DynamicChainRig` component,
`PhysicsSystem`'s stubbed `m_globalSettings`/`m_rigCache` members
compiling). Modifies: `src/Game/Physics/PhysicsSystem.h/.cpp`,
`src/Game/Game.cpp` (mesh-registration call site), Editor Inspector panel.
Adds: `src/Physics/GlobalPhysicsSettings.h`, a new chain-auto-detection
module, and `src/Game/Physics/DynamicChainRigCache.h`. This is the phase
where dynamic bone chains stop being an always-empty stub and become real,
and where the "Damping/Stiffness/Weight/Wind" knobs the brief asks for
become genuinely tunable.

## v4 Revision Notice (naming only, read this first)

The "v3 Revision Notice" immediately below is kept intact for history. v4
adds exactly one further kind of change: every hair-specific identifier
this phase introduces is renamed to a generic equivalent, per
`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v4)" —
`HairPhysicsRigCache` → `DynamicChainRigCache`, `HairChainDetection.h` /
`DetectHairChains()` → `DynamicChainDetection.h` / `DetectDynamicChains()`,
`HairChainDetectionDefaults` → `DynamicChainDetectionDefaults`,
`RegisterHairPhysicsChains()` → `RegisterDynamicChains()`,
`AttachHairPhysicsRigIfNeeded()` → `AttachDynamicChainRigIfNeeded()`. No
detection algorithm, ownership decision, or Editor behavior described below
changed — only names. The Inspector section title itself also changes from
a hair-specific label to a generic one (see 3.5).

## v3 Revision Notice (ownership — kept for history)

Phase 3 was rewritten so that `GlobalPhysicsSettings` and the dynamic-chain
cache are owned by a brand-new, standalone `PhysicsSystem`
(`src/Game/Physics/`), NEVER by `AnimationSystem` — see that document's own
v3 Revision Notice for the full rationale (genuine ECS system
independence: `AnimationSystem` must never `#include` anything under
`src/Physics/`). Every place this document's v2 draft said "wire X into
`AnimationSystem`" now means "wire X into `PhysicsSystem`" instead. The
one other structural change from v2: `DynamicChainRigCache`'s cached
`ModelEntry` now also stores a private COPY of that model's `SkeletonData`
(alongside its detected `DynamicChainDefinition`s) — because `PhysicsSystem`
no longer has any access to `AnimationSystem::m_rigCache` (that would be
exactly the cross-system reach-through Phase 3 forbids), it must own
whatever skeleton-hierarchy data `Animation/BoneWorldMatrixQuery.h`'s
`ComputeBoneWorldMatrix()` needs to walk, independently. This is a small,
deliberate, accepted memory duplication (one extra `SkeletonData` copy per
distinct physics-bearing model path, not per entity/instance) in exchange
for `PhysicsSystem` never depending on `AnimationSystem`'s own cache at all
— see Phase 3's own "zero cross-system reach-through" rule.

## Step 1: The Goal

1. Split every physics parameter cleanly into **global** (one instance,
   tied to the whole world/scene — Wind, gravity, simulation quality) and
   **local** (per-model/per-joint — Damping, Stiffness, Weight, and each
   chain's own gravity/wind scale), matching the brief's explicit
   requirement verbatim.
2. Automatically DERIVE which bones form a dynamic bone chain from data
   the engine **already extracts from `.pmx`** today — no new asset format,
   no new authoring UI required to get a first working result, consistent
   with this engine's existing "reuse already-imported data" philosophy
   (see `PHASE0`'s Culprit A discussion of `Bone::deformAfterPhysics` and
   `RigidBody::motionType`).
3. Give a human a way to see and tune the local parameters live, through
   the existing Editor Inspector (a real, if minimal, code addition — not a
   "someday" UI stub).

## Step 2: The Situation / The Problem

- `SkeletonData.h`'s `Bone::deformAfterPhysics` ("physics-driven 'jiggle'
  bones") and `PhysicsData.h`'s `RigidBody::motionType ==
  RigidBodyMotionType::Dynamic` ("the bone should instead follow the
  SIMULATED rigid body — e.g. jiggle hair/skirt bones") are BOTH already
  populated by `PmxLoader.cpp` for any real MMD model that has physics-driven
  bones (most rigs with dynamic secondary motion do) — but, per Culprit A,
  nothing has ever read either flag. This is a ready-made, zero-new-format
  signal for "which bones should this campaign simulate."
- `PhysicsSystem` (Phase 3 v4) is a brand-new, independent class — it
  does NOT share `AnimationSystem`'s `SkeletalRigCache`/`AnimationClipCache`/
  `ResolvedAnimationBindingCache`. The new chain cache it owns
  (`DynamicChainRigCache`) follows the SAME `GetOrLoad()`/`TryGet()`-keyed-by-
  absolute-`*.gta`-path SHAPE those three already establish (consistency of
  convention), but is a genuinely separate cache instance, populated by a
  genuinely separate registration call, at a genuinely separate call site
  method (`PhysicsSystem::RegisterDynamicChains()`, never
  `AnimationSystem::RegisterSkinnedMesh()`).
- `Game::CreateMeshEntityFromGtaFile()` is the existing, single hand-off
  point where a model's `SkinnedMeshData` (which already carries the full
  `SkeletonData`, including `deformAfterPhysics`, and could carry
  `PhysicsData` alongside it — check `SkinnedMeshData`'s own definition in
  `SkeletalRigCache.h` and extend it if `PhysicsData` isn't already
  threaded through) first becomes available — this is where chain
  auto-detection must run, ONCE, not per-frame, via a call directly to
  `m_physicsSystem`, alongside (never through) the existing
  `m_animationSystem.RegisterSkinnedMesh()` call.

## Step 3: The Plan

### 3.1 `src/Physics/GlobalPhysicsSettings.h` (new) — the GLOBAL half

```cpp
#pragma once
#include "WindField.h"
#include "../Math/Vec3.h"

#include <cstdint>

namespace gte {

// ONE instance of this exists for the whole running game/scene (owned by
// PhysicsSystem - see 3.4 below; a future multi-scene engine would move
// this to a proper Scene/World object, but this engine has no such concept
// yet - see TODO.md's own "Scene serialization" entry - so
// PhysicsSystem, the sole current consumer, is the correct, honest
// owner for now - deliberately NOT AnimationSystem, see PHASE3's own v3
// Revision Notice for why physics-only global state must never live on the
// animation orchestrator). EVERYTHING in this struct is GLOBAL/world-tied,
// per the brief's own explicit split - contrast with
// DynamicChainDefinition/DynamicJointSettings (Physics/DynamicChainDefinition.h),
// which are LOCAL, per-model/per-joint.
struct GlobalPhysicsSettings {
    Vec3 gravity = Vec3(0.0f, -9.8f, 0.0f);
    WindSettings wind;

    // Simulation quality/perf knobs - also legitimately "global" (a
    // per-model override would be surprising/hard to reason about for a
    // scene with many physics-bearing models).
    float fixedTimestepSeconds = 1.0f / 60.0f;
    std::uint8_t maxStepsPerFrame = 4;
};

} // namespace gte
```

Update `PhysicsSystem::Update()` (Phase 3, 3.6) to read
`m_globalSettings.fixedTimestepSeconds`/`.maxStepsPerFrame` (it already does
- Phase 3 v4's draft already names this member `m_globalSettings`, of type
`GlobalPhysicsSettings`, directly - no further change needed there beyond
this struct itself now existing for real instead of being forward-declared).

### 3.2 Chain auto-detection: `src/Physics/DynamicChainDetection.h`/`.cpp` (new) - unchanged from v2, renamed in v4

Pure function, takes already-loaded `SkeletonData` (+ optional
`PhysicsData` for parameter seeding) and returns every detected chain — no
ECS, no file I/O, fully Tier-1-testable with a hand-built `SkeletonData`
fixture. (v3/v4 note: this module has zero ECS/`AnimationSystem`/
`PhysicsSystem` dependency either way - nothing here changes from v2 except
its own name.)

```cpp
#pragma once
#include "DynamicChainDefinition.h"
#include "../Assets/PhysicsData.h"
#include "../Assets/SkeletonData.h"

#include <vector>

namespace gte {

// LOCAL default parameters used to seed a detected chain's joints when no
// more specific per-bone RigidBody data is found (see below) - itself
// still "local" in the global/local split (these are per-MODEL defaults,
// tunable independently of GlobalPhysicsSettings), exposed to the Editor
// as the starting point a user then fine-tunes per joint (Phase 4.5/
// Inspector).
struct DynamicChainDetectionDefaults {
    DynamicJointSettings defaultJointSettings; // damping/stiffness/mass fallback.
    float defaultGravityScale = 1.0f;
    float defaultWindScale = 1.0f;
    std::uint8_t defaultConstraintIterations = 4;
    std::size_t minimumChainLength = 2; // shorter runs (e.g. a single physics-driven bone) are not worth simulating as a chain - skip them.
};

// Detects every maximal, LINEAR run of bones in `skeleton` where:
//   - every bone in the run has Bone::deformAfterPhysics == true, AND
//   - each bone's parentBoneIndex is the PREVIOUS bone in the same run
//     (a genuinely linear chain - a bone with more than one
//     deformAfterPhysics CHILD starts a new, separate chain per child
//     rather than being folded into one branching definition - see
//     PHASE2's own "we will not handle branching chains" scope note).
// The run's OWN PARENT bone (the first non-deformAfterPhysics ancestor)
// becomes that chain's rootBoneIndex (the pinned anchor - see
// DynamicChainDefinition.h). A run that starts at the skeleton's own literal
// ROOT bone (no parent at all, i.e. the would-be rootBoneIndex has no
// non-deformAfterPhysics ancestor to anchor to) is DISCARDED outright,
// never emitted with `rootBoneIndex == -1` - `Animation/BoneChainResolver.h`'s
// own documented contract makes `ComputeBoneWorldMatrix(..., -1)` silently
// return `Mat4::Identity()`, which would anchor that chain to the WORLD
// origin instead of the character, a visibly wrong "bone floating at
// (0,0,0)" result for what is already a malformed/degenerate rig. Chains
// shorter than `defaults.minimumChainLength` joints are ALSO discarded -
// both rejection rules apply independently.
//
// For each detected joint bone, if `physics` (may be nullptr - a model
// with no PMX rigidbody data at all is still fully supported, using pure
// defaults) contains a RigidBody whose own `boneIndex` matches AND whose
// `motionType != RigidBodyMotionType::Static`, that RigidBody's `mass`/
// `linearDamping` seed this joint's DynamicJointSettings::mass/damping
// instead of `defaults.defaultJointSettings` - reusing already-authored PMX
// physics data exactly the way RigidBodyMotionType::Dynamic's own doc
// comment always intended, without building a general rigid-body solver.
// `stiffness` has no PMX equivalent - always comes from
// `defaults.defaultJointSettings.stiffness` (or a later per-joint Inspector
// override - see 3.5), never derived from RigidBody data.
//
// restLengths (DynamicChainDefinition's own field) are computed here too,
// directly from skeleton.bones[...].position (bind-pose positions) via
// Length(childBindPos - parentBindPos) - precomputed once, never
// recomputed per frame (see DynamicChainDefinition.h's own doc comment).
std::vector<DynamicChainDefinition> DetectDynamicChains(
    const SkeletonData& skeleton, const PhysicsData* physics, const DynamicChainDetectionDefaults& defaults);

} // namespace gte
```

Implementation approach: build a `childrenByParent` adjacency list (a
`std::vector<std::vector<std::int32_t>>`, index-aligned with
`skeleton.bones`) once; then, for every bone whose OWN
`deformAfterPhysics == true` and whose PARENT's `deformAfterPhysics ==
false` — i.e. every chain's own root/first-joint boundary — walk forward
through `childrenByParent`, following the run only while there is EXACTLY
ONE `deformAfterPhysics` child (more than one child means a branch point:
stop the current chain there, and let each of those children start its OWN
new chain in a later top-level iteration). A bone whose
`deformAfterPhysics == true` but which has NO parent at all
(`parentBoneIndex < 0`, i.e. it IS the skeleton's own root bone) must be
SKIPPED here entirely, never treated as a chain-start boundary. This
directly reuses the "walk children of a bone" pattern already implicit in
`ECS/TransformHierarchy.h`'s own children-lookup convention, just applied
to `SkeletonData` bones instead of ECS entities.

Add `tests/Physics/DynamicChainDetectionTests.cpp`: a hand-built 5-bone
skeleton (root → chainRoot[not flagged] → joint1 → joint2 → joint3, all
three joints flagged `deformAfterPhysics`) detects exactly one 3-joint chain
with the correct `rootBoneIndex`/`jointBoneIndices` order and
correctly-computed `restLengths`; a skeleton with a BRANCH (joint1 has two
`deformAfterPhysics` children) detects two separate chains, each starting at
joint1's own two children; a skeleton with a run shorter than
`minimumChainLength` detects nothing; a `RigidBody` matching one joint's
`boneIndex` with `motionType == Dynamic` correctly overrides that one
joint's `mass`/`damping` while its siblings keep the defaults; a degenerate
skeleton whose OWN root bone (index 0, `parentBoneIndex == -1`) is itself
flagged `deformAfterPhysics == true` produces ZERO chains from that run
(never a chain with `rootBoneIndex == -1`), while any OTHER,
properly-anchored chain elsewhere in the same skeleton is still detected
normally. Add a matching descriptive paragraph for this new test file to
`tests/CMakeLists.txt`'s own header "Test taxonomy" comment block.

### 3.3 `src/Game/Physics/DynamicChainRigCache.h` (new; v3 — moved from `src/Game/Animation/`, extended with a `SkeletonData` copy; renamed in v4)

Mirrors `SkeletalRigCache.h`'s exact shape (path-keyed, `GetOrLoad`-style,
populated once at model-registration time, read every frame) but now lives
under `src/Game/Physics/` (owned by `PhysicsSystem`, never
`AnimationSystem`), and its cached entry carries a copy of `SkeletonData`
alongside the detected chains (see this document's own v3 Revision Notice
for why):

```cpp
#pragma once
#include "../../Physics/DynamicChainDefinition.h"
#include "../../Assets/SkeletonData.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

// Path-keyed cache of DETECTED dynamic bone chains (plus the SkeletonData
// they were detected from) per distinct mesh *.gta - mirrors
// SkeletalRigCache's own "Register() once, TryGet() every frame" convention
// (see AGENTS.md's own caching precedent table, Job System Phase 4 audit -
// this class's own TryGet() result is READ-SAFE under the identical rule
// already documented there: never call Register() again on the same key
// while a result from an earlier TryGet() call is still in use elsewhere
// this frame). Owned exclusively by PhysicsSystem - AnimationSystem must
// never reference this class (see PHASE3's own "zero cross-system
// reach-through" rule).
class DynamicChainRigCache {
public:
    struct ModelEntry {
        SkeletonData skeleton; // a private COPY - see this document's own v3 Revision Notice.
        std::vector<DynamicChainDefinition> chains;
    };

    void Register(const std::string& absoluteGtaPath, SkeletonData skeleton, std::vector<DynamicChainDefinition> chains)
    {
        m_entriesByPath[absoluteGtaPath] = ModelEntry{ std::move(skeleton), std::move(chains) };
    }

    const ModelEntry* TryGet(const std::string& absoluteGtaPath) const
    {
        const auto it = m_entriesByPath.find(absoluteGtaPath);
        return it != m_entriesByPath.end() ? &it->second : nullptr;
    }

private:
    std::unordered_map<std::string, ModelEntry> m_entriesByPath;
};

} // namespace gte
```

### 3.4 Wire real detection + real cache into `PhysicsSystem` (v3/v4 — never `AnimationSystem`)

- `PhysicsSystem` (Phase 3, 3.6) already declares
  `DynamicChainRigCache m_rigCache;` and `GlobalPhysicsSettings m_globalSettings;`
  as real members — this phase makes them meaningful (Phase 3 stubbed
  `RegisterDynamicChains()` to a no-op; this phase gives it a real body):
  ```cpp
  void PhysicsSystem::RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
  {
      const DynamicChainDetectionDefaults defaults{}; // Editor may expose overrides later.
      std::vector<DynamicChainDefinition> chains =
          DetectDynamicChains(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr, defaults);
      m_rigCache.Register(absoluteGtaPath, data.skeleton, std::move(chains)); // NOTE: copies data.skeleton - see this doc's v3 Revision Notice.
  }

  void PhysicsSystem::AttachDynamicChainRigIfNeeded(Registry& registry, Entity rootEntity, const std::string& absoluteGtaPath)
  {
      const DynamicChainRigCache::ModelEntry* model = m_rigCache.TryGet(absoluteGtaPath);
      if (model == nullptr || model->chains.empty()) {
          return; // Nothing detected for this model - no DynamicChainRig needed.
      }
      DynamicChainRig& rig = registry.AddComponent<DynamicChainRig>(rootEntity);
      rig.meshGtaPath = absoluteGtaPath;
      rig.chainStates.resize(model->chains.size()); // one default-constructed DynamicChainRuntimeState per detected chain.
  }
  ```
  (If `SkinnedMeshData` — `SkeletalRigCache.h` — does not yet carry a
  `PhysicsData` member, add one now: `std::optional<PhysicsData> physics;`,
  populated wherever `SkinnedMeshData` is first built from a loaded `*.gta`'s
  rig metadata, since `RigFile.h`/`PmxLoader.h` already extract
  `PhysicsData` today — check `RigFile.h`'s round-trip and thread it through
  if it isn't already reaching `SkinnedMeshData`.)
- `Game::CreateMeshEntityFromGtaFile()` (`Game.cpp`) calls both new methods
  directly on `m_physicsSystem`, ALONGSIDE (never through)
  `m_animationSystem.RegisterSkinnedMesh()` — Phase 3's stubbed call site
  already has these two lines; this phase is what makes them do real work:
  ```cpp
  if (const SkinnedMeshData* skin = m_meshInstantiationSystem.TryGetSkinnedMeshData(absoluteGtaPath)) {
      m_animationSystem.RegisterSkinnedMesh(absoluteGtaPath, *skin);
      m_physicsSystem.RegisterDynamicChains(absoluteGtaPath, *skin);
      m_physicsSystem.AttachDynamicChainRigIfNeeded(m_registry, root, absoluteGtaPath);
      // ... existing RegisterGpuSkinnedMesh() call, unchanged ...
  }
  ```
  This is what makes Phase 3's `PhysicsSystem::Update()` `model == nullptr`
  early-out actually start doing real work the very next frame.

### 3.5 Editor exposure: Inspector "Dynamic Chain Physics" section

Add a new section to `src/Editor/Panels/InspectorPanel.cpp` (follow the
existing "Skeletal Animator" section's own pattern exactly — a collapsible
header shown only when the selected entity has the relevant component):
when the selected entity has a `DynamicChainRig`, show:
- An `enabled` checkbox (bound directly to `DynamicChainRig::enabled`).
- A read-only chain count/joint count summary (e.g. "3 chains, 11 joints
  total").
- Per-chain, per-joint sliders for `damping`/`stiffness`/`mass` — these
  write into the `DynamicChainDefinition` the `DynamicChainRigCache` owns for
  that model path (a live-tunable, shared-across-every-instance-of-that-
  model edit — exactly matching how `Bone`/skeleton data is already
  effectively shared per model path today; a future per-INSTANCE override
  is explicitly out of scope, see Step 4).
- Read-only display (not yet editable — see Step 4) of the current
  `GlobalPhysicsSettings` (gravity/wind), sourced from
  `PhysicsSystem::GetGlobalPhysicsSettings()` (v3/v4 — Phase 3's draft
  already declares this accessor; NEVER
  `AnimationSystem::GetGlobalPhysicsSettings()`, which does not and must not
  exist) — proves the global/local split is visible and legible to a user,
  even before a dedicated global-settings editing surface exists. The
  Inspector panel needs a reference to `PhysicsSystem` threaded through
  `EditorContext`/wherever it already reaches `AnimationSystem` for the
  existing "Skeletal Animator" section, following that same existing
  plumbing pattern.

### 3.6 Tests for this phase

- `tests/Physics/DynamicChainDetectionTests.cpp` — see 3.2.
- `tests/Game/Physics/DynamicChainRigCacheTests.cpp` (v3 — moved from
  `tests/Game/Animation/`) — trivial `Register`/`TryGet` round-trip
  (confirm both `skeleton` and `chains` round-trip correctly) + "unknown
  path returns nullptr", mirroring whatever existing test covers
  `SkeletalRigCache`'s own same shape.
- Extend `tests/Game/Physics/PhysicsSystemTests.cpp` (started in Phase
  3 v4) with a REAL, non-stub case: register a small synthetic rigged model
  (hand-built `SkeletonData` with a 2-joint `deformAfterPhysics` run)
  through `RegisterDynamicChains()` + `AttachDynamicChainRigIfNeeded()`,
  separately drive a `ResolvedAnimationPose` component onto the same entity
  (as `AnimationSystem::EvaluatePoses()` would), run several
  `PhysicsSystem::Update()` calls with a non-zero `deltaSeconds` and
  non-zero gravity, and assert the simulated joints' resulting
  `ResolvedAnimationPose::pose` entries actually DIFFER from the pure-FK
  (physics-disabled) values that were written in — the end-to-end proof
  this whole campaign exists to deliver: a physics-driven bone that visibly
  moves under gravity where a static FK bone would not. Note this test needs
  NO `AnimationSystem` involvement at all to prove this — it drives
  `ResolvedAnimationPose` directly, which is itself further proof the two
  systems are genuinely decoupled.
- Add/extend the matching descriptive paragraph for
  `DynamicChainRigCacheTests.cpp` and for the extended
  `PhysicsSystemTests.cpp` in `tests/CMakeLists.txt`'s own header "Test
  taxonomy" comment block (the `DynamicChainDetectionTests.cpp` one was
  already called out in 3.2's own test list above).

## Step 4: What We Will NOT Do

- We will **not** build a per-INSTANCE parameter override system in this
  phase — every entity spawned from the same model `*.gta` shares one
  `DynamicChainDefinition` set (tunable via the Inspector, per 3.5), exactly
  mirroring how `SkeletalRigCache`'s `SkinnedMeshData` is already shared
  per-path today. A future "override just this one instance's wind
  strength" feature is a deliberately separate, unstarted follow-up.
- We will **not** build an Editor control surface for editing
  `GlobalPhysicsSettings` itself in this phase (gravity/wind sliders) —
  3.5 only makes it visible/read-only; wiring live editing is a small,
  explicitly-deferred follow-up (should the need arise, add a
  "Physics"/"World" panel rather than bolting world-level controls onto the
  per-entity Inspector).
- We will **not** attempt to auto-detect chains from bone NAME conventions
  (e.g. matching a specific-use-case substring in the bone's name) —
  `deformAfterPhysics`/`RigidBody::motionType` are reliable, already-imported,
  engine-native signals; name-matching would be fragile and redundant.
- We will **not** add a JSON/asset-file authoring format for hand-crafting
  chains on a model with no PMX physics data at all — if that need arises
  later, it is its own follow-up campaign, not silently folded in here.
- **(v3)** We will **not** let `DynamicChainRigCache`/`GlobalPhysicsSettings`
  become members of `AnimationSystem` again, under any circumstance —
  including a future "it would be more convenient to just reach into
  AnimationSystem's cache" temptation. If a genuine future need arises for
  the two systems to share more data, add a THIRD, independent, read-only
  cache both systems reference (owned by neither), never make one system
  own the other's state.

## Step 5: Their Role

1. Add `GlobalPhysicsSettings.h` (3.1) — it already compiles into
   `PhysicsSystem`'s stubbed member from Phase 3; this step just gives
   the struct its real fields.
2. Implement and test `DetectDynamicChains()` (3.2) in complete isolation
   first — this is the single trickiest piece of new logic in this phase
   (tree-walking with branch-splitting), and it has zero ECS/GPU dependency,
   so get it fully correct and tested before touching `PhysicsSystem`.
3. Implement `DynamicChainRigCache` (3.3) under `src/Game/Physics/` — trivial,
   mirrors existing code, but double-check it does NOT get placed under
   `src/Game/Animation/` (that would be the exact ownership regression this
   revision exists to prevent).
4. Wire `RegisterDynamicChains()`/`AttachDynamicChainRigIfNeeded()` +
   the real `Game.cpp` call site (3.4) — this is what turns Phase 3's inert
   stub into a live feature, entirely inside `PhysicsSystem`, never
   touching `AnimationSystem.h/.cpp` at all.
5. Add the Inspector section (3.5) — confirms, visually, that a real MMD
   model with a physics-driven bone run spawned in the Editor now has
   tunable damping/stiffness/weight sliders, and that the previously-empty
   `DynamicChainRig` component is now populated with real chain state.
6. Land the end-to-end test from 3.6 last, once everything above compiles —
   it is this campaign's actual proof of value, and (per 3.6's own note) it
   should visibly need no `AnimationSystem` involvement to pass.
