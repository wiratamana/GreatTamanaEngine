// src/Core/ScreenPostProcessPassPriorityAssignment.h
//
// better-render-pass-1 campaign, PHASE9
// (PHASE9_SCAFFOLDING_FIX_AND_SCREEN_POST_PROCESS_CONVENIENCE_API.md, Part B,
// Decision D3).
//
// Core::AddScreenPostProcessPass() needs its own, independent, RUNTIME
// priority auto-assignment mechanism - a simple, monotonically-incrementing
// counter, shared process-wide (not per-project), read and incremented once
// per call with no explicit `priority` supplied. This is NOT the same
// mechanism as ScreenPassPriorityAssignment.h's own ComputeNextScreenPassPriority()
// (that one is a SCAFFOLD-TIME, file-scanning mechanism with no "sibling
// *ScreenPass.cpp files on disk" to scan against for a hand-written call that
// never goes through the scaffolding tool at all - see this phase's own task
// doc, Step 2, for the full reasoning).
//
// Declared here, in its own header/source pair, with EXTERNAL linkage -
// mirroring ScreenPassPriorityAssignment.h's own "own file, external linkage,
// for testability" precedent - so tests/Core/ScreenPostProcessPassPriorityAssignmentTests.cpp
// can exercise this pure counter-increment logic directly, without needing a
// real, GPU-backed gte::Core instance at all.
#pragma once

#include <cstdint>

namespace gte {

// Returns the CURRENT value of `counter`, then increments it by one (plain
// post-increment semantics) - a simple, monotonically-incrementing, never-
// reset auto-priority source. `counter` is owned entirely by the caller
// (Core::AddScreenPostProcessPass() keeps it as a function-local `static`,
// shared process-wide for the lifetime of the process - see Core.cpp), so
// this function itself is pure and trivially Tier-1-testable with a plain
// local `std::int32_t` variable.
std::int32_t NextAutoScreenPostProcessPassPriority(std::int32_t& counter);

} // namespace gte
