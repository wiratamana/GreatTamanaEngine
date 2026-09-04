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
