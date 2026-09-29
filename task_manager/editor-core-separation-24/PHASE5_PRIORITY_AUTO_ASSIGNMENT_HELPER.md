# PHASE5 — The Priority Auto-Assignment Helper (`ComputeNextScreenPassPriority()`)

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first, in full).
Design doc Step: Step 4 of
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
(the `ComputeNextScreenPassPriority()` sub-section specifically — read it in
full before starting).

## Step 1: The Goal

Write the ONE function that lets scaffolding a SECOND (or third, fourth, ...)
Screen Post-Process Pass into the same project — using ONLY the Create menu,
zero manual C++ editing — never land on the exact same
`RenderFeatureStage::PostComposite` + priority pair as an earlier one,
including correctly across a delete-one-then-create-another-one churn
pattern (this campaign's own explicit, permanent design boundary: no
in-engine delete/rename of a scaffolded asset exists, so this arithmetic must
independently stay correct across manual, off-engine file deletions).

## Step 2: The Situation

No such function exists yet. `EditorProjectLifecycleCapability.cpp` already
has an established, reusable convention for a directory scan
(`CreateAssetScaffold()`'s own existing case-insensitive collision scan,
lines 606-621, using `std::filesystem::directory_iterator` +
`ToLowerAscii()`) and a `std::optional`-free, exception-free parsing
discipline is expected (this whole file avoids exceptions as control flow —
confirmed, no `try`/`catch` appears anywhere in it).

**Mandatory file placement — read before writing any code.** Exactly like
PHASE4's `TryAutoWireRegisterCall()`, this function MUST be declared in a
real header with EXTERNAL linkage, in its OWN new file pair,
`src/Editor/ScreenPassPriorityAssignment.h`/`.cpp` — NOT inside
`EditorProjectLifecycleCapability.cpp`'s anonymous namespace. Step 4 below
requires a brand-new, DEDICATED test file
(`tests/Editor/ScreenPassPriorityAssignmentTests.cpp`) that calls
`ComputeNextScreenPassPriority()` DIRECTLY, by name — this is only possible
if the function has external linkage. See PHASE4's own Step 2 for the full,
confirmed evidence behind this rule
(`tests/Editor/AssetScaffoldTemplateTests.cpp`'s own header comment
explicitly documents that every existing anonymous-namespace helper in
`EditorProjectLifecycleCapability.cpp` is UNCALLABLE from a separate test
.cpp file) — it applies identically here; do not place this function inside
that anonymous namespace instead.

`ToLowerAscii()` itself (used by this function, per 3.2 below) DOES live
inside `EditorProjectLifecycleCapability.cpp`'s own anonymous namespace today
and has internal linkage — this new file cannot call it directly. Either (a)
duplicate a tiny, private, file-local lowercase helper inside
`ScreenPassPriorityAssignment.cpp` itself (simplest, and consistent with this
codebase's general tolerance for small, single-purpose, non-shared helpers —
confirm this is genuinely the smaller diff before choosing it), or (b) if the
implementer prefers, promote `ToLowerAscii()` itself to
`EditorProjectLifecycleCapability.h`'s public surface (or a tiny, shared,
dependency-free string-utility header) so both files can use the ONE real
implementation — either approach is acceptable; pick whichever produces the
smaller, clearer diff and record the choice in the completion report. Do NOT
silently duplicate `ToLowerAscii()` byte-for-byte AND leave the original
in place without a comment noting the duplication exists and why — a future
reader must not mistake this for an accidental copy-paste that drifted.

## Step 3: The Plan

### 3.1 — Why "highest surviving priority + 1", never a plain file count

A plain file count silently breaks the moment ANY sibling `*ScreenPass.cpp`
file is deleted (outside the Editor — there is no in-engine delete for a
scaffolded asset) and a new one is then created, since the count no longer
reflects which priority numbers are actually still in use by the SURVIVING
files — a fresh scaffold could then be assigned a priority a surviving
sibling already holds, a real, silent, same-stage collision. Reading back the
highest priority literal actually present and assigning one more than that is
immune to this failure mode: the first Screen Post-Process Pass ever
scaffolded into a project still gets priority `0`, but every later one gets
`(highest surviving priority) + 1`, regardless of how many were deleted along
the way.

### 3.2 — Declaration and body

New file, `src/Editor/ScreenPassPriorityAssignment.h`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>

namespace gte {

// A plain std::filesystem::directory_iterator scan over every regular file
// whose filename ends with "ScreenPass.cpp" (case-insensitive). For each such
// file, reads its contents and std::string::find()s the literal token
// "/*priority=*/" (no <regex>); if found, parses the run of ASCII digits
// (optionally preceded by a single '-') immediately following it via
// std::from_chars (or an equivalent plain, exception-free integer parse) and
// folds it into a running maximum. A file that does not contain the token at
// all, or whose text immediately after the token fails to parse as an
// integer, is silently skipped for this computation - it never blocks or
// fails the scan. Returns 0 when assetsDirectory does not exist yet, or when
// no sibling file contributed a parseable priority; otherwise returns
// (highest parsed priority found) + 1.
std::int32_t ComputeNextScreenPassPriority(const std::filesystem::path& assetsDirectory);

} // namespace gte
```

New file, `src/Editor/ScreenPassPriorityAssignment.cpp`:

```cpp
#include "ScreenPassPriorityAssignment.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <string>

namespace gte {

namespace {
// File-local lowercase helper (see this phase's own Step 2 note on why this
// is NOT the same ToLowerAscii() EditorProjectLifecycleCapability.cpp uses
// internally - that one has internal linkage and is not reachable from here).
std::string ToLowerAsciiLocal(const std::string& text)
{
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
} // namespace

std::int32_t ComputeNextScreenPassPriority(const std::filesystem::path& assetsDirectory)
{
    std::error_code iterationError;
    if (!std::filesystem::is_directory(assetsDirectory, iterationError)) {
        return 0;
    }

    static const std::string kSuffix = "screenpass.cpp"; // already-lowered, matches ToLowerAsciiLocal(filename) below.
    static const std::string kToken = "/*priority=*/";

    bool foundAny = false;
    std::int32_t highest = 0;

    for (const auto& entry : std::filesystem::directory_iterator(assetsDirectory, iterationError)) {
        if (!entry.is_regular_file()) continue;
        const std::string lowerName = ToLowerAsciiLocal(entry.path().filename().string());
        if (lowerName.size() < kSuffix.size()
            || lowerName.compare(lowerName.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
            continue;
        }

        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream.is_open()) continue;
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

        const std::size_t tokenPos = content.find(kToken);
        if (tokenPos == std::string::npos) continue;

        std::size_t digitsStart = tokenPos + kToken.size();
        std::size_t digitsEnd = digitsStart;
        if (digitsEnd < content.size() && content[digitsEnd] == '-') ++digitsEnd;
        while (digitsEnd < content.size() && std::isdigit(static_cast<unsigned char>(content[digitsEnd]))) {
            ++digitsEnd;
        }
        if (digitsEnd == digitsStart) continue; // token present but nothing parseable followed it.

        std::int32_t parsed = 0;
        const auto parseResult = std::from_chars(
            content.data() + digitsStart, content.data() + digitsEnd, parsed);
        if (parseResult.ec != std::errc{}) continue; // failed to parse - skip, never crash the scan.

        if (!foundAny || parsed > highest) {
            highest = parsed;
        }
        foundAny = true;
    }

    return foundAny ? (highest + 1) : 0;
}

} // namespace gte
```

Notes:
- `#include <charconv>` (for `std::from_chars`) is a fresh include in this new
  file — no double-include risk since this is a brand-new translation unit.
- The filename-suffix check deliberately compares against the ALREADY-LOWERED
  `lowerName` — never a case-sensitive `ends_with`.
- This function is read-only — it never writes anything, never mutates
  `ActiveProjectAssemblyState`, and is safe to call from any thread that can
  safely read the filesystem (mirrors `CreateAssetScaffold()`'s own existing
  collision-scan's thread-safety contract).

**Both new files must be added to the root `CMakeLists.txt`'s `gte_editor`
source list** — the same explicit, hand-maintained, non-glob list PHASE4's
own `ScreenPassAutoWire.h`/`.cpp` must be added to (confirmed around line
1095-1096, next to `EditorProjectLifecycleCapability.h`/`.cpp`'s own entry).
Add this pair immediately next to that same entry too, or next to PHASE4's
own newly-added pair if that phase has already landed by the time this one
starts.

## Step 4: Required Tier-1 tests (per the source document's own Step 6
Definition-of-Done, brought forward into this phase)

Add a NEW file, `tests/Editor/ScreenPassPriorityAssignmentTests.cpp`,
`#include`-ing `Editor/ScreenPassPriorityAssignment.h` directly and calling
`ComputeNextScreenPassPriority()` by name. **Add this new test file to
`tests/CMakeLists.txt`'s own explicit test-source list** (the same
hand-maintained, non-glob list PHASE4's own `ScreenPassAutoWireTests.cpp`
must be added to — confirm both are actually present in that list before this
phase's own build/test verification step below, since forgetting to register
a new test file produces NO error at all, it simply never runs).

Cover, using real temporary directories/files on disk (reuse
`std::filesystem::temp_directory_path()`, this codebase's own established
convention — see PHASE4's own Step 4 for further precedent; do not invent a
second, different temp-directory convention):

1. An empty/missing `Assets/` folder -> returns `0`.
2. N existing `*ScreenPass.cpp` files with priorities `0..N-1` -> returns `N`.
3. Case-insensitive filename matching (`FooSCREENPASS.CPP` still counts).
4. Unrelated files (e.g. `Foo.cpp`, `FooRenderPass.cpp`, `FooScreenPassBackup.txt`)
   are never counted.
5. A sibling file whose MIDDLE priority was deleted (e.g. priorities `0, 1, 2`
   existed, the file holding priority `1` was deleted from disk) still yields
   `(highest surviving priority) + 1` == `3`, never a plain re-numbered count
   (`2`) — this is the concrete regression test for the exact scenario Step
   3.1 above exists to prevent.
6. A sibling file with no parseable `"/*priority=*/"` token at all (e.g. a
   hand-edited file where that comment was removed or reworded) is silently
   skipped rather than treated as priority `0` or crashing the scan — confirm
   the OVERALL scan still correctly returns `(highest of the OTHER, parseable
   siblings) + 1`.
7. A sibling file containing the token followed by non-digit garbage (e.g.
   `/*priority=*/abc`) is skipped the same way, never crashing.
8. A negative priority literal (e.g. `/*priority=*/-5`) parses correctly and
   participates in the max computation like any other value (a hand-edited
   negative priority is unusual but not disallowed, per the source document's
   own text).

## Step 5: Verification (this phase only)

1. Incremental build (`cmake --build build`).
2. `ctest -R ScreenPassPriority` (or whatever the actual registered test name
   ends up being) — all new cases passing.
3. This function is not yet CALLED by any real dispatch path (PHASE6's job) —
   no live HTTP/UI verification is possible or expected in this phase.

## Step 6: Completion

Write `PHASE5_COMPLETION_REPORT.md` in this same folder. `git_add` +
`git_commit` covering the code change (both new `src/Editor/` files, the
`CMakeLists.txt` edit, the new test file, the `tests/CMakeLists.txt` edit) and
the report. Do not run a full build/regression here (Locked Decision 2,
PHASE0).
