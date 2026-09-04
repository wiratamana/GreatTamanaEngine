#pragma once

namespace gte {

// How many fixed-size physics steps to run this frame, given how much real
// time elapsed and how much "leftover" time is already banked from previous
// frames - the standard "accumulator pattern" every fixed-timestep game loop
// tutorial documents (e.g. Glenn Fiedler's "Fix Your Timestep!"), applied
// here narrowly to dynamic-chain stepping rather than the whole engine's
// frame loop (Application::Run() itself stays variable-timestep - this is
// scoped ONLY to Physics/ stepping, see
// task_manager/verlet-integration-1/PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md's
// own Culprit D).
//
// `accumulatedSeconds` (in/out) is DynamicChainRig::accumulatedSeconds -
// incremented by `frameDeltaSeconds`, then decremented by `fixedTimestep`
// once per returned step. `maxStepsPerFrame` bounds the return value (a
// spiral-of-death guard - e.g. a 5-second Editor breakpoint pause must never
// demand 300 catch-up steps in one call); any leftover time beyond what
// `maxStepsPerFrame` can drain is simply DISCARDED (accumulatedSeconds
// clamped back down), which is the standard, accepted trade for this pattern
// - the simulation is allowed to visibly "skip ahead" after a huge stall, it
// must never spend seconds of real time catching up in a single Update()
// call. Returns 0 (and leaves accumulatedSeconds untouched beyond the
// increment) whenever fixedTimestep <= 0 - a degenerate/misconfigured value
// must never divide by zero or infinite-loop.
int ComputeFixedStepCount(float& accumulatedSeconds, float frameDeltaSeconds, float fixedTimestep, int maxStepsPerFrame) noexcept;

} // namespace gte
