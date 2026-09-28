# PHASE2 — Strict Name Validator & Existing-Route Hardening

Parent: `PHASE0_MASTER_STRATEGY.md` (read first — LDD-CP3, Finding 4).

Depends on: nothing new from PHASE1 (independent piece of work — could run
in parallel with PHASE1 if two implementers were available; done serially
here only because one implementer works through phases in order).
Blocks: PHASE3 (`CreateNewProjectAssembly()` calls
`IsValidProjectAssemblyIdentifierName()` as its very first step).

End state of this phase: a new, dependency-free header/source pair defines
`IsValidProjectAssemblyIdentifierName()`; `NetworkRoutes.cpp`'s existing
`ParseProjectNameQuery()` is hardened to reuse it (LDD-CP3).

---

## STEP 1 — New header: `ProjectAssemblyNameValidation.h/.cpp`

`src/Core/Plugins/ProjectAssemblyNameValidation.h` (new, `gte_core`-tier,
zero dependencies beyond `<string>`):

```cpp
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
```

`src/Core/Plugins/ProjectAssemblyNameValidation.cpp`:

```cpp
#include "ProjectAssemblyNameValidation.h"

#include <array>
#include <cctype>
#include <algorithm>

namespace gte {

namespace {

bool IsAsciiLetterOrUnderscore(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool IsAsciiAlnumOrUnderscore(char c)
{
    return IsAsciiLetterOrUnderscore(c) || (c >= '0' && c <= '9');
}

bool IsReservedWindowsDeviceName(const std::string& name)
{
    // Case-insensitive exact match against the fixed list - COM1-COM9/
    // LPT1-LPT9 checked via a shared prefix+digit test rather than 18
    // separate literals.
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });

    static const std::array<const char*, 4> kFixedNames = { "CON", "PRN", "AUX", "NUL" };
    for (const char* fixedName : kFixedNames) {
        if (upper == fixedName) {
            return true;
        }
    }
    if (upper.size() == 4 && (upper.rfind("COM", 0) == 0 || upper.rfind("LPT", 0) == 0)) {
        const char lastChar = upper.back();
        if (lastChar >= '1' && lastChar <= '9') {
            return true;
        }
    }
    return false;
}

} // namespace

bool IsValidProjectAssemblyIdentifierName(const std::string& name, std::string& outErrorMessage)
{
    if (name.empty()) {
        outErrorMessage = "name must not be empty";
        return false;
    }
    if (!IsAsciiLetterOrUnderscore(name.front())) {
        outErrorMessage = "name must start with a letter or underscore";
        return false;
    }
    for (char c : name) {
        if (!IsAsciiAlnumOrUnderscore(c)) {
            outErrorMessage = "name may only contain letters, digits, and underscores (no spaces, '.', '/', '\\')";
            return false;
        }
    }
    if (IsReservedWindowsDeviceName(name)) {
        outErrorMessage = "'" + name + "' is a reserved Windows device name and cannot be used";
        return false;
    }
    return true;
}

} // namespace gte
```

Register both new files in root `CMakeLists.txt`'s `gte_core` source list,
immediately after `src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`
(confirmed lines 358-359 today), mirroring that entry's own comment style.

## STEP 2 — Tier-1 tests (new file)

`tests/Core/Plugins/ProjectAssemblyNameValidationTests.cpp` (new,
zero-fixture, pure logic — no filesystem/CMake/engine state needed at all):

```cpp
#include "Core/Plugins/ProjectAssemblyNameValidation.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

bool IsValid(const std::string& name)
{
    std::string ignored;
    return IsValidProjectAssemblyIdentifierName(name, ignored);
}

} // namespace

TEST(ProjectAssemblyNameValidationTest, RejectsEmptyString) { EXPECT_FALSE(IsValid("")); }
TEST(ProjectAssemblyNameValidationTest, RejectsLeadingDigit) { EXPECT_FALSE(IsValid("1Project")); }
TEST(ProjectAssemblyNameValidationTest, RejectsEmbeddedSpace) { EXPECT_FALSE(IsValid("My Project")); }
TEST(ProjectAssemblyNameValidationTest, RejectsDotDot) { EXPECT_FALSE(IsValid("../evil")); }
TEST(ProjectAssemblyNameValidationTest, RejectsForwardSlash) { EXPECT_FALSE(IsValid("a/b")); }
TEST(ProjectAssemblyNameValidationTest, RejectsBackslash) { EXPECT_FALSE(IsValid("a\\b")); }

TEST(ProjectAssemblyNameValidationTest, RejectsReservedDeviceNamesExactCase)
{
    EXPECT_FALSE(IsValid("CON"));
    EXPECT_FALSE(IsValid("PRN"));
    EXPECT_FALSE(IsValid("AUX"));
    EXPECT_FALSE(IsValid("NUL"));
    EXPECT_FALSE(IsValid("COM1"));
    EXPECT_FALSE(IsValid("COM9"));
    EXPECT_FALSE(IsValid("LPT1"));
    EXPECT_FALSE(IsValid("LPT9"));
}

TEST(ProjectAssemblyNameValidationTest, RejectsReservedDeviceNamesMixedCase)
{
    EXPECT_FALSE(IsValid("con"));
    EXPECT_FALSE(IsValid("CoN"));
    EXPECT_FALSE(IsValid("com3"));
}

TEST(ProjectAssemblyNameValidationTest, AcceptsGenuinelyValidNames)
{
    EXPECT_TRUE(IsValid("MyGame"));
    EXPECT_TRUE(IsValid("_LeadingUnderscore"));
    EXPECT_TRUE(IsValid("_"));
    EXPECT_TRUE(IsValid("Project_Assembly_2"));
    EXPECT_TRUE(IsValid("ProjectAssemblyProbe")); // the one real, existing project name - must never regress.
}

TEST(ProjectAssemblyNameValidationTest, ErrorMessageIsPopulatedOnlyOnFailure)
{
    std::string message = "sentinel-must-be-cleared-by-caller-not-this-function";
    EXPECT_TRUE(IsValidProjectAssemblyIdentifierName("ValidName", message));
    EXPECT_EQ(message, "sentinel-must-be-cleared-by-caller-not-this-function"); // untouched on success.

    std::string failureMessage;
    EXPECT_FALSE(IsValidProjectAssemblyIdentifierName("", failureMessage));
    EXPECT_FALSE(failureMessage.empty());
}

} // namespace gte
```

Add this file to `tests/CMakeLists.txt`'s hand-maintained list, alongside
the other `Core/Plugins/*Tests.cpp` entries.

## STEP 3 — LDD-CP3: harden `ParseProjectNameQuery()` in place

Confirmed real, current body, `src/Network/NetworkRoutes.cpp` lines
1201-1211:

```cpp
ParsedProjectNameQuery ParseProjectNameQuery(const std::string& nameParam)
{
    ParsedProjectNameQuery parsed;
    if (nameParam.empty()) {
        parsed.errorMessage = "'name' query parameter is required";
        return parsed;
    }
    parsed.projectName = nameParam;
    parsed.valid = true;
    return parsed;
}
```

New body — reuses `IsValidProjectAssemblyIdentifierName()` (this phase's
own new function) instead of only the empty-check:

```cpp
ParsedProjectNameQuery ParseProjectNameQuery(const std::string& nameParam)
{
    ParsedProjectNameQuery parsed;
    std::string validationError;
    if (!IsValidProjectAssemblyIdentifierName(nameParam, validationError)) {
        parsed.errorMessage = nameParam.empty() ? "'name' query parameter is required" : validationError;
        return parsed;
    }
    parsed.projectName = nameParam;
    parsed.valid = true;
    return parsed;
}
```

(The `nameParam.empty()` special-case preserves this function's own
EXISTING, already-relied-upon exact error message string for the empty
case — confirm no existing test asserts on the OTHER, generic
`IsValidProjectAssemblyIdentifierName()` empty-string message instead
before assuming this distinction is unnecessary; keeping it costs nothing
and avoids any risk of an existing test's exact-string assertion breaking.)

Add `#include "Core/Plugins/ProjectAssemblyNameValidation.h"` to
`NetworkRoutes.cpp`'s own include list.

**Verification for this one, small, existing-code change**: `search_in_dir`
for `ParseProjectNameQuery` across `tests/` to find every existing test
that exercises it (candidates: anything under
`tests/Network/`). Re-run exactly those existing test(s) (targeted `ctest
-R`/filter run, not a full suite) and confirm every one still passes
unchanged — every existing real project name in this repo
(`"ProjectAssemblyProbe"`) already satisfies the stricter rule trivially,
so this must be a silent, pure hardening with zero fallout; if anything
existing actually fails, STOP and investigate before proceeding to PHASE3
(a genuine regression here would mean this "recommended, low-risk"
hardening is not actually low-risk, and needs its own separate decision).

## Definition of Done — this phase only

- [ ] `IsValidProjectAssemblyIdentifierName()` exists, compiles, and every
      test in `ProjectAssemblyNameValidationTests.cpp` passes.
- [ ] `ParseProjectNameQuery()`'s body now calls the new validator; every
      pre-existing test that exercises it (found via `search_in_dir`)
      still passes, confirmed by an actual targeted run, not assumed.
- [ ] Both new files are registered in their respective `CMakeLists.txt`s.
- [ ] Zero new compiler warnings.

## What this phase does NOT do

- Does NOT touch `CreateNewProjectAssembly()` or anything else that calls
  this new validator for the FIRST time in a WRITE path — that begins in
  PHASE3.
- Does NOT run a full `ctest` regression pass — only the specific,
  targeted existing test(s) STEP 3 identifies.
