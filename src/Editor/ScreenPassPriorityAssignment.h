// src/Editor/ScreenPassPriorityAssignment.h
//
// editor-core-separation-24 campaign ("Project Assembly On-Screen Render
// Feature Compositing", BIG-STEP 2 - Editor Integration), PHASE5
// (PHASE5_PRIORITY_AUTO_ASSIGNMENT_HELPER.md).
//
// Declared here, in its OWN header/source pair, with EXTERNAL linkage - NOT
// inside EditorProjectLifecycleCapability.cpp's own anonymous namespace. This
// is not a style preference: tests/Editor/ScreenPassPriorityAssignmentTests.cpp
// needs to call ComputeNextScreenPassPriority() directly, by name, from a
// separate translation unit, which is only possible with external linkage
// (PHASE0_MASTER_STRATEGY.md, Locked Decision 16 - the exact same reasoning
// PHASE4's ScreenPassAutoWire.h/.cpp already documents for
// TryAutoWireRegisterCall()).
#pragma once

#include <cstdint>
#include <filesystem>

namespace gte {

// Scans every regular file directly inside `assetsDirectory` whose filename
// ends with "ScreenPass.cpp" (case-insensitive - e.g. "FooScreenPass.cpp",
// "FOOSCREENPASS.CPP"). For each such file, reads its contents and
// std::string::find()s the literal token "/*priority=*/" (no <regex>
// anywhere); if found, parses the run of ASCII digits (optionally preceded
// by a single '-') immediately following it and folds the result into a
// running maximum. A file that does not contain the token at all, or whose
// text immediately after the token fails to parse as an integer, is silently
// skipped for this computation - it never blocks or fails the scan.
//
// Returns 0 when `assetsDirectory` does not exist yet, or when no sibling
// file contributed a parseable priority; otherwise returns
// (highest parsed priority found among surviving siblings) + 1.
//
// Deliberately "highest surviving priority + 1", NEVER a plain file count -
// a plain count silently breaks the moment any sibling *ScreenPass.cpp file
// is deleted outside the Editor (there is no in-engine delete for a
// scaffolded asset) and a new one is then created, since the count no
// longer reflects which priority numbers are actually still in use. See
// this phase's own PHASE5_PRIORITY_AUTO_ASSIGNMENT_HELPER.md, Step 3.1, for
// the full reasoning.
//
// Read-only - never writes anything, never mutates ActiveProjectAssemblyState,
// and is safe to call from any thread that can safely read the filesystem.
std::int32_t ComputeNextScreenPassPriority(const std::filesystem::path& assetsDirectory);

} // namespace gte
