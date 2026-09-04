# PHASE3 — Pipeline Integration and Fixed Timestep (v4 — ECS-independent, generic naming)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprits B, C, D). Depends on: Phase 2
(`DynamicChainSolver.h`, `BoneChainPhysicsResolver.h`, both compiling and
tested). Modifies: `src/Game/Animation/AnimationSystem.h/.cpp`,
`src/Game/Game.h/.cpp`. Adds: `src/ECS/Components/ResolvedAnimationPose.h`,
`src/ECS/Components/DynamicChainRig.h`, `src/Physics/FixedTimestepAccumulator.h/.cpp`,
`src/Animation/AnimationPoseEvaluator.h/.cpp`'s new overload, and a brand-new,
fully independent `src/Game/Physics/PhysicsSystem.h/.cpp`. This is the
phase where a dynamic bone chain first visibly swings on screen.

## v4 Revision Notice (naming only, read this first)

This document was v3 when it was last revised for genuine ECS
independence (see the "v3 Revision Notice" immediately below, kept intact
for history). v4 makes exactly one further kind of change on top of that:
every hair-specific identifier this phase introduces is renamed to a
generic equivalent, per `PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes
(v4)" — `HairPhysicsSystem` → `PhysicsSystem`, `HairPhysicsRig` (ECS
component) → `DynamicChainRig`, `HairPhysicsRigCache` →
`DynamicChainRigCache`, `RegisterHairPhysicsChains()` →
`RegisterDynamicChains()`, `AttachHairPhysicsRigIfNeeded()` →
`AttachDynamicChainRigIfNeeded()`. No ordering, wiring, or ownership
decision described below changed — only names.

## v3 Revision Notice (ECS independence — kept for history)

This document's v1/v2 revisions wired the bone-chain physics INLINE, as a
block of code appended directly inside `AnimationSystem::Update()`'s own
per-animator loop, with `AnimationSystem` itself owning the chain cache
(`m_chainRigCache`) and global settings (`m_globalPhysicsSettings`), and the
pre-physics `pose` living only as a local stack variable passed by reference
between inline steps. A design review (done by reading the actual, current
`src/Game/Animation/AnimationSystem.cpp`/`.h` source tree, not just this
document) found that this does NOT deliver genuine ECS system independence:
it makes `AnimationSystem` permanently aware of this physics system existing
at all (an `#include "../../Physics/DynamicChainSolver.h"` sitting inside
the animation orchestrator, a physics cache as one of its private members),
and it makes "physics" and "animation" a single, inseparable call
(`AnimationSystem::Update()`) rather than two independently callable,
independently testable, independently schedulable systems.

**v3 fixes this by introducing one new ECS component,
`ResolvedAnimationPose` (3.1 below), as the ENTIRE hand-off contract between
three independent stages `Game::Update()` now calls in a fixed order:**

```cpp
// Game::Update() (src/Game/Game.cpp), v3/v4:
void Game::Update(double deltaSeconds, const InputState& input)
{
    m_animationSystem.EvaluatePoses(m_registry, deltaSeconds);   // ANIMATION -
        // samples motion, solves IK, applies append inheritance, writes the
        // result into every animated entity's ResolvedAnimationPose
        // component. Has ZERO knowledge that this physics system exists
        // anywhere in this engine - no #include of anything under src/Physics/.

    m_physicsSystem.Update(m_registry, deltaSeconds);            // PHYSICS -
        // reads ResolvedAnimationPose's bone translations for whichever
        // bones its own detected chains simulate, runs the fixed-timestep
        // Verlet solve, OVERWRITES just those physics-controlled bone
        // entries in that SAME component, in place. Has ZERO knowledge that
        // motion sampling/IK/append/skinning/GPU upload exist - no
        // #include of MotionSampler.h/IkSolver.h/AppendBoneSolver.h/
        // AnimationPoseEvaluator.h/VertexSkinning.h/Renderer/Mesh anywhere.

    m_animationSystem.SkinAndUpload(m_registry);                 // SKINNING -
        // reads whatever ResolvedAnimationPose currently holds (physics-
        // adjusted or not - it does not, and must not, care which) and does
        // the actual vertex skin + GPU upload, exactly as today.
}
```

Animation -> Physics -> Skinning, as three separate, independently
schedulable calls, communicating ONLY through the `ResolvedAnimationPose`
component - never through a shared private cache, never through one calling
the other's methods directly. This is the shape the rest of this document
now describes; every numbered step below has been rewritten (not merely
patched) against this shape. Phase 2's deliverables
(`DynamicChainDefinition`/`DynamicChainRuntimeState`/`DynamicChainSolver`/
`BoneChainPhysicsResolver`, `Animation/BoneWorldMatrixQuery.h`) are entirely
unaffected by this revision - they were already pure, ECS-free functions;
only WHO calls them changes (`PhysicsSystem::Update()` instead of
`AnimationSystem::Update()`).

## Step 1: The Goal

Make `Game::Update()` actually invoke Phase 2's bridge for every model that
has at least one detected dynamic bone chain, with: (a) a
correctness-critical, explicitly documented, genuinely-independent physics
STAGE rather than a hack bolted onto an existing system's internals, (b)
simulation state that survives frame-to-frame without being reset or
duplicated, (c) a fixed timestep so the simulation is stable regardless of
real, variable frame time, and (d) **true ECS decoupling**: `AnimationSystem`
and the new `PhysicsSystem` must be two classes that neither `#include`s the
other, neither calls the other's methods, and neither knows the other
exists - the only thing they share is the shape of one ECS component.

## Step 2: The Situation / The Problem

Recall Culprits B/C/D from `PHASE0`, re-examined here against the
independence requirement:

- `Animation/AnimationPoseEvaluator.h`'s `EvaluateAnimatedSkinningPose()` is
  a fixed, four-call, documented-as-correctness-critical sequence:
  `SampleAnimationPose() → SolveIkChains() → ApplyAppendInheritance() →
  ComputeSkinningMatrices()`. It is a **pure** function (no persistent
  state, no ECS) - it cannot own the new mutable `DynamicChainRuntimeState`
  itself, and (v3/v4) it must not even be the thing that DECIDES whether
  physics runs - that decision belongs entirely to `PhysicsSystem`,
  reached later, through the ECS, not through this function's own call
  graph.
- `AnimationSystem::Update(Registry& registry, double deltaSeconds)`
  currently does FOUR things in one strictly-sequential per-animator loop:
  advance playback time, evaluate the animated pose, run CPU/GPU vertex
  skinning, and upload to the GPU. Per `AnimationSystem.cpp`'s own prominent
  loop comment (*"THIS OUTER LOOP MUST REMAIN STRICTLY SEQUENTIAL"*), that
  sequencing exists ONLY because two entities spawned from the same model
  share GPU mesh buffers - a concern that applies to the skinning/upload
  half of the work, not to pose evaluation, and has NOTHING to do with this
  physics system at all. Bolting this physics stepping into the middle of
  this one loop (v1/v2's approach) conflated two unrelated sequencing
  concerns. (v3/v4) Splitting `Update()` into `EvaluatePoses()` (no
  GPU/Renderer touch at all - safe to reason about independently of the
  shared-buffer rule) and `SkinAndUpload()` (identical to today's
  GPU-touching half, unchanged) resolves this cleanly, and creates the exact
  seam `PhysicsSystem::Update()` needs to run between them.
- `deltaSeconds` reaching `Game::Update()` is real frame time - directly
  feeding it into `StepDynamicChain()`'s `fixedDeltaTime` parameter would
  violate Phase 1/2's own stability assumptions; `PhysicsSystem` (not
  `AnimationSystem`) owns the fixed-timestep accumulator that fixes this,
  since fixed-timestepping is a physics-only concern.

## Step 3: The Plan

### 3.1 New ECS component: `src/ECS/Components/ResolvedAnimationPose.h`

This is the entire hand-off contract described in the v3 Revision Notice
above - the ECS-native replacement for what used to be a `pose` local
variable threaded by hand between inline steps of one function:

```cpp
#pragma once
#include "../../Animation/BoneLocalOffset.h"

#include <vector>

namespace gte {

// Written EXCLUSIVELY by AnimationSystem::EvaluatePoses() - the sample ->
// IK -> append -> FK stage (Animation/AnimationPoseEvaluator.h's new
// EvaluateAnimatedPoseBeforePhysics(), see 3.3 below) - once per playing
// SkeletalAnimator, every frame, always OVERWRITING `pose` wholesale (never
// reading a previous frame's leftover value first).
//
// Then, OPTIONALLY, overwritten bone-by-bone, IN PLACE, by
// PhysicsSystem::Update() (src/Game/Physics/PhysicsSystem.h) - the
// only stage that ever mutates individual elements of an EXISTING `pose`
// rather than replacing it wholesale, and only for the specific
// `DynamicChainDefinition::jointBoneIndices` its own detected chains name
// (see Physics/BoneChainPhysicsResolver.h's ApplyDynamicChainPhysicsToPose()).
//
// Finally, only ever READ by AnimationSystem::SkinAndUpload() (see 3.5
// below), which does not know or care whether physics touched it.
//
// This component IS the entire coupling surface between the three
// independent stages Game::Update() runs in a fixed order (Animation ->
// Physics -> Skinning, see PHASE3's own v3 Revision Notice) - no stage
// calls another stage's code directly, and no stage holds a pointer/
// reference into another stage's private cache. Attached to the SAME
// entity SkeletalAnimator/DynamicChainRig live on (a model's ROOT/animator
// entity - see MeshAssetSource.h/SkeletalAnimator.h).
struct ResolvedAnimationPose {
    std::vector<BoneLocalOffset> pose;
};

} // namespace gte
```

Add `src/ECS/Components/ResolvedAnimationPose.h` to `CMakeLists.txt`.

### 3.2 New ECS component: `src/ECS/Components/DynamicChainRig.h`

Follows `SkeletalAnimator.h`'s own precedent (plain data, a path-string-keyed
link back into a system-owned cache, no live pointers stored in the
component itself) - with one deliberate, v3-specific change from the
original v1/v2 draft: this component carries its **own**
`meshGtaPath`, rather than `PhysicsSystem` reaching into a
`SkeletalAnimator` component to read ITS `meshGtaPath`. This is not
redundancy for its own sake - it is what lets `PhysicsSystem` process an
entity knowing only "this entity has a `DynamicChainRig` and (hopefully) a
`ResolvedAnimationPose`," with no assumption that a `SkeletalAnimator`
component (an `Animation`/`AnimationSystem`-owned shape) even exists on it.
**Zero cross-system component reach-through** is a hard rule for this
component, not a style preference - see Step 4.

```cpp
#pragma once
#include "../../Physics/DynamicChainRuntimeState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Attached to a model's ROOT entity (same entity SkeletalAnimator lives on -
// see MeshAssetSource.h/SkeletalAnimator.h) whenever
// PhysicsSystem::RegisterDynamicChains() (see PHASE4) detects at least one
// physics-driven bone chain in that model's SkeletonData.
struct DynamicChainRig {
    // Self-contained lookup key back into PhysicsSystem's OWN
    // DynamicChainRigCache (Game/Physics/DynamicChainRigCache.h, see PHASE4) -
    // set once, at registration time, mirroring SkeletalAnimator::
    // meshGtaPath's own "component stores a stable string key, never a live
    // pointer" convention. A deliberate, small, per-entity duplication of a
    // string SkeletalAnimator's own component also happens to carry -
    // accepted in exchange for PhysicsSystem never needing to know
    // SkeletalAnimator's shape exists.
    std::string meshGtaPath;

    // Index-aligned 1:1 with whatever DynamicChainRigCache::TryGet(meshGtaPath)
    // returns (see PHASE4) - one runtime state per chain in that model.
    std::vector<DynamicChainRuntimeState> chainStates;

    // Fixed-timestep accumulator (Culprit D) - carries "leftover" simulation
    // time across frames so the simulation steps at a constant rate
    // regardless of the real, variable render frame rate. See
    // FixedTimestepAccumulator.h (3.3) for the stepping math this drives.
    float accumulatedSeconds = 0.0f;

    bool enabled = true; // lets PHASE4's Inspector toggle disable physics per-instance without removing the component.
};

} // namespace gte
```

Add `src/ECS/Components/DynamicChainRig.h` to `CMakeLists.txt`.

### 3.3 `src/Physics/FixedTimestepAccumulator.h` (new, pure math) - unchanged from v1/v2

A small, standalone, Tier-1-testable helper — deliberately NOT baked
directly into any system's body, so its edge cases (a huge frame-time
spike, a near-zero delta) can be unit-tested without any ECS/Registry
involvement at all. (v3/v4 note: this is now owned/called by
`PhysicsSystem::Update()`, never by `AnimationSystem` - a purely
mechanical change of caller, the function itself is identical.)

```cpp
#pragma once

namespace gte {

// How many fixed-size physics steps to run this frame, given how much real
// time elapsed and how much "leftover" time is already banked from previous
// frames - the standard "accumulator pattern" every fixed-timestep game
// loop tutorial documents (e.g. Glenn Fiedler's "Fix Your Timestep!"),
// applied here narrowly to dynamic-chain stepping rather than the whole
// engine's frame loop (Application::Run() itself stays variable-timestep -
// this is scoped ONLY to Physics/ stepping).
//
// `accumulatedSeconds` (in/out) is DynamicChainRig::accumulatedSeconds -
// incremented by `frameDeltaSeconds`, then decremented by `fixedTimestep`
// once per returned step. `maxStepsPerFrame` bounds the return value (a
// spiral-of-death guard - e.g. a 5-second Editor breakpoint pause must
// never demand 300 catch-up steps in one call); any leftover time beyond
// what `maxStepsPerFrame` can drain is simply DISCARDED (accumulatedSeconds
// clamped back down), which is the standard, accepted trade for this
// pattern - the simulation is allowed to visibly "skip ahead" after a huge
// stall, it must never spend seconds of real time catching up in a single
// Update() call.
int ComputeFixedStepCount(float& accumulatedSeconds, float frameDeltaSeconds, float fixedTimestep, int maxStepsPerFrame) noexcept;

} // namespace gte
```

Implementation:
```cpp
#include "FixedTimestepAccumulator.h"
#include <algorithm>

namespace gte {

int ComputeFixedStepCount(float& accumulatedSeconds, float frameDeltaSeconds, float fixedTimestep, int maxStepsPerFrame) noexcept
{
    if (fixedTimestep <= 0.0f) {
        return 0;
    }
    accumulatedSeconds += frameDeltaSeconds;

    int steps = 0;
    while (accumulatedSeconds >= fixedTimestep && steps < maxStepsPerFrame) {
        accumulatedSeconds -= fixedTimestep;
        ++steps;
    }
    // Spiral-of-death guard: never let unboundedly-large leftover time keep
    // demanding more steps next frame either.
    if (steps == maxStepsPerFrame) {
        accumulatedSeconds = std::min(accumulatedSeconds, fixedTimestep);
    }
    return steps;
}

} // namespace gte
```

Add to `CMakeLists.txt` + `tests/Physics/FixedTimestepAccumulatorTests.cpp`:
a normal 60fps-ish delta with `fixedTimestep = 1/60` yields exactly one step
most calls; a delta of exactly `2 * fixedTimestep` yields exactly two steps
and zero leftover; a huge delta (e.g. 5 seconds) is clamped to
`maxStepsPerFrame` and leaves `accumulatedSeconds <= fixedTimestep`
afterward (never unbounded); a delta smaller than `fixedTimestep` yields
zero steps and simply accumulates.

### 3.4 Extend the pipeline: `Animation/AnimationPoseEvaluator.h`/`.cpp` - unchanged from v1/v2

Per Culprit B, the existing four-call order must stay intact and
untouched — a 5th, explicitly-optional stage is APPENDED, not interleaved,
and (v3/v4) that 5th stage is not even a call this file makes or knows about
- it is simply whatever `PhysicsSystem::Update()` does to the
`ResolvedAnimationPose` component LATER, in a totally separate function
call. Add a new overload (never remove the existing one — every other
current caller keeps compiling unchanged):

```cpp
// Existing, unchanged:
std::vector<Mat4> EvaluateAnimatedSkinningPose(
    const SkeletonData& skeleton, const ResolvedAnimationBinding& binding, float frame);

// NEW - Phase 3. Runs the exact same four-stage sequence as the overload
// above, but STOPS one step short of the final ComputeSkinningMatrices()
// call and hands the caller back the intermediate, pre-physics `pose`
// (sample -> IK -> append, already resolved) instead of finished skinning
// matrices - giving a caller exactly the hook point Culprit B's fixed
// pipeline was missing. The caller (AnimationSystem::EvaluatePoses(), see
// 3.5) writes this return value straight into the calling entity's
// ResolvedAnimationPose::pose (3.1) and does nothing else with it -
// physics-stepping and skinning both happen in later, separate functions.
std::vector<BoneLocalOffset> EvaluateAnimatedPoseBeforePhysics(
    const SkeletonData& skeleton, const ResolvedAnimationBinding& binding, float frame);
```

In `AnimationPoseEvaluator.cpp`: extract the existing body's first three
calls (`SampleAnimationPose`/`SolveIkChains`/`ApplyAppendInheritance`) into
`EvaluateAnimatedPoseBeforePhysics()`, then reimplement
`EvaluateAnimatedSkinningPose()` as:
```cpp
std::vector<Mat4> EvaluateAnimatedSkinningPose(const SkeletonData& skeleton, const ResolvedAnimationBinding& binding, float frame)
{
    const std::vector<BoneLocalOffset> pose = EvaluateAnimatedPoseBeforePhysics(skeleton, binding, frame);
    return ComputeSkinningMatrices(skeleton, pose);
}
```
This is a pure, behavior-preserving refactor — every existing caller and
every existing `tests/Animation/AnimationPoseEvaluatorTests.cpp` test must
still pass unmodified; add exactly one new regression test asserting
`EvaluateAnimatedSkinningPose(...) == ComputeSkinningMatrices(skeleton,
EvaluateAnimatedPoseBeforePhysics(...))` for a fixed fixture, proving the two
functions can never silently diverge later.

### 3.5 Split `AnimationSystem::Update()` into `EvaluatePoses()` + `SkinAndUpload()`

This is the core v3 structural change inside `AnimationSystem` itself.
`AnimationSystem.h`'s single `void Update(Registry&, double deltaSeconds);`
is REPLACED (not overloaded) by two public methods:

```cpp
// Advances every playing SkeletalAnimator's frame, evaluates its pose
// (sample -> IK -> append -> FK-ready BoneLocalOffset array, via
// Animation/AnimationPoseEvaluator.h's EvaluateAnimatedPoseBeforePhysics()),
// and writes the result into that entity's ResolvedAnimationPose component
// (adding the component the first time an entity is seen). Touches NO
// Renderer/Mesh/GPU state whatsoever - #include "../../Physics/*" does NOT
// appear anywhere in this class anymore; this method has zero knowledge
// that this physics system exists. Safe to call before PhysicsSystem::Update()
// every frame, in Game::Update() - see PHASE3's own v3 Revision Notice.
void EvaluatePoses(Registry& registry, double deltaSeconds);

// Reads whatever ResolvedAnimationPose::pose currently holds for every
// playing SkeletalAnimator (physics-adjusted by PhysicsSystem or not -
// this method does not, and must not, branch on which) and performs the
// exact same CPU/GPU vertex skinning + GPU upload work
// AnimationSystem::Update() used to do inline, byte-for-byte unchanged
// (mode branch, scratch-buffer reuse, MeshAssetPart grouping/packing,
// GpuSkinningRigCache upload). An entity with no ResolvedAnimationPose yet
// (e.g. EvaluatePoses() skipped it this frame because it wasn't playing) is
// simply skipped here too - the identical "not playing -> continue" guard
// EvaluatePoses() itself applies.
void SkinAndUpload(Registry& registry);
```

`Update(Registry&, double)` itself is DELETED from `AnimationSystem`'s
public interface - `Game::Update()` (3.7 below) calls the two new methods
directly, with `PhysicsSystem::Update()` sandwiched between them. (If
any other call site outside `Game.cpp` still calls the old `Update()`,
update it to call both new methods back-to-back with nothing in between -
grep confirms `Game.cpp` is the only call site today.)

Implementation notes for `AnimationSystem.cpp`:

- `EvaluatePoses()` is the FIRST HALF of today's per-animator loop body:
  the frame-advance/looping logic (`animator.frame += ...`, the
  loop/clamp-to-`lastFrame` branch) plus the
  `EvaluateAnimatedPoseBeforePhysics(...)` call, followed by:
  ```cpp
  ResolvedAnimationPose& resolvedPose = registry.HasComponent<ResolvedAnimationPose>(animatorEntity)
      ? *registry.TryGetComponent<ResolvedAnimationPose>(animatorEntity)
      : registry.AddComponent<ResolvedAnimationPose>(animatorEntity);
  resolvedPose.pose = std::move(pose);
  ```
  (Use whatever `Registry` idiom `AGENTS.md`'s ECS section documents for
  "get-or-add" - if `Registry` has no single `GetOrAddComponent<T>()` helper
  yet, `TryGetComponent` + `AddComponent` on a null result is the existing
  pattern `AnimationSystem::Play()` already uses for `SkeletalAnimator`.)
  This method touches `m_rigCache`/`m_clipCache`/`m_bindingCache` exactly as
  before - those three caches stay on `AnimationSystem`, they are pure
  animation-authoring data, nothing to do with physics.
- `SkinAndUpload()` is the SECOND HALF of today's loop body: everything
  from `const GpuSkinningRigCache::GpuModelEntry* gpuEntry = ...` onward,
  unchanged, except it no longer receives `skinningMatrices` from a local
  variable computed earlier in the SAME loop iteration - instead:
  ```cpp
  const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(animatorEntity);
  if (resolvedPose == nullptr) {
      continue; // EvaluatePoses() hasn't produced a pose for this entity (yet, or this frame) - nothing to skin.
  }
  const std::vector<Mat4> skinningMatrices = ComputeSkinningMatrices(skinData->skeleton, resolvedPose->pose);
  ```
- Both methods keep their OWN copy of the existing outer-loop guard
  (`if (!animator.playing || animator.animationGtaPath.empty()) { continue; }`)
  and both still iterate `registry.Storage<SkeletalAnimator>()` - this is
  now TWO separate full passes over that storage per frame instead of one
  combined pass, which is a deliberate, accepted trade for genuine stage
  independence (each pass is O(animator count), not O(animator count²); the
  cost of a second pass is the same iteration already paid twice by the
  Editor's own Hierarchy/Inspector code elsewhere in this engine).
- The *** THIS OUTER LOOP MUST REMAIN STRICTLY SEQUENTIAL *** rule (shared
  GPU mesh buffers) applies ONLY to `SkinAndUpload()`'s loop now -
  `EvaluatePoses()` touches no Renderer/Mesh state at all, so nothing stops
  a future optimization pass from parallelizing IT across animators (not
  attempted in this phase - noted for Phase 5 to consider, see that
  document's own Step 4).
- `#include "../../Physics/..."` must NOT appear anywhere in
  `AnimationSystem.h`/`.cpp` after this change. If a diff introduces one,
  the split was done wrong - `AnimationSystem` must compile and link with
  zero knowledge that `src/Physics/`/`PhysicsSystem` exist.

### 3.6 New, fully independent `src/Game/Physics/PhysicsSystem.h`/`.cpp`

The direct, ECS-native replacement for v1/v2's inline block inside
`AnimationSystem::Update()`. Lives alongside `AnimationSystem`/`RenderSystem`/
`MeshInstantiationSystem` under `src/Game/` (it is a `Game`-layer
orchestrator, exactly like them), but — unlike those three — it is
DELIBERATELY NOT in `AGENTS.md`'s "systems allowed to depend on both ECS and
Renderer" list, because it never touches `Renderer`/`Mesh`/`Pipeline` at
all; it only reads/writes ECS components and calls pure `src/Physics/`/
`Animation/BoneWorldMatrixQuery.h` functions. (v3 addition - Step 3.8 below
adds the one small `AGENTS.md` clarification this creates.)

```cpp
#pragma once
#include "../../ECS/Registry.h"
#include "DynamicChainRigCache.h"
#include "../../Physics/GlobalPhysicsSettings.h"

#include <string>

namespace gte {
struct SkinnedMeshData; // Assets/SkeletalRigCache-adjacent forward decl - see SkeletalRigCache.h.

// The secondary-motion, Verlet-based dynamic-bone-chain physics
// orchestrator - the direct analog of AnimationSystem, but for physics
// instead of animation, and with NO dependency on AnimationSystem/
// Animation/MotionSampler.h/IkSolver.h/AppendBoneSolver.h/
// AnimationPoseEvaluator.h/VertexSkinning.h/Renderer/Mesh anywhere in this
// class. Owns its own DynamicChainRigCache (chain definitions + a private
// copy of each registered model's SkeletonData, see
// DynamicChainRigCache.h/PHASE4) and its own GlobalPhysicsSettings
// (gravity/wind/fixed-timestep/max-steps) - NEITHER of these lives on
// AnimationSystem, unlike v1/v2's original draft; see this file's own
// header comment for why the small resulting SkeletonData duplication
// (once in AnimationSystem::m_rigCache, once here) is an accepted, small
// trade for genuine system independence.
class PhysicsSystem {
public:
    // Mirrors AnimationSystem::RegisterSkinnedMesh()'s own shape and
    // calling convention (called from the SAME Game::CreateMeshEntityFromGtaFile()
    // hand-off site, ALONGSIDE - never through - AnimationSystem::RegisterSkinnedMesh(),
    // see PHASE4, 3.4). Detects dynamic bone chains
    // (Physics/DynamicChainDetection.h) and caches them (plus a copy of
    // `data.skeleton`) keyed by `absoluteGtaPath`. A no-op (registers zero
    // chains) for a model with none detected - see PHASE4.
    void RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data);

    // If this model has at least one detected chain, attaches a
    // DynamicChainRig component (sized to match) to `rootEntity` - called
    // right after RegisterDynamicChains() at the same Game.cpp call
    // site (see PHASE4, 3.4). A no-op if zero chains were detected.
    void AttachDynamicChainRigIfNeeded(Registry& registry, Entity rootEntity, const std::string& absoluteGtaPath);

    // For every entity carrying an ENABLED DynamicChainRig AND a
    // ResolvedAnimationPose (added earlier THIS SAME FRAME by
    // AnimationSystem::EvaluatePoses() - see Game::Update()'s fixed call
    // order, PHASE3's own v3 Revision Notice): fixed-timestep-accumulates
    // `deltaSeconds`, then for each of that entity's detected chains, steps
    // Phase 2's DynamicChainSolver the resulting number of times and rewrites
    // the physics-controlled bone entries of ResolvedAnimationPose::pose in
    // place via BoneChainPhysicsResolver's ApplyDynamicChainPhysicsToPose().
    // An entity with no ResolvedAnimationPose yet this frame (AnimationSystem
    // skipped a non-playing animator) is simply skipped - degrade
    // gracefully, never assume the component exists.
    void Update(Registry& registry, double deltaSeconds);

    const GlobalPhysicsSettings& GetGlobalPhysicsSettings() const noexcept { return m_globalSettings; }
    GlobalPhysicsSettings& GetGlobalPhysicsSettings() noexcept { return m_globalSettings; }

private:
    DynamicChainRigCache m_rigCache;
    GlobalPhysicsSettings m_globalSettings;
};

} // namespace gte
```

`.cpp` implementation of `Update()` (the direct successor of v1/v2's inline
block, now standalone and reading/writing ONLY through ECS components):

```cpp
#include "PhysicsSystem.h"

#include "../../Animation/BoneWorldMatrixQuery.h"
#include "../../ECS/Components/DynamicChainRig.h"
#include "../../ECS/Components/ResolvedAnimationPose.h"
#include "../../Physics/FixedTimestepAccumulator.h"
#include "../../Physics/DynamicChainSolver.h"
#include "../../Physics/BoneChainPhysicsResolver.h"
#include "../../Profiling/ScopeTimer.h"

namespace gte {

void PhysicsSystem::Update(Registry& registry, double deltaSeconds)
{
    GTE_PROFILE_SCOPE("PhysicsSystem::Update");

    ComponentStorage<DynamicChainRig>& rigs = registry.Storage<DynamicChainRig>();
    for (std::size_t i = 0; i < rigs.Size(); ++i) {
        DynamicChainRig& rig = rigs.ComponentAt(i);
        if (!rig.enabled) {
            continue;
        }
        const Entity entity = rigs.EntityAt(i);

        ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        if (resolvedPose == nullptr) {
            continue; // Nothing to overwrite - AnimationSystem::EvaluatePoses() hasn't produced a pose for this entity this frame.
        }

        const DynamicChainRigCache::ModelEntry* model = m_rigCache.TryGet(rig.meshGtaPath);
        if (model == nullptr || model->chains.size() != rig.chainStates.size()) {
            continue; // Not (yet) registered, or stale - degrade gracefully.
        }

        std::vector<BoneLocalOffset>& pose = resolvedPose->pose;

        const int stepCount = ComputeFixedStepCount(rig.accumulatedSeconds, static_cast<float>(deltaSeconds),
            m_globalSettings.fixedTimestepSeconds, m_globalSettings.maxStepsPerFrame);
        if (stepCount <= 0) {
            continue;
        }

        for (std::size_t chainIndex = 0; chainIndex < model->chains.size(); ++chainIndex) {
            const DynamicChainDefinition& chain = model->chains[chainIndex];
            DynamicChainRuntimeState& state = rig.chainStates[chainIndex];

            // Captured ONCE, from `pose` EXACTLY as EvaluatePoses() left it
            // this frame, BEFORE any substep below mutates `pose` in place -
            // see PHASE0's Revision Notes finding #4. Never re-read inside
            // the substep loop.
            const Mat4 rootWorld = ComputeBoneWorldMatrix(model->skeleton, pose, chain.rootBoneIndex);
            const Vec3 rootWorldPos = rootWorld.TransformPoint(Vec3::Zero());

            std::vector<Vec3> animatedJointWorldPositions;
            animatedJointWorldPositions.reserve(chain.jointBoneIndices.size());
            for (std::int32_t boneIndex : chain.jointBoneIndices) {
                animatedJointWorldPositions.push_back(
                    ComputeBoneWorldMatrix(model->skeleton, pose, boneIndex).TransformPoint(Vec3::Zero()));
            }

            for (int step = 0; step < stepCount; ++step) {
                StepDynamicChain(chain, rootWorldPos, animatedJointWorldPositions, state,
                    m_globalSettings.fixedTimestepSeconds, m_globalSettings.gravity, m_globalSettings.wind);

                std::vector<Vec3> simulatedPositions;
                simulatedPositions.reserve(state.particles.size());
                for (const VerletParticle& particle : state.particles) {
                    simulatedPositions.push_back(particle.position);
                }
                ApplyDynamicChainPhysicsToPose(model->skeleton, chain, simulatedPositions, pose);
            }
        }
    }
}

} // namespace gte
```

Note this file's `#include` list: **no** `Animation/AnimationPoseEvaluator.h`,
`MotionSampler.h`, `IkSolver.h`, `AppendBoneSolver.h`, `VertexSkinning.h`,
`ECS/Components/SkeletalAnimator.h`, `Renderer/*`, `Mesh.h`, `RenderSystem.h`,
or `MeshInstantiationSystem.h` — this is the literal, checkable proof of
"zero knowledge of motion sampling/IK/append/skinning" the user's own
architecture sketch asked for. `Animation/BoneWorldMatrixQuery.h` is the one
`Animation/` header this file DOES include, and that is fine: it is a pure,
promoted, engine-wide utility (Phase 2, Culprit E) with no dependency on
`AnimationSystem`/`SkeletalAnimator`/the sampling-IK-append pipeline itself
- exactly the same shared primitive `IkSolver.cpp` also calls.

`DynamicChainRigCache` itself (its `ModelEntry` carrying BOTH
`std::vector<DynamicChainDefinition> chains` and a `SkeletonData skeleton`
copy) is specified fully in `PHASE4` (3.3/3.4 there, updated for this v3/v4
revision) since real chain detection is out of THIS phase's scope — for
Phase 3 itself, stub `DynamicChainRigCache`/`GlobalPhysicsSettings` exactly
as v1/v2 always intended: `RegisterDynamicChains()`/
`AttachDynamicChainRigIfNeeded()` are no-ops, `m_rigCache.TryGet(...)` always
returns `nullptr`, so `PhysicsSystem::Update()` above is a **provable
no-op** for every entity in this phase (nothing has a `DynamicChainRig`
component yet), while still compiling and being exercised by this phase's
own tests (3.9 below). Phase 4 is what makes it real.

Add `src/Game/Physics/PhysicsSystem.h/.cpp` and (stub for now, real in
Phase 4) `src/Game/Physics/DynamicChainRigCache.h` to `CMakeLists.txt`.

### 3.7 Wire the three stages into `Game::Update()`

`src/Game/Game.h`: add `PhysicsSystem m_physicsSystem;` as a new
member, alongside the existing `AnimationSystem m_animationSystem;`
(constructed independently - `PhysicsSystem` takes no constructor
dependencies today, unlike `AnimationSystem`, which needs `RenderSystem&`/
`MeshInstantiationSystem&`).

`src/Game/Game.cpp`:
```cpp
// BEFORE (existing, single call):
//   m_animationSystem.Update(m_registry, deltaSeconds);

// AFTER (Phase 3, v3/v4):
m_animationSystem.EvaluatePoses(m_registry, deltaSeconds);
m_physicsSystem.Update(m_registry, deltaSeconds);
m_animationSystem.SkinAndUpload(m_registry);
```

`Game::CreateMeshEntityFromGtaFile()` also gains one new call, alongside
(never instead of) its existing `RegisterSkinnedMesh()`/
`RegisterGpuSkinnedMesh()` calls (real wiring lands in Phase 4; for this
phase, calling the stubbed version below is enough to prove the plumbing
compiles and is genuinely inert):
```cpp
if (const SkinnedMeshData* skin = m_meshInstantiationSystem.TryGetSkinnedMeshData(absoluteGtaPath)) {
    m_animationSystem.RegisterSkinnedMesh(absoluteGtaPath, *skin);
    m_physicsSystem.RegisterDynamicChains(absoluteGtaPath, *skin);        // NEW - Phase 3/4.
    m_physicsSystem.AttachDynamicChainRigIfNeeded(m_registry, root, absoluteGtaPath); // NEW - Phase 3/4.
    // ... existing RegisterGpuSkinnedMesh() call, unchanged ...
}
```

### 3.8 One small `AGENTS.md` clarification (v3 addition)

`AGENTS.md`'s "Entity-Component-System (ECS)" section currently states that
"Only `RenderSystem`, `MeshInstantiationSystem`, and `AnimationSystem` are
allowed to depend on both the ECS world AND `Renderer`/`Mesh`/`Pipeline`."
Add one sentence directly after that rule, in the same change as this
phase's own code:

> `PhysicsSystem` (`src/Game/Physics/PhysicsSystem.h/.cpp`) is a
> fourth `Game`-layer orchestrator system, but is deliberately NOT part of
> this dual-dependency list — it depends on the ECS `Registry` (to read/
> write `DynamicChainRig`/`ResolvedAnimationPose`) but never on `Renderer`/
> `Mesh`/`Pipeline`, by design, so it stays independently testable and
> independently schedulable from `AnimationSystem`'s own GPU-touching half
> (see `task_manager/verlet-integration-1/PHASE3...md`'s own v3 Revision
> Notice for the full rationale).

### 3.9 Tests for this phase

- `tests/Animation/AnimationPoseEvaluatorTests.cpp`: add the
  divergence-guard regression test described in 3.4.
- `tests/Physics/FixedTimestepAccumulatorTests.cpp`: as listed in 3.3.
- A new `tests/Game/Animation/AnimationSystemEvaluatePosesTests.cpp` (Tier 1):
  with a hand-built `Registry` + minimal `SkinnedMeshData` (no real `*.gta`
  file, following whatever fixture pattern `tests/Game/RenderSystemTests.cpp`
  already uses), confirm `EvaluatePoses()` followed by `SkinAndUpload()`
  produces BYTE-IDENTICAL skinning matrices/vertex output to calling the
  OLD, single-call `EvaluateAnimatedSkinningPose()` directly and skinning
  from that - proving the split itself is a genuine, verified behavioral
  no-op. Also assert `EvaluatePoses()` actually adds/updates a
  `ResolvedAnimationPose` component on the animator entity with the
  expected `pose` contents.
- A new `tests/Game/Physics/PhysicsSystemTests.cpp` (Tier 1, mirrors the
  fixture style above): with the stubbed, always-empty `DynamicChainRigCache`
  from 3.6, confirm `PhysicsSystem::Update()` never modifies an
  entity's `ResolvedAnimationPose::pose` (proving THIS phase's wiring is a
  genuine no-op too, symmetric to the animation-side test above); also
  confirm it is a safe no-op when called on an entity that has a
  `DynamicChainRig` but no `ResolvedAnimationPose` yet (order-of-operations
  degrade-gracefully case), and when called on an entity that has neither
  component at all.
- (v2/v3/v4 addition) Add a matching descriptive paragraph for each new test
  file above to `tests/CMakeLists.txt`'s own header "Test taxonomy" comment
  block, in the same style/level of detail as every existing entry there.

## Step 4: What We Will NOT Do

- We will **not** make the fixed timestep configurable per-model in this
  phase — one engine-wide `GlobalPhysicsSettings` instance (owned by
  `PhysicsSystem`, see 3.6) is enough for now.
- We will **not** touch `Application::Run()`'s own frame-delta computation —
  the fixed-timestep accumulator introduced here is scoped ENTIRELY to
  `PhysicsSystem::Update()`, never the whole engine's frame loop.
- We will **not** implement real chain detection (`DynamicChainDetection.h`)
  or a real, populated `DynamicChainRigCache` in this phase — stub them
  exactly as described in 3.6 and leave the real data-driven population to
  Phase 4.
- We will **not** relax the existing "`AnimationSystem::SkinAndUpload()`'s
  outer per-animator loop must stay strictly sequential" rule (unchanged
  from v1/v2, just renamed/relocated to the half of the old loop that
  actually needs it — see 3.5's own note on why `EvaluatePoses()` is NOT
  bound by this same constraint).
- **(v3, the central rule of this whole revision) We will NOT let
  `AnimationSystem` `#include` anything under `src/Physics/`, and we will
  NOT let `PhysicsSystem` `#include` `Animation/AnimationPoseEvaluator.h`,
  `MotionSampler.h`, `IkSolver.h`, `AppendBoneSolver.h`, `VertexSkinning.h`,
  `ECS/Components/SkeletalAnimator.h`, or anything under `Renderer/`/`Mesh.h`/
  `RenderSystem.h`/`MeshInstantiationSystem.h`.** Any future edit that adds
  one of these `#include`s back is reintroducing the exact coupling this
  revision exists to remove — treat it as a regression, not a convenience.
- We will **not** have `PhysicsSystem` call any `AnimationSystem` method,
  or vice versa. The two classes must remain mutually unaware of each
  other's existence; `Game::Update()` (3.7) is the ONLY place that knows
  both exist and calls them in order.

## Step 5: Their Role

1. Add `ResolvedAnimationPose.h` (3.1) and `DynamicChainRig.h` (3.2) first,
   with no logic behind them yet — they must compile trivially as plain
   ECS components.
2. Add `FixedTimestepAccumulator.h/.cpp` (3.3), independent of any ECS/pose
   change, with its own tests green before touching anything else.
3. Refactor `AnimationPoseEvaluator.h/.cpp` per 3.4, and get its regression
   test green before touching `AnimationSystem.cpp`/`PhysicsSystem.cpp`
   at all.
4. Split `AnimationSystem::Update()` into `EvaluatePoses()`/`SkinAndUpload()`
   per 3.5. Confirm the FULL existing test suite still passes — this
   touches a widely-depended-upon file.
5. Add the brand-new `PhysicsSystem` (3.6), stubbed exactly as
   instructed, and wire it into `Game::Update()`/`Game::CreateMeshEntityFromGtaFile()`
   per 3.7.
6. Apply the `AGENTS.md` clarification (3.8) in the SAME change.
7. Add the new Tier 1 tests from 3.9 proving BOTH halves of this split are
   behavioral no-ops today, and confirm the FULL existing test suite still
   passes end-to-end.
8. Stop here. Do not proceed to make dynamic chains actually
   populate/animate anything visible — that is Phase 4's explicit job.
