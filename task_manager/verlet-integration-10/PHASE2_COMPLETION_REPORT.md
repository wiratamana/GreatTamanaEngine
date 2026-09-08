# PHASE2 Completion Report — Joint's Own PMX Rigid-Body Shape Becomes Its Collision Radius

Campaign: `task_manager/verlet-integration-10/` (parent: `PHASE0_MASTER_STRATEGY.md`)
Phase document executed: `PHASE2_JOINT_OWN_RIGID_BODY_SHAPE_AS_COLLISION_RADIUS.md`
Date: 2026-09-08

## Summary

Implemented PHASE2 of the verlet-integration-10 campaign exactly as specified
in the phase document: a simulated joint particle is no longer a zero-radius
mathematical point during collision — it now carries a real, PMX-derived
physical collision radius (`VerletParticle::collisionRadius`), inflating the
effective test/push-out surface of every `Solve*Collision()` function it's
tested against. This directly answers the user's own "i believe still using
sphere collider?" question: the MOVING side of a collision now genuinely has
a physical extent too, matching how the real MMD/Bullet physics pipeline
treats both sides of every collision test as real, sized rigid bodies.

## Files Changed

### Production code
- `src/Physics/VerletParticle.h` — added a trailing `collisionRadius` field
  (default `0.0f`, reproducing the pre-PHASE2 zero-radius-point behavior for
  every existing hand-built particle in the test suite).
- `src/Physics/SphereCollider.cpp`/`.h` — `SolveSphereCollision()` now
  computes `effectiveRadius = collider.radius + max(0, particle.collisionRadius)`
  and tests/pushes against that instead of the collider's own bare radius.
  `CapsuleCollider.cpp`'s existing delegation to this function means Capsule
  collision inherits the inflation for free, with zero code change of its
  own (only a doc-comment note added to `CapsuleCollider.h`).
- `src/Physics/BoxCollider.cpp`/`.h` — `SolveBoxCollision()` now inflates its
  effective half-extents on every axis by `max(0, particle.collisionRadius)`
  before the penetration test/push-out (a deliberately conservative,
  never-unsafe per-axis approximation of a true rounded-box Minkowski sum).
- `src/Physics/DynamicChainDefinition.h` — added a trailing `collisionRadius`
  field (default `0.0f`) to `DynamicJointSettings`.
- `src/Physics/DynamicChainDetection.cpp` — new pure helper
  `DeriveJointCollisionRadius(shape, shapeSize)` (Sphere/Capsule use their
  own authored radius directly — a Capsule's height never contributes, since
  it's a segment length already accounted for by the chain's own
  `restLengths`; Box uses the smallest of its three half-extents, a safe
  inscribed-sphere approximation; a degenerate/negative result clamps to
  exactly `0.0f`). Step G's existing per-joint rigid-body-matching loop now
  also seeds `chain.jointSettings[j].collisionRadius` from it.
- `src/Physics/DynamicChainSolver.cpp` — `SeedParticlesFromAnimatedPose()`
  now also copies `definition.jointSettings[i].collisionRadius` into
  `particle.collisionRadius` at the same point `inverseMass` is seeded.
- `src/Physics/DynamicChainSolver.h` — doc comment updates (step 1's seeding
  list, step 5's collision description) describing the new radius inflation.

### Tests (extended/new)
- `tests/Physics/SphereColliderTests.cpp` — 2 new cases appended:
  `ParticleWithNonZeroCollisionRadiusIsPushedFartherThanAZeroRadiusParticle`,
  `ZeroCollisionRadiusReproducesExactPreExistingBehavior`.
- `tests/Physics/BoxColliderTests.cpp` — 1 new case appended:
  `ParticleWithNonZeroCollisionRadiusIsPushedToAnInflatedFace`.
- `tests/Physics/DynamicChainDetectionJointRadiusTests.cpp` (new file) — 5
  tests: Sphere/Capsule/Box shape-to-radius derivation, a degenerate
  negative-half-extent clamp-to-zero regression guard, and a multi-joint
  chain proving each joint derives its own radius independently (not a
  single reused value).

### Build registration
- `tests/CMakeLists.txt` — new test file added to `GTE_TEST_SOURCES`, plus a
  matching taxonomy-comment entry.

## Verification

- **Fast compile check**, per this phase's workflow instructions (no full
  build/regression — that's PHASE5): `cmake --build build --target gte_core`
  and `cmake --build build --target GreatTamanaEngineTests` both succeed with
  zero errors/warnings related to this change.
- Additionally ran the full PHASE1+PHASE2-relevant test subset directly
  (`GreatTamanaEngineTests.exe --gtest_filter=...`) as an extra sanity check
  beyond the required compile-only verification. All of this phase's own new/
  extended tests pass (`SphereColliderTests`, `BoxColliderTests`,
  `DynamicChainDetectionJointRadiusTests`), along with every pre-existing
  `DynamicChainDetectionTests`/`DynamicChainSolverTests` test.
- **Pre-existing, PHASE1-introduced test failure noted (NOT caused by this
  phase, left unmodified — out of PHASE2's scope):**
  `DynamicChainSolverCollisionGroupFilterTests.JointAndColliderInDifferentGroupsWithNoOverlapDoNotCollide`
  fails on its own, independent of any PHASE2 change (confirmed: this test
  never touches `collisionRadius`, which defaults to `0.0f` throughout). The
  test's own 1-joint pendulum (root at origin, rest length 1, gravity, no
  goal constraint) naturally settles at roughly `(0, -1, 0)` regardless of
  whether collision is filtered — a point whose distance from the test's own
  chosen collider center `(1, -0.5, 0)` (radius `1.0`) is ~1.13, i.e. already
  outside the sphere even with collision correctly blocked. This looks like a
  geometry-selection mistake in the PHASE1 test itself (it was verified only
  via compile-check at the time, per `PHASE1_COMPLETION_REPORT.md`'s own
  "did not run ctest" note) rather than any solver defect — the other 4 tests
  in that same file (including the symmetric same-group "must collide" case)
  all pass correctly. Left unmodified since fixing a PHASE1 test file is
  outside this phase's assigned scope; flagged here for PHASE5's own
  end-to-end regression pass to address.

## Notable Incident (self-corrected mid-session, not a tool malfunction)

While editing `VerletParticle.h`, an `edit_line` call miscounted which line
held `bool pinned = false;` vs. the struct's closing `};`, and briefly
replaced the wrong line. Caught immediately by re-reading the file after the
edit, and fixed with a follow-up `edit_line` call restoring `bool pinned =
false;` in the correct place before continuing. Also two doc-comment edits
(`SphereCollider.h`'s doc comment) benefited from `edit_line`'s own
auto-dedup safety net removing an accidental leftover duplicate line; the
`DynamicChainSolver.h` doc-comment edit needed one manual correction after a
similar off-by-one boundary overlap (dropped a sentence, restored by
re-editing that same line range). All of this was normal editing-mistake
self-correction, not a malfunctioning tool.

## What's Next

PHASE3 (`PHASE3_COLLISION_ENABLED_ON_BY_DEFAULT.md`) flips
`DynamicChainDefinition::collisionEnabled`'s default from `false` to `true`,
and fixes the one existing test whose assertion depends on the old default —
read that phase document next.
