# PHASE1 — Core Verlet Physics Foundation — Completion Report

Campaign: `task_manager/verlet-integration-1/` (Verlet-Integrated Dynamic Bone
Physics System). Parent: `PHASE0_MASTER_STRATEGY.md`. This report covers
**Phase 1 only** — `PHASE1_CORE_VERLET_PHYSICS_FOUNDATION.md` — the
foundation phase with no dependency on any other phase.

## Branch / Prerequisites

- Branch: `feature/physics-from-scratch` (unchanged, as required).
- Read `readme.md` and `agents.md` in full before starting.
- Read all six strategy files under `task_manager/verlet-integration-1/`
  (`PHASE0_MASTER_STRATEGY.md` through `PHASE5_...md`) to understand the full
  five-phase campaign and where Phase 1 fits.
- No prior completion report existed for this campaign (Phase 1 is the first
  phase to be implemented — `src/Physics/` did not exist in the source tree
  before this session), so there was no "previous phase" report to read for
  continuation clues beyond `PHASE0_MASTER_STRATEGY.md`'s own Revision Notes
  (v2/v3/v4), which were followed exactly as written (e.g. the divide-by-zero
  guard fix, the `kTwoPi` reuse instruction, generic non-hair-specific naming).

## What Was Done

Implemented Phase 1 exactly per `PHASE1_CORE_VERLET_PHYSICS_FOUNDATION.md`'s
Step 3/Step 5 checklist: a brand-new, always-compiled, ECS/GPU/Renderer-free
`src/Physics/` module (mirroring how `src/Jobs/`/`src/Profiling/` were
bootstrapped), plus its full Tier-1 test coverage.

### New source files (`src/Physics/`)

- **`VerletParticle.h`** — the plain `VerletParticle` struct
  (`position`/`previousPosition`/`inverseMass`/`pinned`) and the
  `ImpliedVelocity()` helper. No behavior beyond trivial accessors, per the
  "ECS component"-style philosophy `AGENTS.md` already documents.
- **`VerletIntegration.h`/`.cpp`** — `IntegrateParticle()`, exactly the
  Stormer-Verlet-with-damping formula specified in the phase document: a
  no-op for a pinned particle (keeping `previousPosition` in lock-step),
  damping clamped to `[0,1]` internally, `newPosition = position + velocity +
  acceleration * dt^2`.
- **`ChainConstraints.h`/`.cpp`** — the two independent constraint kinds:
  - `SolveDistanceConstraint()` — the STRUCTURAL (rest-length) constraint,
    mass-weighted correction. Implements both v2-fix guards from
    `PHASE0_MASTER_STRATEGY.md`'s Revision Notes: (1) returns immediately
    whenever `a.inverseMass + b.inverseMass <= kEpsilon`, checked directly
    and independently of the `pinned` flags (never just "both pinned"), and
    (2) returns immediately whenever `currentLength <= kEpsilon` (degenerate,
    no well-defined direction).
  - `SolveGoalConstraint()` — the user-facing "Stiffness" parameter, a plain
    `Lerp(position, animatedTargetPosition, Clamp(stiffness01, 0, 1))`,
    no-op for a pinned particle.
- **`WindField.h`/`.cpp`** — `WindSettings` (the global/world wind
  parameter surface) and `ComputeWindAcceleration()`, a pure, deterministic
  function of `(settings, worldPosition, timeSeconds)` with no RNG/global
  state, exactly matching the documented formula (spatial-hash-shifted gust
  phase via `Dot(worldPosition, (12.9898, 78.233, 37.719)) * 0.001`). Reuses
  `gte::kTwoPi` from `Math/MathTypes.h` directly — no second, local `kTwoPi`
  constant was declared, per the phase document's explicit v2-fix
  instruction.

All four `.h`/`.cpp` pairs depend on nothing but `src/Math/Vec3.h` and
`src/Math/MathTypes.h` (`Clamp`, `kEpsilon`, `kTwoPi`) — zero dependency on
`SkeletonData`, `Registry`, `Entity`, or any GPU/Renderer type, confirmed by
the compile succeeding with only those two math headers included.

### New test files (`tests/Physics/`), all Tier 1

- **`VerletIntegrationTests.cpp`** (5 tests) — particle-at-rest stays at
  rest; implied velocity continues in a straight line under zero
  acceleration/damping (Newton's first law); damping of `1.0` fully kills
  implied velocity after one step; a pinned particle never moves regardless
  of acceleration/damping; gravity-only free-fall over 240 fixed steps
  matches the closed-form `0.5*g*t^2` within a small relative tolerance.
- **`ChainConstraintsTests.cpp`** (7 tests) — stretched particles pulled
  together by the exact mass-weighted prediction; particles already at rest
  length left untouched; a pinned side receives zero correction while the
  other receives 100%; the v2-regression case (both particles
  `inverseMass == 0` but neither flagged `pinned`) is a safe, finite no-op;
  `SolveGoalConstraint` at `stiffness01` `0`/`1`/`0.5` leaves-untouched/
  snaps-exactly/lands-exactly-halfway respectively.
- **`WindFieldTests.cpp`** (3 tests) — same inputs always produce
  bit-identical output (determinism); `baseStrength` alone with zero gust
  produces a time-invariant constant vector; two different world positions
  with gust enabled produce measurably different results (proves the
  spatial-hash term is genuinely wired in).

All 15 new tests pass (verified directly, see "Verification" below).

### Build wiring

- **`CMakeLists.txt`** — added the new `src/Physics/*` block to
  `add_library(gte_core STATIC ...)`'s source list, placed directly after
  the existing `src/Animation/*` block (cosmetic ordering only, per the
  phase document).
- **`tests/CMakeLists.txt`** — added `Physics/VerletIntegrationTests.cpp`,
  `Physics/ChainConstraintsTests.cpp`, and `Physics/WindFieldTests.cpp` to
  the unconditional (Tier 1, always-built) `GTE_TEST_SOURCES` list, plus a
  matching descriptive paragraph for each in the file's own "Test taxonomy"
  header comment block — closing PHASE0's v2 Revision Notes finding #6
  ("every existing Tier-1 test file has a matching descriptive paragraph;
  none of the five phase documents' instructions mentioned adding one" — this
  phase's own instructions did call it out explicitly, and it was followed).

## What Was Deliberately NOT Done (per Phase 1's own "Step 4: What We Will
NOT Do", and per the overall workflow rules for this task)

- No rotation/orientation on `VerletParticle` — pure point mass, exactly as
  specified (bone rotation reconstruction is Phase 2's job).
- No angular/bend constraints, no collision of any kind (ground plane, head
  sphere) — both explicitly deferred to Phase 5.
- **Zero ECS/`SkeletonData`/`AnimationSystem`/Renderer wiring** — `src/Physics/`
  compiles and is tested in total isolation, per Phase 1's own scope. Phases
  2–5 (bone↔particle bridge, ECS `PhysicsSystem`, data-driven parameters,
  collision/hardening) are NOT part of this change and remain the next
  phases in the campaign.
- `src/Math/Vec3.h`/`Quat.h`/`Mat4.h` were not touched at all — every helper
  Phase 1 needed that didn't already exist was added as a free function local
  to `src/Physics/`.
- No full build or full regression test was run (per this task's explicit
  workflow rules) — only a fast, targeted incremental compile of `gte_core`
  + `GreatTamanaEngineTests`, plus a filtered run of only the 15 new tests.

## Verification

1. **Reconfigure**: `cmake -S . -B build` — succeeded, picked up the new
   `src/Physics/*` sources and the new `tests/Physics/*` sources with no
   errors (only a pre-existing, unrelated KTX-Software git-describe warning
   was printed, not caused by this change).
2. **Fast compile check** (not a full build): `cmake --build build --target
   GreatTamanaEngineTests` — this incrementally rebuilt exactly the new
   object files (`VerletIntegration.cpp.obj`, `ChainConstraints.cpp.obj`,
   `WindField.cpp.obj`, plus the three new test `.cpp.obj` files), re-linked
   `libgte_core.a`, and re-linked `GreatTamanaEngineTests.exe` — **8/8 build
   steps succeeded, zero warnings/errors**.
3. **Targeted test run**: ran
   `GreatTamanaEngineTests.exe --gtest_filter=VerletIntegrationTests.*:ChainConstraintsTests.*:WindFieldTests.*`
   directly — **15/15 new tests passed** (0 failures). No full `ctest`
   regression run was performed, per this task's workflow rules (reserved for
   the campaign's final phase).

## Next Steps (for whoever picks up Phase 2)

Proceed to `PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md` — it depends directly on
this phase's `VerletParticle`/`IntegrateParticle`/`SolveDistanceConstraint`/
`SolveGoalConstraint`/`ComputeWindAcceleration` signatures (all implemented
exactly as documented, so Phase 2 should be able to call them directly with
no signature mismatches) plus the promotion of `IkSolver.cpp`'s private
`ComputeBoneWorldMatrix()` helper (Culprit E in `PHASE0_MASTER_STRATEGY.md`)
into a shared, public location.
