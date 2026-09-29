# PHASE4 — The Auto-Wire Helper (`TryAutoWireRegisterCall()`)

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first, in full).
Design doc Step: Step 3B of
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
— **read that Step's own full text before starting.**

## Step 1: The Goal

Write the ONE real mechanism that removes the manual "remember to call
`Register<Name>ScreenPass(core);` yourself" step for any project whose own
`<ProjectName>Game.cpp` already carries PHASE2's two anchor comments. This is
a small, pure(ish — it does real file I/O, but takes a path and a string in,
returns a bool, with no other engine dependency) function, independently
Tier-1-testable against real temp files on disk — no live `Core`/GPU/ImGui
needed at all.

## Step 2: The Situation

PHASE2 will have already landed the two anchor comments into
`BuildGameStubCppContent()`'s template (for NEW projects only). This phase's
own function reads whatever real, on-disk `<ProjectName>Game.cpp` PHASE6
hands it and either safely inserts two lines or safely does nothing at all.

`ProjectAssemblyNameValidation.h`'s own documented convention (confirmed via
`EditorProjectLifecycleCapability.cpp`'s own top-of-file style) is: plain
`std::string`/`std::ifstream`/`std::ofstream` scans, **never `<regex>`**, for
any `gte_core`/`gte_editor`-tier code in this system. This function follows
that same convention exactly.

**Mandatory file placement — read before writing any code.** This function
MUST be declared in a real header with EXTERNAL linkage, in its OWN new
file pair, `src/Editor/ScreenPassAutoWire.h`/`.cpp` — NOT inside
`EditorProjectLifecycleCapability.cpp`'s anonymous namespace. This is not a
style preference; it is a hard requirement, confirmed by direct evidence
already in this codebase: `tests/Editor/AssetScaffoldTemplateTests.cpp`
(which already exists) documents, in its own header comment, that every
existing template-builder helper in `EditorProjectLifecycleCapability.cpp`
lives in an UNNAMED (anonymous) namespace and therefore has INTERNAL
linkage — "NOT visible/callable from this separate test .cpp file at all."
Step 4 below requires a brand-new, DEDICATED test file
(`tests/Editor/ScreenPassAutoWireTests.cpp`) that calls
`TryAutoWireRegisterCall()` DIRECTLY, by name, against real temp files — this
is only possible at all if the function has external linkage. Placing it in
the anonymous namespace as the earlier draft of this phase once allowed as an
equally-acceptable option would make Step 4's own required test file FAIL TO
LINK; do not do that.

`EditorCapabilities.h` itself must NOT be touched — this new header is an
internal implementation detail, never part of the public
`IAssetScaffoldingCapability` surface (unchanged from the earlier guidance).

## Step 3: The Plan

### 3.1 — Declaration

New file, `src/Editor/ScreenPassAutoWire.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace gte {

// Returns true if BOTH required insertions were made (or were already
// present - see idempotency note below) - false if EITHER anchor comment
// is missing, in which case the target file is left COMPLETELY UNTOUCHED
// (never a half-patched file).
//
// `registerFunctionName` - e.g. "RegisterFooScreenPass" (PascalCase,
// matching every other generated symbol's own naming convention).
bool TryAutoWireRegisterCall(const std::filesystem::path& projectGameCppPath,
    const std::string& registerFunctionName);

} // namespace gte
```

New file, `src/Editor/ScreenPassAutoWire.cpp`, implements it (real body in
3.2 below). `#include "ScreenPassAutoWire.h"` from
`EditorProjectLifecycleCapability.cpp` (PHASE6 needs this — confirm that
`#include` is actually present once PHASE6 lands, it is easy to forget when
splitting a helper out into its own file mid-campaign).

**Both new files must be added to the root `CMakeLists.txt`'s `gte_editor`
source list.** That list is a plain, explicit, hand-maintained file
enumeration — NOT a `CONFIGURE_DEPENDS`/glob-based list (confirmed: searching
that file for `EditorProjectLifecycleCapability.cpp` finds it listed by name,
around line 1095-1096, alongside `ActiveProjectAssemblyState.h/.cpp`) — a new
`.cpp` file that is never added to this list silently never compiles into
`gte_editor.a` at all, with no error of any kind (the file just sits on disk,
unused). Add the new `ScreenPassAutoWire.h`/`.cpp` pair immediately next to
`EditorProjectLifecycleCapability.h`/`.cpp`'s own existing entry, with a
one-line comment explaining what it is (matching that section's own existing
comment-per-file-group style).

### 3.2 — Real body, in order (per the source design document's own Step 3B)

1. Read the WHOLE file as plain text (`std::ifstream`, slurp into one
   `std::string`) — no `<regex>`. If the file cannot be opened at all
   (missing, permissions), return `false` immediately — nothing written.
2. Plain `std::string::find()` for the two EXACT anchor lines:
   `"// GTE_AUTO_REGISTER_FORWARD_DECLARATIONS - do not remove or edit this line."`
   and
   `"// GTE_AUTO_REGISTER_ANCHOR - do not remove or edit this line."`
   — if EITHER is missing, return `false` immediately. Nothing is written.
3. **Idempotency guard**: split the file into lines and scan them one by
   one; for each line, trim leading whitespace and SKIP it entirely if it
   then starts with `"//"` (a line comment) — this check must never be
   fooled by a PREVIOUSLY auto-wired call a human later commented out (e.g.
   to temporarily disable one effect while debugging another); treating a
   commented-out call as "still wired" would report a false success and
   silently leave that effect permanently disabled with no warning at all.
   If any REMAINING, non-comment line contains
   `registerFunctionName + "(core);"`, return `true` WITHOUT writing
   anything — this project was already correctly, ACTIVELY wired. This is a
   plain line-oriented scan (no `<regex>`) — it does not need to understand
   block comments (`/* ... */`) or string literals, since no real generated
   call line will ever legitimately appear inside either in a file this tool
   itself writes.
4. Insert `"void " + registerFunctionName + "(gte::Core& core);"` on the line
   immediately following the forward-declarations anchor line.
5. Insert `"    " + registerFunctionName + "(core);"` (4-space indent,
   matching this file's own existing body-indentation convention) on the
   line immediately following the `RegisterProject()`-body anchor line.
6. Write the WHOLE modified text back to `projectGameCppPath`
   (`std::ofstream`, complete overwrite) — all-or-nothing; if the write
   itself fails partway, log the failure loudly (`GTE_LOG_ERROR`), but this
   function itself still just returns `false` to its caller — PHASE6's own
   caller-side logic is what decides the user-facing reminder message
   wording for this case (the `<Name>ScreenPass.cpp` file itself was ALREADY
   written successfully before this helper is even called — see PHASE6).

### 3.3 — Implementation guidance: line-based insertion

Since both insertion points are "the line immediately after anchor line X",
the simplest correct implementation:
1. Split the whole file content into a `std::vector<std::string>` of lines
   (splitting on `\n`, being careful to preserve whether the file uses a
   trailing newline — this file is always written by this same engine's own
   `WriteTextFile()`, which never adds a BOM and always ends with `\n`, so a
   straightforward `\n`-delimited split-and-rejoin is safe here; do not
   over-engineer CRLF handling beyond what this codebase's own existing
   `ReplaceAll()`/`WriteTextFile()` helpers already assume, matching their
   established convention).
2. Find the 0-based index of the line whose trimmed content exactly equals
   each anchor's own full text.
3. `insert()` the new line at `index + 1` for each anchor — insert the
   FORWARD-DECLARATION anchor's own new line FIRST if both insertions happen
   in the same pass over the same vector, so the second insertion's own
   target index (computed from the ORIGINAL anchor scan, before any insert
   shifts indices) is still correct — either recompute indices after the
   first insert, or insert from the BOTTOM of the file upward (whichever
   anchor is LOWER in the file, insert there first) so an earlier insertion
   never shifts a not-yet-processed insertion point. Get this right — an
   off-by-one here silently inserts a line into the wrong place, a subtle,
   hard-to-notice defect exactly the kind PHASE8's own byte-for-byte
   verification checkbox exists to catch.
4. Rejoin with `\n` and write.

## Step 4: Required Tier-1 tests (per the source document's own Step 6
Definition-of-Done, brought forward into this phase since the function itself
is being written here)

Add a NEW file, `tests/Editor/ScreenPassAutoWireTests.cpp`, `#include`-ing
`Editor/ScreenPassAutoWire.h` directly (this is exactly why Step 2/3.1 above
mandate external linkage — with the function correctly declared in its own
header, this is a plain, ordinary `#include` + direct call, no different from
any other Tier-1 test in this codebase). **Add this new test file to
`tests/CMakeLists.txt`'s own explicit test-source list** (also a
hand-maintained, non-glob list — confirmed: `Editor/AssetScaffoldTemplateTests.cpp`
is listed there by name, around line 2476) — right next to that existing
entry, since both live in `tests/Editor/` and cover closely related
functionality.

Cover, at minimum, one test case per scenario, each constructing a real
temporary file on disk — reuse `std::filesystem::temp_directory_path()`
(this codebase's own established convention; at least a dozen existing test
files already use it, including `tests/Editor/AssetScaffoldTemplateTests.cpp`'s
own `TempScratchProjectDirectory` helper, which is a good shape to mirror for
a unique-per-test-run scratch file/directory, though this phase's own tests
only need a single scratch FILE, not a whole `Assets/` folder):

1. Both anchors present, `core` named correctly -> returns `true`, and the
   file on disk afterward contains BOTH new lines, in the correct positions,
   with every other original line byte-for-byte unchanged.
2. The forward-declarations anchor is missing (only the body anchor present)
   -> returns `false`, file completely unchanged (compare full file content
   before/after, not just "still exists").
3. The body anchor is missing (only the forward-declarations anchor present)
   -> returns `false`, file completely unchanged.
4. BOTH anchors missing (e.g. an old-style project's template) -> returns
   `false`, file completely unchanged.
5. Idempotent re-run: calling it a second time for the SAME `registerFunctionName`
   against the file it just successfully wired -> returns `true`, writes
   NOTHING new (confirm file content is byte-for-byte identical to the
   post-first-call state).
6. A previously auto-wired call line manually commented out afterward (e.g.
   `// RegisterFooScreenPass(core);`) -> a fresh call correctly treats it as
   NOT currently wired and inserts a fresh, active, UNCOMMENTED call line
   (confirm the file now contains BOTH the old commented-out line AND the
   new active one — this function never deletes/edits an existing line, it
   only ever inserts).
7. Calling it TWICE in a row for two DIFFERENT `registerFunctionName` values
   against the same file -> both forward declarations and both call lines
   are present afterward, each exactly once, neither duplicated, and the
   SECOND call's own insertion point resolution is still correct (the second
   insertion must not be thrown off by the first insertion's own already-
   shifted line indices) — this is the concrete regression test for the
   "insert from the bottom up, or recompute indices" hazard in Step 3.3
   above.
8. A deliberately corrupted/binary-garbage input file (e.g. raw non-UTF8
   bytes with no recognizable anchor text at all) -> fails safely (`false`,
   unchanged), never crashes/throws.
9. A file that genuinely does not exist on disk at all (e.g. a made-up path
   under the same temp directory that was never created) -> `false`,
   obviously nothing written (this is the exact scenario PHASE6/PHASE8's own
   live "old-project fallback" check actually exercises for real against
   `Projects/ProjectAssemblyProbe/`, whose own Game-half source file is
   named `HelloGame.cpp`, NOT `ProjectAssemblyProbeGame.cpp` — see PHASE6's
   own Step 5 note — so the real call there hits THIS code path, "file
   cannot be opened", not the "anchors missing" path; both must return
   `false` safely, and this test case is what proves the file-not-found path
   specifically, independent of PHASE6's own live check).

## Step 5: Verification (this phase only)

1. Incremental build (`cmake --build build`).
2. `ctest -R ScreenPassAutoWire` (or whatever the actual registered test name
   ends up being — confirm via `tests/CMakeLists.txt`'s own registration) —
   all new cases passing.
3. This function is not yet CALLED by any real dispatch path (PHASE6's job) —
   no live HTTP/UI verification is possible or expected in this phase.

## Step 6: Completion

Write `PHASE4_COMPLETION_REPORT.md` in this same folder. `git_add` +
`git_commit` covering the code change (both new `src/Editor/` files, the
`CMakeLists.txt` edit, the new test file, the `tests/CMakeLists.txt` edit) and
the report. Do not run a full build/regression here (Locked Decision 2,
PHASE0). If this phase's own diff feels large/risky by the time it's done,
consider a single `delegate_task(position: "next")` self-double-check of just
this function and its tests before writing the completion report (Locked
Decision 14, PHASE0) — instruct that sub-task to use `ask_questions` for any
ambiguity it finds, and to report back inline without creating its own report
file.
