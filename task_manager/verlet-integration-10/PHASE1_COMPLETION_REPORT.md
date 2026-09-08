# PHASE1 Completion Report — PMX Collision Group/Layer Filtering

Campaign: `task_manager/verlet-integration-10/` (parent: `PHASE0_MASTER_STRATEGY.md`)
Phase document executed: `PHASE1_PMX_COLLISION_GROUP_LAYER_FILTERING.md` (v2)
Date: 2026-09-08

## Summary

Implemented PHASE1 of the verlet-integration-10 campaign exactly as specified
in the phase document: the Verlet dynamic-chain collision solver now respects
PMX's own rigid-body collision-group/layer filtering rule (Bullet's own
`(1<<groupA & maskB) && (1<<groupB & maskA)` symmetric AND-test), instead of
testing every joint against every detected Static collider unconditionally.

## Files Changed

### Production code
- `src/Physics/Collider.h` — added trailing `group` (`std::uint8_t`, default
  `0`) and `collisionMask` (`std::uint16_t`, default `0xFFFF`) fields to
  `Collider`, plus an updated `SolveCollision()` doc comment clarifying that
  filtering is the caller's responsibility.
- `src/Physics/ModelColliderDefinition.h` — added trailing `group`/
  `collisionMask` fields (same defaults) to `ModelColliderDefinition`.
- `src/Physics/ModelColliderDetection.cpp` — `DetectModelColliders()` now
  copies `body.group`/`body.collisionGroupMask` verbatim into the detected
  `ModelColliderDefinition`.
- `src/Physics/DynamicChainDefinition.h` — added trailing `group`/
  `collisionMask` fields to `DynamicJointSettings`.
- `src/Physics/DynamicChainDetection.cpp` — Step G's existing per-joint
  rigid-body-matching loop now also seeds `chain.jointSettings[j].group`/
  `collisionMask` from the matched `RigidBody`.
- `src/Game/Physics/PhysicsSystem.cpp` — the per-frame collider-resolution
  loop now copies `colliderDef.group`/`collisionMask` into the resolved
  `Collider`.
- `src/Physics/DynamicChainSolver.cpp` — added a shift-safe `GroupBit()`
  helper (masks `group & 0x0Fu` before `1u << group`, preventing undefined
  behavior for an out-of-range/adversarial byte) and `GroupsMayCollide()`
  (the symmetric Bullet-style AND-test). The collision loop (step 5) now
  runs this check before calling `SolveCollision()` for every joint/collider
  pair.
- `src/Physics/DynamicChainSolver.h` — doc comment update describing the new
  group/mask filter step.

### Tests (new)
- `tests/Physics/DynamicChainSolverCollisionGroupFilterTests.cpp` — 5 tests:
  same-group collide, disjoint-mask do-not-collide (with `collisionEnabled`
  explicitly `true`, proving it's the group filter and not the chain-level
  opt-in), symmetric-AND proof (3 sub-cases via an independently-derived
  reference truth table), default-values-reproduce-old-behavior regression,
  and an out-of-range-group-value safety/consistency test (including a
  `group = 255` sanitizer-friendly sub-case).
- `tests/Physics/ModelColliderDetectionGroupMaskTests.cpp` — 2 tests: verbatim
  copy of a non-default group/mask, and verbatim copy of an all-default
  (zero) group/mask (proving `ModelColliderDefinition` never silently
  defaults to `0xFFFF` the way `Collider`/`DynamicJointSettings` do).

### Build registration
- `tests/CMakeLists.txt` — both new test files added to `GTE_TEST_SOURCES`,
  plus matching taxonomy-comment entries describing what each file covers.

## Verification

- **Fast compile check only**, per this phase's workflow instructions (no
  full build/regression yet — that's PHASE5).
- `cmake --build build --target gte_core` — succeeds, zero errors/warnings
  related to this change.
- `cmake --build build --target GreatTamanaEngineTests` — succeeds, all new
  and existing test files (including the two new ones added this phase)
  compile and link cleanly into `GreatTamanaEngineTests.exe`.
- Did **not** run `ctest` (deferred to PHASE5 per the task instructions).

## Notable Incident (tool malfunction, reported separately)

During the `Collider.h` edit, `edit_line`'s `length` parameter was
over-estimated (51, based on the phase doc's shown line count) against the
file's actual remaining line count. Per `edit_line`'s own documented
behavior, an over-long `length` is silently clamped to the file's real end —
which meant the trailing `} // namespace gte` closing brace (which was
*within* that over-estimated range) got swallowed by the replacement, since
the new `contents` I supplied didn't itself repeat that closing brace. This
produced a real, cascading compile failure (thousands of "in namespace
'gte::std'..." errors, since every subsequent `#include` — including the C++
standard library — textually landed inside the still-open `namespace gte {`
scope). Diagnosed by re-reading the file after the edit, confirmed the
missing closing brace, and fixed it with a follow-up `edit_line` call that
re-appended `} // namespace gte`. Rebuilding afterward confirmed a fully
clean compile with no remaining errors. This was a genuine case of the
*documented* over-estimated-`length` failure mode described in `edit_line`'s
own tool description — not something to bug-report (the tool's docs already
warn about this exact scenario and how to verify against it), but worth
noting here as the reason this phase's edit took two passes on that one
file.

## What's Next

PHASE2 (`PHASE2_JOINT_OWN_RIGID_BODY_SHAPE_AS_COLLISION_RADIUS.md`) builds on
these same files (`DynamicJointSettings`, `VerletParticle`, the Step G seeding
loop) to give each joint's own PMX Dynamic rigid body's shape/size a real
collision radius — read that phase document next; it explicitly depends on
this phase's `group`/`collisionMask` plumbing already existing.
