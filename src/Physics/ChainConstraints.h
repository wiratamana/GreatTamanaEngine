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
