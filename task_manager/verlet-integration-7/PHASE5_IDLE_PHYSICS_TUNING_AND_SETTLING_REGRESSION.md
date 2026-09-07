# PHASE5 — Idle Physics Tuning And Settling Regression

Part of the `verlet-integration-7` campaign — read `PHASE0_MASTER_STRATEGY.md`
first. Depends on Phases 1-4 already being landed (this phase tunes against
the complete, fully-working, fully-visible pipeline they build). This phase
fixes **Culprit E**.

## Step 1: The Goal (Where are we going?)

The default `DynamicJointSettings` (`damping`/`stiffness`/`mass`) and
`GlobalPhysicsSettings` (`gravity`/`wind`) numeric constants must produce a
believable, STABLE idle result for a model that is NEVER animated: its
hair/skirt must visibly sag away from the bind pose under gravity (not look
"dead"/perfectly rigid), must genuinely settle into a steady shape within a
bounded, short amount of simulated time (not oscillate/jitter forever), and
must never diverge/explode/NaN. This must be proven by a new, automated,
numeric regression test — not just eyeballed once and left untested.

## Step 2: The Situation / The Problem (Where are we now?)

`DynamicJointSettings`'s defaults (`src/Physics/DynamicChainDefinition.h`,
lines 8-12):

```cpp
struct DynamicJointSettings {
    float damping = 0.08f;
    float stiffness = 0.35f;
    float mass = 1.0f;
};
```

`GlobalPhysicsSettings`'s defaults (`src/Physics/GlobalPhysicsSettings.h`,
lines 13-30):

```cpp
Vec3 gravity = Vec3::Down() * 9.8f;
WindSettings wind; // baseStrength = 0.0f, gustStrength = 0.0f by default.
```

Every one of these numbers was chosen and validated (across
`verlet-integration-1` through `verlet-integration-6`) purely by observing
physics riding on TOP of an actively-playing MMD dance animation — large,
constantly-changing bone motion that visually masks a mediocre idle default.
`SolveGoalConstraint()`'s own doc comment (`Physics/ChainConstraints.h`)
states plainly: `stiffness01 == 1` is "visually indistinguishable from
physics being off" — with the current default of `0.35`, a good deal of
"pull back toward bind pose" still happens every single structural-relax
pass, which may make a perfectly-still T-pose chain barely sag at all before
snapping back near-rigidly, OR (depending on the actual interaction with
`constraintIterations = 4` and `damping = 0.08`) may instead oscillate
visibly without ever fully settling — this campaign is the FIRST time this
exact combination is ever exercised against a perfectly static baseline, so
the actual behavior must be measured, not assumed.

## Step 3: The Plan (How will we get there?)

### 3.1 — New test file: `tests/Physics/DynamicChainSolverIdleSettlingTests.cpp`

A pure, fully isolated Tier-1 test (no ECS/`AnimationSystem`/`PhysicsSystem`
at all — calls `StepDynamicChain()` directly, mirroring
`tests/Physics/DynamicChainSolverTests.cpp`'s own existing convention) that:

1. Builds a multi-joint (4-5 joint) linear `DynamicChainDefinition` via
   `DynamicChainDefinition::MakeLinearParentIndices()`, using the DEFAULT
   `DynamicJointSettings{}` for every joint (never overridden), extending
   perpendicular to gravity (same rationale `PhysicsSystemTests.cpp`'s own
   fixture already documents: colinear-with-gravity would only ever
   compress/stretch, never genuinely swing/sag sideways).
2. Holds `rootWorldPosition`/`animatedJointWorldPositions` PERFECTLY
   CONSTANT across every call (this is the literal definition of "a
   perfectly still T-pose" — no animation, no root motion, at the
   `StepDynamicChain()` level).
3. Uses the default `GlobalPhysicsSettings{}`'s own `gravity`/`wind` values
   (read them from a real `GlobalPhysicsSettings{}` instance rather than
   hand-typing the literals, so this test can never silently drift out of
   sync with the real defaults).
4. Steps for a long, fixed simulated duration (e.g. 10 simulated seconds, at
   `GlobalPhysicsSettings{}.fixedTimestepSeconds`, i.e. 600 steps at the
   default 1/60s) and records, every step, the tip joint's distance from its
   own ORIGINAL bind-pose (pre-simulation) position.
5. Asserts, numerically:
   - **Never NaN/Inf** at any step (`IsFinite()`-equivalent check on every
     particle, every step) — proves the tuned defaults never trip the
     solver's own existing NaN/Inf guard under realistic idle conditions.
   - **Not "dead"**: the tip's MAXIMUM distance from its bind-pose position
     across the whole run exceeds a small, deliberately-chosen epsilon (e.g.
     greater than 1% of the chain's own total rest length) — proves gravity
     visibly moves it at all.
   - **Not "exploded"**: that same maximum distance stays below a bounded
     multiple of the chain's own total rest length (e.g. under 1.5x) — a
     chain cannot physically stretch/swing further than its own rod lengths
     allow if the structural constraint is working, so any excess is a sign
     of instability, not motion.
   - **Genuinely settles**: after some bounded prefix of the 600 steps (e.g.
     within the first 300, "half the run"), the PER-STEP delta of the tip's
     own position drops below and STAYS below a small convergence epsilon
     for the remainder of the run (never re-exceeds it) — proves it reaches
     and holds a steady-state shape, rather than oscillating/jittering
     forever.

### 3.2 — Tune the actual defaults until the new test passes with good numbers

Run the Step 3.1 test against TODAY'S literal defaults first. If it fails
(or passes only marginally, e.g. barely-perceptible sag, or a settle time
uncomfortably close to the full 600-step budget), adjust the REAL constants
in:

- `src/Physics/DynamicChainDefinition.h` — `DynamicJointSettings::damping`/
  `stiffness` defaults (`mass` needs no re-tuning — it purely scales
  perceived "weight," not settling behavior/stability).
- `src/Physics/GlobalPhysicsSettings.h` — `gravity` magnitude (keep the
  direction; MMD/PMX convention is already Y-down, do not change it) and,
  if a genuinely "dead"/motionless idle look persists even with reasonable
  gravity/stiffness/damping tuning, introduce a small non-zero DEFAULT
  `WindSettings` (`baseStrength`/`gustStrength`/`gustFrequency`) — a subtle,
  constant ambient sway that keeps a resting chain visibly "alive" the way
  real hair/cloth never sits perfectly, completely motionless.

Iterate the actual numeric literals in these two real header files as real
code edits (not merely prose suggestions) until Step 3.1's test passes with
comfortable margins (not just barely over/under its thresholds) — this
IS the deliverable of this phase, not a suggestion for a human to apply
later.

### 3.3 — Audit existing tests for now-invalidated exact-value assertions

Before changing any default, first run
`search_in_dir`-equivalent auditing (a text search) across
`tests/Physics/DynamicChainSolverTests.cpp`, `tests/Physics/ChainConstraintsTests.cpp`,
`tests/Physics/BoneChainPhysicsResolverTests.cpp`,
`tests/Game/Physics/PhysicsSystemTests.cpp`,
`tests/Game/Physics/PhysicsSystemParallelTests.cpp`, and
`tests/Game/Physics/DynamicChainRigCacheTests.cpp` for any assertion that
depends on the LITERAL default `damping`/`stiffness`/`gravity` value rather
than a weaker "diverges at all"/"stays within bounds" style assertion (the
existing `RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity`
test, for example, only asserts `anyDiffers == true` — tolerant of a tuning
change by construction). Any test found asserting an EXACT expected
post-step position/rotation using today's literal default constants must be
updated, in this SAME change, to match the newly-tuned defaults — never left
silently red. Record which (if any) such tests were found and updated in
this phase's own completion notes.

### 3.4 — Documentation: explain WHY the new numbers exist

Update `GlobalPhysicsSettings.h`'s own doc comment (and
`DynamicChainDefinition.h`'s `DynamicJointSettings` doc comment, if its
defaults changed) to explain the reasoning, e.g.: "tuned specifically so a
completely still, non-animated T-pose chain (task_manager/verlet-integration-7)
settles into a visible, stable, non-oscillating sag within a few seconds —
previous defaults were only ever validated with physics riding on top of an
actively-playing animation, which masked how they behave in true isolation."

### 3.5 — Confirm Phase 3's world-space gravity direction needs no further change

Explicitly re-confirm (no code change needed here, a documentation-only
cross-check): now that Phase 3 simulates in TRUE world space, `gravity`
(`Vec3::Down() * 9.8f`) already correctly points toward real-world-down
regardless of the character's own rotation — a tilted/rotated character's
hair still falls toward genuine down, which is the physically-expected
behavior. Confirm this explicitly in this phase's own completion notes as
"verified, no change needed" rather than silently assuming it.

## Step 4: What We Will NOT Do (Focus, this phase)

- We will **not** touch `Physics/VerletIntegration.cpp`'s or
  `Physics/ChainConstraints.cpp`'s actual formulas — this phase only tunes
  DEFAULT VALUES fed into those already-correct, unmodified functions.
- We will **not** introduce per-chain-type "hair vs. skirt" tuning presets —
  a single, well-tuned global default set, exactly mirroring how
  `DynamicJointSettings`/`GlobalPhysicsSettings` are already structured
  today (a human can still override per-joint values in the Inspector
  afterward, exactly as before).
- We will **not** re-run/re-tune anything from `verlet-integration-1`
  through `verlet-integration-6`'s own already-closed campaigns beyond these
  two specific default-value structs — their own algorithms
  (detection/solver/resolver) remain fully out of scope, per `PHASE0`'s own
  "What We Will NOT Do".
