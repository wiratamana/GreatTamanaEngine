# PHASE4 Completion Report — Runtime World-Space Collider Resolution and Solver Wiring

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase file executed: `PHASE4_RUNTIME_WORLD_SPACE_COLLIDER_RESOLUTION_AND_WIRING.md`
Status: **COMPLETE** — compiles cleanly (both `gte_core` and
`GreatTamanaEngineTests`), all directly-relevant tests pass (113/113 across
the Physics/, Game/Physics/, and Animation-touching-physics regression slice
run as an extra sanity check, including the 4 new tests this phase adds), no
regressions observed.

---

## What was done

Implemented `PHASE4_RUNTIME_WORLD_SPACE_COLLIDER_RESOLUTION_AND_WIRING.md`
exactly as specified (Steps 3.1–3.3), picking up from PHASE3's own completion
report, which confirmed `PhysicsSystem.cpp`'s `StepDynamicChainRange()` still
resolved a hardcoded, always-empty `const std::vector<Collider> colliders;`
placeholder and that nothing yet resolved `model->colliders` (PHASE3's
detected, LOCAL/bind-pose-relative list) to world space per frame:

1. **`src/Game/Physics/PhysicsSystem.cpp` (3.1)**:
   - Added `#include <algorithm>` (for `std::any_of`).
   - Added a small, anonymous-namespace helper, `ToColliderShape()`, mapping
     `Assets::RigidBodyShape` to `Physics::ColliderShape` via an explicit
     `switch` (never a `static_cast` between the two unrelated enum types),
     exactly per the strategy document's own doc comment.
   - Extended `DynamicChainBatchContext` with one new, LAST field,
     `const std::vector<Collider>* resolvedColliders;`, immediately after
     `bool frozen;` — verbatim per the master strategy's own v2 "exact,
     unambiguous" fix (Revision Notes, finding #5), so there was zero risk of
     a positional aggregate-initializer mismatch.
   - Deleted `StepDynamicChainRange()`'s PHASE3 placeholder comment/local
     (`const std::vector<Collider> colliders;`) and changed its
     `StepDynamicChain(...)` call to pass `*context.resolvedColliders`
     instead — the shared, per-entity list flows uniformly to every chain
     regardless of that chain's own `collisionEnabled`, exactly matching how
     `skeleton`/`pose` are already passed uniformly today.
   - Inside `PhysicsSystem::Update()`, right after
     `entityWorldMatrix`/`entityWorldMatrixInverse` are computed and right
     before the `DynamicChainBatchContext context{ ... };` construction,
     added the real per-entity, per-frame resolution: an
     `anyChainWantsCollision` guard (`std::any_of` over `model->chains`)
     combined with `!model->colliders.empty()` skips the entire loop (and
     therefore every extra `ComputeBoneWorldMatrix()` call) whenever nothing
     on this entity actually wants collision this frame — the overwhelmingly
     common case today, since `collisionEnabled` still defaults to `false`
     until PHASE5's Editor opt-in lands. When at least one chain does want
     it, every `ModelColliderDefinition` is resolved to a world-space
     `Collider` via
     `boneWorld = entityWorldMatrix * ComputeBoneWorldMatrix(model->skeleton, resolvedPose->pose, colliderDef.boneIndex)`,
     `collider.center = boneWorld.TransformPoint(colliderDef.localOffsetPosition)`,
     and `collider.rotation = Quat::FromMat4(boneWorld) * colliderDef.localOffsetRotation`
     — reusing the exact, already-established `Quat::FromMat4()`
     bone-rotation-extraction precedent from `BoneChainPhysicsResolver.cpp`,
     per the strategy document's own Step 2.
   - Updated the `DynamicChainBatchContext context{ ... };` construction to
     append the one new trailing argument, `&resolvedColliders`, matching the
     new field's position exactly.
2. **`src/Physics/Collider.h`/`Physics/ModelColliderDefinition.h`** — no
   changes needed; both already existed from PHASE1/PHASE3 exactly as this
   phase's own code expects (`ColliderShape`, `Collider::{shape,center,
   rotation,size}`, `ModelColliderDefinition::{boneIndex,shape,shapeSize,
   localOffsetPosition,localOffsetRotation}` all matched verbatim).
3. **`tests/Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp`
   (3.3, new file)** — a dedicated companion to PHASE6's own broader
   end-to-end multi-shape test, focused specifically on the *resolution
   math* itself, driving the real `PhysicsSystem::Update()` pipeline
   end-to-end (never a hand-called `StepDynamicChain()` in isolation). Every
   test neutralizes every OTHER simulation force (`gravity = 0`,
   `DynamicJointSettings::stiffness = 0`, `damping = 1.0`,
   `DynamicChainDefinition::constraintIterations = 0`, mutated post-
   registration via `PhysicsSystem::GetDynamicChainRigCache().TryGetMutable()`)
   so collision resolution is the *only* thing that can ever move a joint
   particle — isolating this phase's own correctness concern from the rest
   of the solver's already-tested dynamics. 4 tests, matching every required
   case from the strategy document:
   - `ColliderTracksItsBoneAsTheBoneAnimates` — a Sphere collider coincident
     with a joint's bind position triggers `SphereCollider.cpp`'s own
     degenerate zero-distance push on frame 1 (pushes to `Y = radius`);
     moving the collider's OWN bone via a direct `ResolvedAnimationPose::pose`
     mutation between frames, then calling `Update()` again, lands the joint
     at the analytically-derived `Y = 0.5` (a *moved* sphere's new surface) —
     clearly distinguishable from the `Y ≈ 1.0` a stale/un-resolved collider
     would instead produce.
   - `ColliderRespectsItsOwnBindPoseLocalOffsetAndRotation` — a Box collider
     whose `RigidBody::translate`/`rotateRadians` (an absolute, model-space
     bind-pose transform per `Assets/PhysicsData.h`'s own documented
     convention) is deliberately non-coincident with its own tracked bone's
     bind position; with the bone held at bind pose throughout, the resolved
     collider must reproduce that rigid body's own authored absolute
     transform exactly — verified by seeding a joint at a known point INSIDE
     the box (computed via the engine's own `Quat`/`Vec3` API, never a
     hand-derived magic number) and confirming the collision push lands
     exactly where that same API predicts.
   - `NoChainWantingCollisionResolvesAnEmptyListEveryFrame` — the *exact*
     same overlapping-collider setup as the first test (which WOULD trigger
     a push if collision were enabled), but with every chain's
     `collisionEnabled` left at its `false` default — confirms the joint
     never moves at all, proving both the perf-guard's empty-list skip and
     `StepDynamicChain()`'s own `collisionEnabled` guard combine correctly
     into a genuine no-op.
   - `EntityWorldTransformRotationIsComposedIntoColliderOrientation` — reuses
     the Box fixture above, but rotates the *owning ECS entity's* `Transform`
     (deliberately about a different axis than the box's own authored
     rotation) instead of any bone; since rotating the entity rotates the
     joint AND the collider by the identical amount, the expected result is
     simply the unrotated case's own result, itself rotated — confirms
     `entityWorldMatrix` is genuinely composed into the collider's
     center/rotation, not just into the joint's own position (which was
     already proven by verlet-integration-7).
4. **`tests/CMakeLists.txt`** — `Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp`
   added to `GTE_TEST_SOURCES`, immediately after the existing
   `Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp` line, per the
   strategy document's own instruction.

### One deviation worth recording: the new test file's fixture needed a second, inert joint

The strategy document's own Step 3.3 sketches single-joint-of-interest test
scenarios, but `DynamicChainDetection.cpp`'s Step G discards any detected
chain shorter than `DynamicChainDetectionDefaults::minimumChainLength`
(`= 2`, `DynamicChainDetection.h`) — a real, pre-existing constraint this
phase's own strategy document didn't call out, and `PhysicsSystem::RegisterDynamicChains()`
has no parameter to override it. A naive single-Dynamic-joint fixture is
therefore silently discarded as "too short" before ever reaching this phase's
own resolution code, which was first caught by this session's own initial
test run (`ColliderTracksItsBoneAsTheBoneAnimates`/
`ColliderRespectsItsOwnBindPoseLocalOffsetAndRotation`/
`EntityWorldTransformRotationIsComposedIntoColliderOrientation` all failed on
the first attempt; `NoChainWantingCollisionResolvesAnEmptyListEveryFrame`
passed either way, since "nothing moved" is also exactly what a
never-attached rig produces). Fixed by adding a second, deliberately inert
Dynamic joint (`kSecondJointBoneIndex`, positioned 1000 units away from
everything else in every fixture, and further neutralized by this file's own
`constraintIterations = 0` mutation) purely to satisfy
`minimumChainLength` — a test-fixture-only change, no production code was
affected. Re-ran the full new suite afterward: all 4 tests pass.

---

## Verification

Per this task's workflow rules (no full build/regression yet), a fast,
targeted compile check plus a run of the new and directly-related test
suites were performed:

1. `cmake --build build --target gte_core` — **zero warnings/errors**,
   including the fully-rewritten `Game/Physics/PhysicsSystem.cpp`.
2. `cmake -S . -B build` (reconfigure, to pick up the new test file) — clean.
3. `cmake --build build --target GreatTamanaEngineTests` — **zero
   warnings/errors**, including the new
   `tests/Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp` (after
   one fixture fix — see above — and one missing include,
   `ECS/TransformHierarchy.h`, needed for `ComputeWorldTransform()`).
4. `GreatTamanaEngineTests.exe --gtest_filter=PhysicsSystemModelColliderResolutionTests.*`
   — **4/4 passed**.
5. As a broader sanity check (not the full suite — that is PHASE6's job),
   `--gtest_filter=*Physics*:*Collider*:*DynamicChain*` — **113/113 passed**,
   covering every Physics/, Game/Physics/, and Animation-touching-physics
   test in the suite (including `ModelColliderDetectionTests`,
   `DynamicChainSolverTests`' own collider-related cases,
   `PhysicsSystemParallelTests`' serial-vs-parallel byte-identity check, and
   every pre-existing `PhysicsSystem*`/`*DynamicChain*` regression), no
   regressions.

No other test suites were run (per the "no full build/regression yet"
instruction) — the full `ctest` run is deliberately deferred to PHASE6 as
planned by the master strategy.

---

## Notes for the next phase (PHASE5)

- Collision is now **fully functional end-to-end** for any entity/chain that
  opts in: a real model's PMX Static Sphere/Box/Capsule rigid bodies are
  detected (PHASE3), resolved to world space every frame with correct
  position AND orientation (this phase), and genuinely collided against by
  every chain whose `DynamicChainDefinition::collisionEnabled` is `true`
  (PHASE2's solver wiring). Nothing in the running Editor/game actually
  exercises this yet, though — `collisionEnabled` still defaults to `false`,
  and per PHASE2's own completion report, `Editor/Panels/InspectorPanel.cpp`
  and `Editor/BoneViewerWindow.cpp` currently show only the *minimal*
  transitional `collisionEnabled` checkbox / generic "Collision: enabled"
  text (no collider-count readout, no richer visualization) — PHASE5 is what
  turns that into the intended real Inspector/Bone-Viewer UI.
- Per PHASE0's own v2 Revision Notes (finding #1), PHASE5 must fix **four**
  call sites, not the two `PHASE5_EDITOR_INSPECTOR_UI_UPDATE.md` originally
  scoped in v1: `Panels/InspectorPanel.cpp`'s two (`ModelPartInspector`'s
  Verlet case + the per-chain "Dynamic Chain Physics" section) AND
  `Editor/BoneViewerWindow.cpp`'s two (the Verlet tree pane's text row + the
  3D-viewport head-collider wireframe block) — please re-read PHASE2's own
  completion report's "Additional work" section before starting, since the
  exact call sites it describes (already carrying a minimal, clearly-labeled
  transitional fix) are the ones to replace with the real, richer UI, not
  the original v1-described call sites.
- No deviations from PHASE4's own strategy document were made for anything
  within its formal scope; the only extra work was the test-fixture-only
  `minimumChainLength` fix described above, which does not affect any
  production code path.
