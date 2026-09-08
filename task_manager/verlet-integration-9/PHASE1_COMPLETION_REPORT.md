# PHASE1 Completion Report — Generic Shape Collision Math: Box + Capsule + Unified `Collider`

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase file executed: `PHASE1_GENERIC_SHAPE_COLLISION_MATH_BOX_AND_CAPSULE.md`
Status: **COMPLETE** — compiles cleanly, new tests pass, no regressions expected (purely additive).

---

## What was done

Implemented PHASE1 exactly as specified in
`PHASE1_GENERIC_SHAPE_COLLISION_MATH_BOX_AND_CAPSULE.md` — six brand-new,
pure, engine-data-free source files under `src/Physics/`, plus three new
Tier-1 test files under `tests/Physics/`, registered in both
`CMakeLists.txt` files. Nothing outside this new set of files was modified.

### New source files (`src/Physics/`)

- **`BoxCollider.h`/`.cpp`** — `BoxCollider{center, rotation, halfExtents}` +
  `SolveBoxCollision()`: transforms the particle into the box's local space,
  tests strict penetration on all 3 axes, and — if penetrating — pushes the
  particle out along the axis with the smallest escape distance (standard
  shallow-OBB push-out), reapplying the box's rotation to convert back to
  world space. A degenerate (`<= 0`) half-extent on any axis is automatically
  a no-op (falls out of the strict-inequality test itself, no separate
  branch needed).
- **`CapsuleCollider.h`/`.cpp`** — `CapsuleCollider{center, rotation, radius,
  height}` + `SolveCapsuleCollision()`: computes the closest point on the
  capsule's own central line segment (local +Y axis before rotation, per
  `RigidBodyWireframe.cpp`'s established convention) to the particle, clamped
  to the segment's own end caps, then delegates to the already-tested
  `SolveSphereCollision()` against a sphere of the same radius centered at
  that closest point — zero duplicated math, and the degenerate-at-center
  handling is inherited for free. A non-positive `height` degrades to a pure
  sphere at `center` (both segment endpoints coincide).
- **`Collider.h`/`.cpp`** — a unified, shape-tagged `Collider{shape, center,
  rotation, size}` struct (`ColliderShape::Sphere/Box/Capsule`) plus
  `SolveCollision()`, a thin dispatcher repacking `size`'s per-shape fields
  (Sphere: `size.x` = radius; Box: `size.xyz` = half-extents; Capsule:
  `size.x`/`size.y` = radius/height) into a call to the right underlying
  `Solve*Collision()`. Deliberately has zero dependency on `Assets/` (kept in
  the "pure, engine-data-free" architectural tier alongside
  `SphereCollider`/`VerletParticle`, per `PHASE0`'s own tiering rule) —
  translating a real PMX `RigidBodyShape` into this enum is left to PHASE3.

Every doc comment, algorithm, and API shape matches the strategy document
verbatim (including field ordering, degenerate-case handling, and the
`previousPosition`-is-never-touched / pinned-particle-is-always-a-no-op
conventions already established by `SphereCollider.h`).

### New test files (`tests/Physics/`)

- **`BoxColliderTests.cpp`** (6 tests): fully-outside no-op, penetration
  along the shortest axis pushes to that face, exact-center deterministic
  tie-break with no NaN, a 90°-rotated box proves rotation is actually
  applied (verified by transforming the result back into local space), a
  pinned particle is never moved, and three degenerate half-extent
  variants (`{0,1,1}`, `{-1,1,1}`, `{0,0,0}`) are all no-ops.
- **`CapsuleColliderTests.cpp`** (7 tests): fully-outside no-op, sideways
  penetration against the cylindrical body projects radially outward at
  exactly `radius`, penetration beyond an end cap clamps to that endpoint
  (not the infinite line), a 90°-rotated capsule proves its local +Y axis
  actually rotated, non-positive height degrades to a pure sphere at
  `center`, a pinned particle is never moved, and non-positive radius is a
  no-op.
- **`ColliderTests.cpp`** (3 tests): deliberately thin — each shape variant
  (Sphere/Box/Capsule) is run once through `SolveCollision()` and once
  through the equivalent direct `Solve*Collision()` call on an identical
  starting particle, asserting the two results match exactly. These exist
  purely to prove the dispatcher routes/repacks fields correctly, not to
  re-test the underlying shape math (already covered above).

All 16 new tests pass (`ctest`/direct `--gtest_filter` run — see
"Verification" below).

### Build registration

- Root `CMakeLists.txt`: the 6 new `src/Physics/*.h/.cpp` files were added
  to `add_library(gte_core STATIC ...)`'s source list, immediately after
  the existing `src/Physics/SphereCollider.h/.cpp` lines.
- `tests/CMakeLists.txt`: the 3 new test files were added to
  `GTE_TEST_SOURCES`, immediately after the existing
  `Physics/SphereColliderTests.cpp` line, plus a matching entry added to
  that file's own top-of-file test-taxonomy comment block (mirroring every
  other listed test file's documentation convention).

---

## Verification

Per this task's workflow rules (no full build/regression yet — that is
PHASE6's job), only a **fast, targeted compile check** was performed:

1. `cmake -S . -B build` — reconfigured successfully (no `CMakeLists.txt`
   syntax errors, all fetch/vendor dependencies already present, no
   re-download needed).
2. `cmake --build build --target gte_core` — the 3 new `.cpp` files
   (`BoxCollider.cpp`, `CapsuleCollider.cpp`, `Collider.cpp`) compiled and
   linked into `libgte_core.a` with **zero warnings/errors**.
3. `cmake --build build --target GreatTamanaEngineTests` — the 3 new test
   `.cpp` files compiled and linked into `GreatTamanaEngineTests.exe` with
   **zero warnings/errors**.
4. As an extra sanity check (not a full regression run), the new tests
   specifically were run via
   `GreatTamanaEngineTests.exe --gtest_filter=BoxColliderTests.*:CapsuleColliderTests.*:ColliderTests.*`
   — **16/16 passed**, 0 failures.

No other test suites were run (per the "no full build/regression yet"
instruction) — the rest of the existing test suite was not touched by this
phase's changes in any way (every new file is purely additive; nothing
existing was edited besides the two `CMakeLists.txt` source lists), so no
regression risk is expected, but the full `ctest` run is deliberately
deferred to PHASE6 as planned by the master strategy.

---

## Notes for the next phase (PHASE2)

- Nothing in the rest of the engine calls `SolveBoxCollision()`/
  `SolveCapsuleCollision()`/`SolveCollision()` yet — as expected, this phase
  is 100% new, additive, self-contained math. `DynamicChainDefinition`,
  `DynamicChainSolver`, and `PhysicsSystem` are completely untouched.
- PHASE2 should proceed exactly as documented in
  `PHASE2_DATA_MODEL_GENERALIZED_COLLIDER_LIST.md`: replace
  `DynamicChainDefinition`'s single `hasHeadCollider`/`headColliderBoneIndex`/
  `headColliderRadius` trio with one `bool collisionEnabled` flag, change
  `StepDynamicChain()`'s signature to take a shared
  `const std::vector<Collider>&` (the new `Collider` struct from this
  phase), and add the new `ModelColliderDefinition` struct — plus the v2
  stale-doc-comment fix in `Game/Physics/PhysicsSystem.h`.
- No deviations from the strategy document were made; no ambiguities were
  encountered that required a judgment call beyond what was already fully
  specified.
