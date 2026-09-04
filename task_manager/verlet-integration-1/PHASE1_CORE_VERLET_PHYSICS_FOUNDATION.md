# PHASE1 — Core Verlet Physics Foundation (`src/Physics/`) (v3 — generic naming)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit A). Depends on: nothing (this is
the foundation phase). Produces: a brand-new, always-compiled,
ECS/GPU/Renderer-free module, `src/Physics/`, exactly mirroring how
`src/Jobs/` and `src/Profiling/` were bootstrapped as new top-level engine
modules — plain math + plain data, callable and testable with zero engine
wiring.

**v3 note (no functional change, naming only):** every occurrence of a
`Hair`-prefixed identifier planned for this phase in earlier drafts
(`HairPhysicsConstraints.h/.cpp`) is renamed to `ChainConstraints.h/.cpp` —
see `PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v4)" for the full
rationale: this module never had any hair-specific knowledge to begin with
(it only ever operates on generic `VerletParticle`s), so its file/type names
now say exactly that and nothing more.

## Step 1: The Goal

Deliver every pure, stateless (or trivially-stateful, engine-data-free)
building block a Verlet-chain bone-physics simulation needs, as small,
independently Tier-1-tested functions/structs, with **no** dependency on
`SkeletonData`, `Registry`, `Entity`, or any GPU/Renderer type — only
`src/Math/` (`Vec3`, `Quat`). This phase's own deliverables must be usable,
and testable, in complete isolation from "is this hooked up to a model" —
exactly the same discipline `AGENTS.md` already documents for
`Animation/BoneChainResolver.h`, `Animation/BonePoseMath.h`,
`Jobs/JobDispatch.h`'s `ComputeBatchRanges()`, and `Renderer/DrawStats.h`:
*"test the pure math in isolation before wiring it into anything
stateful."*

## Step 2: The Situation / The Problem

Nothing under `src/` performs numerical integration of any kind today, and
`src/Physics/` does not exist. The engine's math primitives it must be built
from already exist and are directly reusable:

- `src/Math/Vec3.h` — `Vec3`, `operator+/-/*`, `Dot`, `Cross`, `Length`,
  `Normalize`, `Lerp`. No `Vec3::Clamp`/`Vec3::MoveTowards` exists yet —
  Phase 1 adds only what it needs, as free functions in the new module, never
  by editing `Vec3.h` itself (that header is a stable, hand-rolled math
  primitive shared by the whole engine; extending it is out of scope here).
- `src/Math/Quat.h` — `Quat::FromAxisAngle`, `RotateVector`, `Slerp`,
  `Normalize` — will be needed by **Phase 2**, not this phase (Phase 1 is
  pure position-space particle physics; no rotations are solved here).
- `src/Math/MathTypes.h` — `kEpsilon`, `Clamp`, `Lerp` free functions,
  `ApproximatelyEqual` — reuse these rather than re-declaring local epsilon
  constants (mirrors `Vec3.h`'s own `Normalize()` using `kEpsilon`).

## Step 3: The Plan

### 3.1 New CMake module registration

Add a new `src/Physics/` block to `CMakeLists.txt`'s `add_library(gte_core
STATIC ...)` file list (insert it as its own labeled group, the same way
`src/Jobs/` and `src/Profiling/` each get their own contiguous block — see
the existing list, e.g. right after the `src/Animation/*` block or right
before `src/Assets/*`, either is fine since CMake source-list order is
cosmetic only):

```
src/Physics/VerletParticle.h
src/Physics/VerletIntegration.h
src/Physics/VerletIntegration.cpp
src/Physics/ChainConstraints.h
src/Physics/ChainConstraints.cpp
src/Physics/WindField.h
src/Physics/WindField.cpp
```

(`DynamicChainDefinition.h`/`DynamicChainRuntimeState.h`/
`DynamicChainSolver.h/.cpp` are added in Phase 2, not here — Phase 1 stays
scoped to single-particle/single-constraint primitives only.)

Also add a new `tests/Physics/` folder to `tests/CMakeLists.txt`'s
`GTE_TEST_SOURCES` list (follow the exact pattern already used for
`tests/Animation/`/`tests/Jobs/` — these are Tier 1, always built,
unconditionally, never gated behind `GTE_ENABLE_EDITOR`/
`GTE_ENABLE_PROFILER`/`GTE_ENABLE_JOB_SYSTEM`, matching this module's own
"no ECS/GPU dependency" status):

```
tests/Physics/VerletIntegrationTests.cpp
tests/Physics/ChainConstraintsTests.cpp
tests/Physics/WindFieldTests.cpp
```

### 3.2 `src/Physics/VerletParticle.h`

Plain data struct — no behavior beyond trivial accessors, same "ECS
component"-style philosophy `AGENTS.md` already documents for `MeshData`/
`SkeletonData`:

```cpp
#pragma once
#include "../Math/Vec3.h"

namespace gte {

// One simulated point mass in a Verlet particle chain (see
// VerletIntegration.h). Position-based (Störmer-Verlet): velocity is never
// stored explicitly - it is always `position - previousPosition`, implicitly
// folding in the previous step's timestep. This is what makes Verlet
// integration unconditionally stable for a constrained chain without ever
// needing to store/clamp a separate velocity vector.
struct VerletParticle {
    Vec3 position = Vec3::Zero();
    Vec3 previousPosition = Vec3::Zero();

    // 1/mass - "Weight: controls how heavy the simulated chain feels" (see
    // ChainConstraints.h's distance-constraint mass-weighted correction). A
    // pinned particle (see below) uses inverseMass == 0.0f by convention,
    // even though `pinned` is the flag actually checked - this mirrors
    // Position-Based-Dynamics' own "an infinite-mass point never receives a
    // correction" convention, so a caller that forgets to check `pinned`
    // and instead just weights by inverseMass still gets the correct (zero)
    // result.
    float inverseMass = 1.0f;

    // True for a chain's root anchor particle (see DynamicChainSolver.h,
    // Phase 2) - its position is written EVERY STEP directly from the
    // character's own animated FK pose, never touched by
    // IntegrateParticle()/constraint solving. Kept on the particle itself
    // (rather than only in the chain definition) so ChainConstraints.h's
    // constraint functions stay pure/self-contained - they never need a
    // side-channel "is this the anchor" flag.
    bool pinned = false;
};

// Implicit velocity this step, in world units per second - NOT stored, always
// derived. `deltaTime` must be the SAME fixed timestep IntegrateParticle()
// was last called with, or this value is meaningless (mixing timesteps mid-
// simulation is a caller error - Phase 3's fixed-timestep accumulator is
// what guarantees this never happens in practice).
inline Vec3 ImpliedVelocity(const VerletParticle& particle, float deltaTime) noexcept
{
    return deltaTime > 0.0f ? (particle.position - particle.previousPosition) / deltaTime : Vec3::Zero();
}

} // namespace gte
```

### 3.3 `src/Physics/VerletIntegration.h` / `.cpp`

```cpp
#pragma once
#include "VerletParticle.h"

namespace gte {

// Advances one particle by exactly one FIXED timestep of Position-Verlet
// integration with velocity damping - the "Damping: controls how fast the
// simulated chain stops swinging" parameter. A no-op for a pinned particle
// (its position is authoritative from the animated pose, set by the caller
// every step - see DynamicChainSolver.h, Phase 2).
//
// Formula (Stormer-Verlet with a damping term applied to the IMPLICIT
// velocity, the standard "cheap air-drag" extension - see e.g. Jakobsen's
// "Advanced Character Physics", the reference algorithm every from-scratch
// constrained-particle-chain Verlet implementation is built on):
//
//   velocity    = (position - previousPosition) * (1 - damping)
//   newPosition = position + velocity + acceleration * deltaTime^2
//   previousPosition = position
//   position    = newPosition
//
// `damping` is clamped to [0, 1] internally - 0 means "no energy loss, keeps
// swinging forever" (a real damping of exactly 0 is legal and intentional
// for a test asserting pure energy conservation), 1 means "fully damped,
// stops dead every step, effectively rigid/keyframed-looking".
// `acceleration` is the SUM of every force-as-acceleration this particle
// feels this step (gravity*gravityScale + wind*windScale - see WindField.h -
// summed by the Phase 2 caller, never computed inside this function, which
// stays a pure integrator with no knowledge of what a "chain" or "gravity"
// even is).
void IntegrateParticle(VerletParticle& particle, float deltaTime, const Vec3& acceleration, float damping) noexcept;

} // namespace gte
```

`.cpp`:
```cpp
#include "VerletIntegration.h"
#include "../Math/MathTypes.h" // Clamp

namespace gte {

void IntegrateParticle(VerletParticle& particle, float deltaTime, const Vec3& acceleration, float damping) noexcept
{
    if (particle.pinned) {
        // Pinned particles are driven entirely by the caller (the animated
        // FK target) every step - keep previousPosition in lock-step too, so
        // ImpliedVelocity() never reports spurious motion for an anchor that
        // hasn't actually accelerated, and so a particle that stops being
        // pinned later never "teleports" using a stale previousPosition.
        particle.previousPosition = particle.position;
        return;
    }

    const float clampedDamping = Clamp(damping, 0.0f, 1.0f);
    const Vec3 velocity = (particle.position - particle.previousPosition) * (1.0f - clampedDamping);
    const Vec3 newPosition = particle.position + velocity + acceleration * (deltaTime * deltaTime);

    particle.previousPosition = particle.position;
    particle.position = newPosition;
}

} // namespace gte
```

### 3.4 `src/Physics/ChainConstraints.h` / `.cpp`

Two independent constraint kinds, deliberately named to disambiguate the
two different things "stiffness" could mean (this distinction MUST be
preserved through every later phase — see Phase 2/3's own notes):

```cpp
#pragma once
#include "VerletParticle.h"

namespace gte {

// STRUCTURAL constraint - keeps two connected particles a fixed
// `restLength` apart (their bind-pose bone-segment length - see
// DynamicChainSolver.h, Phase 2). This is NOT the user-facing "Stiffness"
// parameter from the brief - it is what stops a chain segment from
// stretching/compressing at all, standard Position-Based-Dynamics
// projection, mass-weighted so a light tip particle moves more than a
// heavy root-adjacent one:
//
//   delta         = b.position - a.position
//   currentLength = |delta|
//   diff          = (currentLength - restLength) / currentLength
//   invMassSum    = a.inverseMass + b.inverseMass
//   correction    = delta * diff * correctionStrength
//   a.position   += correction * (a.inverseMass / invMassSum)   [skipped if a.pinned]
//   b.position   -= correction * (b.inverseMass / invMassSum)   [skipped if b.pinned]
//
// `correctionStrength` in [0, 1] is a PER-ITERATION relaxation factor
// (1.0 = fully resolve this pass; < 1.0 = softer, springier chain) -
// distinct from the per-joint "Stiffness" the user tunes (see
// SolveGoalConstraint below); a chain's overall rod-rigidity is instead
// controlled by DynamicChainDefinition::constraintIterations (more
// iterations this same step -> effectively stiffer rods - see Phase 2).
// No-op if BOTH particles are pinned, or (v2 fix - see PHASE0's Revision
// Notes, finding #1) more generally whenever
// `a.inverseMass + b.inverseMass <= kEpsilon` for ANY reason (a fully rigid
// segment with nowhere for a correction to go - checked directly,
// independent of the `pinned` flags, so a future caller that sets
// `inverseMass == 0` WITHOUT also setting `pinned = true` still degrades to
// a safe no-op instead of a divide-by-zero producing NaN/Inf - mirrors
// `Vec3.h`'s own `Normalize()` degenerate-input convention). Also a no-op if
// `currentLength` is (near-)zero (degenerate, no well-defined direction).
void SolveDistanceConstraint(VerletParticle& a, VerletParticle& b, float restLength, float correctionStrength = 1.0f) noexcept;

// GOAL constraint - THIS is the brief's "Stiffness: controls how much the
// simulated chain tries to keep its original animated shape" parameter.
// Blends the particle's current (physically-simulated) position toward
// `animatedTargetPosition` - wherever plain forward-kinematics animation
// (no physics at all) would have put this joint this frame:
//
//   position = Lerp(position, animatedTargetPosition, Clamp(stiffness01, 0, 1))
//
// stiffness01 == 0 -> fully free physics, ignores the animated pose
// entirely once simulating; stiffness01 == 1 -> fully rigid, snaps back
// onto the animated pose every step (visually indistinguishable from
// physics being off). A no-op for a pinned particle (already authoritative
// from the animated pose - see IntegrateParticle()'s own pinned branch).
void SolveGoalConstraint(VerletParticle& particle, const Vec3& animatedTargetPosition, float stiffness01) noexcept;

} // namespace gte
```

`.cpp` implements exactly the formulas in the comments above, using
`Length`/`Normalize`/`Lerp` from `Vec3.h` and `Clamp` from `MathTypes.h`.
`SolveDistanceConstraint` computes `invMassSum` FIRST and returns immediately,
without modifying either particle, whenever `invMassSum <= kEpsilon` (v2 fix
— covers both particles pinned AND any other way both end up with
~zero `inverseMass`) — never divide by `invMassSum` before this check.
Separately, also guard `currentLength < kEpsilon` by returning without
modifying either particle (degenerate — no well-defined direction to push
along), mirroring `Vec3.h`'s own `Normalize()` degenerate-input convention.

### 3.5 `src/Physics/WindField.h` / `.cpp`

The **global** ("world physics") parameter surface — see Phase 4 for exactly
where a single, shared instance of this lives:

```cpp
#pragma once
#include "../Math/Vec3.h"

namespace gte {

// One shared, WORLD-level wind description - see PHASE4's own "global vs.
// local" split. Not tied to any one model/chain/joint.
struct WindSettings {
    Vec3 direction = Vec3::Forward(); // normalized by ComputeWindAcceleration() internally - need not be pre-normalized by a caller/Editor field.
    float baseStrength = 0.0f;        // constant push, in acceleration units (m/s^2-equivalent).
    float gustStrength = 0.0f;        // amplitude of the additional oscillating gust term.
    float gustFrequency = 0.5f;       // gust oscillations per second.
    float seedOffset = 0.0f;          // shifts the gust phase per-instance, so two models don't visually swing in perfect unison.
};

// A pure, deterministic function of (settings, worldPosition, timeSeconds) -
// same inputs always produce the exact same output vector, so this is
// directly Tier-1-testable with hand-picked inputs (no RNG/global state of
// any kind - "procedural", not "random"). `worldPosition` feeds a small
// fixed spatial hash into the gust phase purely so two different particles
// in the same chain don't oscillate in perfect lockstep (a visually "dead"/
// rigid-looking wind otherwise) - this is NOT meant to be spatially
// accurate turbulence, just cheap, good-enough visual variation.
//
//   phase       = timeSeconds * gustFrequency * TwoPi + seedOffset + Dot(worldPosition, (12.9898, 78.233, 37.719)) * 0.001
//   gust        = sin(phase) * gustStrength
//   accelerationOut = Normalize(direction) * (baseStrength + gust)
Vec3 ComputeWindAcceleration(const WindSettings& settings, const Vec3& worldPosition, float timeSeconds) noexcept;

} // namespace gte
```

Implement `ComputeWindAcceleration` exactly as documented — `std::sin` from
`<cmath>`, and `gte::kTwoPi` (v2 fix — confirmed already declared in
`Math/MathTypes.h`; #include it and reuse it directly, do NOT declare a
second, local `kTwoPi`/`k2Pi` constant in this file's own `.cpp`).

### 3.6 Tests (Tier 1, land in the same change)

- `tests/Physics/VerletIntegrationTests.cpp`: a particle at rest under zero
  acceleration/zero damping stays exactly at rest; a particle given an
  initial implied velocity (set `previousPosition` one step "behind" its
  `position`) continues in a straight line under zero acceleration/zero
  damping (Newton's first law — the core Verlet correctness check); damping
  of `1.0` fully kills implied velocity after one step; a `pinned` particle
  never moves regardless of `acceleration`/`damping` and its
  `previousPosition` tracks `position` every call; gravity-only free-fall
  over N steps matches the closed-form `0.5 * g * t^2` within a small
  tolerance (validates the integration formula itself, not just the API
  contract).
- `tests/Physics/ChainConstraintsTests.cpp`: two particles stretched
  beyond `restLength` are pulled back together by exactly the mass-weighted
  amount the formula predicts; two particles already at `restLength` are
  left untouched; a particle with one side `pinned` receives 100% of the
  correction, the pinned side receives none; (v2 addition — PHASE0 Revision
  Notes finding #1) two particles that are BOTH `inverseMass == 0` but
  NEITHER has `pinned == true` set are left completely untouched (finite,
  unchanged positions — proves the guard checks `invMassSum` directly and
  never divides by zero, rather than only ever checking the `pinned` flags);
  `SolveGoalConstraint` with `stiffness01 == 0` leaves the particle
  untouched, `== 1` snaps it exactly onto the target, `== 0.5` lands exactly
  halfway.
- `tests/Physics/WindFieldTests.cpp`: same `(settings, worldPosition,
  timeSeconds)` triple always produces the bit-identical output vector
  (determinism check); `baseStrength` alone (zero gust) produces a constant
  vector regardless of `timeSeconds`; two different `worldPosition`s with
  gust enabled produce measurably different phases (proves the spatial-hash
  term is actually wired in, not a dead parameter).
- (v2 addition — PHASE0 Revision Notes finding #6) Add a matching descriptive
  paragraph for each of these three new test files to `tests/CMakeLists.txt`'s
  own header "Test taxonomy" comment block, in the same style/level of
  detail as every existing entry there (e.g. `Math/Vec3Tests.cpp`'s entry) —
  every current Tier-1 test file in this codebase has one; a new file added
  without one breaks that established convention.

## Step 4: What We Will NOT Do

- We will **not** give `VerletParticle` a rotation/orientation of its own —
  it is a pure point mass. Bone rotation is reconstructed afterward, in
  Phase 2, from the relative positions of consecutive particles.
- We will **not** implement angular/bend constraints (e.g. "this joint can't
  bend more than 30°") in this phase — that is a Phase 5 hardening concern,
  not part of the bare-bone foundation.
- We will **not** add collision of any kind here (no ground plane, no head
  sphere) — Phase 5.
- We will **not** wire any of this into `SkeletonData`/ECS/`AnimationSystem`
  yet — this phase must compile and pass its own tests in total isolation.
- We will **not** touch `src/Math/Vec3.h`/`Quat.h`/`Mat4.h` themselves —
  every helper this phase needs that doesn't already exist there is added as
  a free function local to `src/Physics/`, never as a new method bolted onto
  a shared math primitive used by every other subsystem in the engine.

## Step 5: Their Role

Implementer checklist for this phase:

1. Create the `src/Physics/` folder with the six files listed in 3.1.
2. Implement `VerletParticle.h`, `VerletIntegration.h/.cpp`,
   `ChainConstraints.h/.cpp`, `WindField.h/.cpp` exactly per 3.2–3.5.
3. Add all six to `CMakeLists.txt`'s `gte_core` source list.
4. Create `tests/Physics/` with the three test files from 3.6, and register
   them in `tests/CMakeLists.txt`'s Tier-1 (always-built) source list.
5. Build `gte_core` + `GreatTamanaEngineTests` and confirm every new test
   passes before starting Phase 2 — Phase 2's bridge code calls these
   functions directly and cannot be correctness-checked itself if this
   layer is already wrong.
