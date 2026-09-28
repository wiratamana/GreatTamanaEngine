#pragma once

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE2 (PHASE0_MASTER_STRATEGY.md, Finding 4 / LDD-CP3).
// Reused everywhere a human- or AI-supplied name is about to become a
// folder/file/CMake-target name: a Project Assembly's own name (this
// campaign) AND a script/shader asset base name (a later campaign, BIG-
// STEP 4) both funnel through this ONE validator - never a second,
// independently-drifting copy of the same rule.
#include <string>

namespace gte {

// A name is valid if and only if it matches ^[A-Za-z_][A-Za-z0-9_]*$ (a
// safe C++ identifier / CMake target name / Windows filename, with no
// path separators, no "..", no spaces, no reserved punctuation) AND is
// not one of Windows' own reserved device names (case-insensitive): CON,
// PRN, AUX, NUL, COM1-COM9, LPT1-LPT9. Returns true and leaves
// outErrorMessage untouched on success; returns false and fills
// outErrorMessage with a short, human-readable reason otherwise (never
// throws). Deliberately a plain, hand-rolled character scan - never
// <regex> (this codebase has no existing <regex> usage anywhere in
// src/Core/; five lines of std::isalnum/std::isalpha checks already
// solve this cleanly, with no new dependency).
bool IsValidProjectAssemblyIdentifierName(const std::string& name, std::string& outErrorMessage);

} // namespace gte
