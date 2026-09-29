# PHASE6 — `CreateAssetScaffold()`'s New Dispatch Branch (wiring everything together)

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first, in full).
Design doc Step: Step 4 of
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
(the `CreateAssetScaffold()`-body sub-section specifically).

**This is the single heaviest phase in this campaign** — it is the first
point where PHASE1 (enum/UI), PHASE2 (template anchors), PHASE3 (generated
template), PHASE4 (auto-wire helper), and PHASE5 (priority helper) all get
exercised together, for real, for the first time. Read every one of those
five phases' own completion reports before starting this one — a subtle
mismatch discovered here (e.g. an anchor string typo from PHASE2, an
off-by-one from PHASE4, a renamed function/file from either) is exactly the
kind of thing this phase's own end-to-end nature is designed to surface.

## Step 1: The Goal

Make "Create -> Screen Post-Process Pass..." a genuinely, fully working
feature for the first time in this campaign: typing a name and clicking
"Create" (or `POST /project_assembly/create_asset?kind=screen_post_process_pass`,
once PHASE7 lands) really writes `Assets/<Name>ScreenPass.cpp`, really
computes a correct, collision-free priority, really attempts to auto-wire the
call into `RegisterProject()`, and reports an honest, dynamic reminder message
either way.

## Step 2: The Situation

`src/Editor/EditorProjectLifecycleCapability.cpp`'s
`CreateAssetScaffold(AssetScaffoldKind kind, const std::string& name)`
(lines 587-645) today:

```cpp
IAssetScaffoldingCapability::ScaffoldOutcome EditorProjectLifecycleCapability::CreateAssetScaffold(
    AssetScaffoldKind kind, const std::string& name)
{
    ScaffoldOutcome outcome;

    const ActiveProjectAssemblyInfo active = ActiveProjectAssemblyState::Instance().GetActive();
    if (!active.hasActiveProject) {
        outcome.errorMessage = "no active project - use New Project or Open Project first";
        return outcome;
    }

    std::string validationError;
    if (!IsValidProjectAssemblyIdentifierName(name, validationError)) {
        outcome.errorMessage = validationError;
        return outcome;
    }

    const std::vector<ScaffoldFileSpec> fileSpecs = BuildScaffoldFileSpecs(kind, name);
    // ... collision scan, write loop, ReminderMessageForKind() ...
}
```

`ActiveProjectAssemblyInfo` (confirmed via `ActiveProjectAssemblyState.h`)
exposes `.hasActiveProject`, `.name`, `.assetsDirectory` — all already used by
the existing generic flow above; this phase's new branch reuses the SAME
`active` local, already resolved at the top of the function.

**By the time this phase starts, PHASE4/PHASE5 will have placed
`TryAutoWireRegisterCall()`/`ComputeNextScreenPassPriority()` in their own new
headers, `src/Editor/ScreenPassAutoWire.h`/`src/Editor/ScreenPassPriorityAssignment.h`
(external linkage — required so each has its own dedicated Tier-1 test file;
see those phases' own Step 2 for why). This phase MUST add both**:

```cpp
#include "ScreenPassAutoWire.h"
#include "ScreenPassPriorityAssignment.h"
```

**to `EditorProjectLifecycleCapability.cpp`'s own include list** — without
these, the calls to `TryAutoWireRegisterCall()`/`ComputeNextScreenPassPriority()`
below will not compile at all (a plain, unmistakable "undeclared identifier"
error — but confirm this explicitly rather than being surprised by it, since
this exact include is easy to forget when the three earlier phases' own
completion reports are read out of context of each other).

## Step 3: The Plan

Insert a NEW, dedicated branch INSIDE `CreateAssetScaffold()`, immediately
AFTER the existing "no active project" and name-validation checks (so both of
those checks are shared, never duplicated), but BEFORE
`BuildScaffoldFileSpecs(kind, name)` is called — this is what makes the new
kind never actually reach the generic flow at all:

```cpp
if (kind == AssetScaffoldKind::ScreenPostProcessPass) {
    // The generated template registers this feature under the debug
    // name "<Name>.ScreenTint" - GtePluginRenderFeatureDescriptor::name
    // is a fixed char[64] (63 usable bytes). Checking this HERE, before
    // writing any file, gives the user a clear, immediate "name too long"
    // error at scaffold time, rather than a silent truncation discovered
    // only later, at compile-and-run time, inside
    // Core::RegisterProjectRenderFeature() (which itself independently
    // refuses an over-length debugName rather than truncating it - BIG-STEP
    // 1 - this check merely surfaces the SAME limit earlier, with a
    // friendlier, scaffold-time error message).
    static const std::string kDebugNameSuffix = ".ScreenTint";
    if (name.size() + kDebugNameSuffix.size() > 63) {
        outcome.errorMessage = "name is too long - \"" + name + kDebugNameSuffix
            + "\" would exceed the engine's 63-character render feature name limit; choose a shorter name";
        return outcome;
    }

    const std::filesystem::path filePath = active.assetsDirectory / (name + "ScreenPass.cpp");
    if (std::filesystem::exists(filePath)) {
        outcome.errorMessage = "a file named '" + filePath.filename().string() + "' already exists";
        return outcome;
    }

    // Best-effort directory creation, mirroring the generic flow's own
    // create_directories() tolerance (CreateAssetScaffold()'s existing
    // generic-kind path, lines 628-629) for a hand-authored/externally-copied
    // project that is missing Assets/ - this MUST run before WriteTextFile()
    // below, whose std::ofstream open would otherwise silently fail if
    // Assets/ does not exist yet (ComputeNextScreenPassPriority() itself
    // already tolerates a missing directory by returning 0, but WriteTextFile()
    // does not, so this call is still required ahead of it).
    std::error_code createDirectoriesError;
    std::filesystem::create_directories(active.assetsDirectory, createDirectoriesError);

    const std::int32_t priority = ComputeNextScreenPassPriority(active.assetsDirectory);

    const std::string content = BuildScreenPostProcessPassCppContent(name, priority);
    if (!WriteTextFile(filePath, content)) {
        outcome.errorMessage = "failed to write '" + filePath.filename().string() + "'";
        return outcome;
    }

    const std::string registerFn = "Register" + name + "ScreenPass";
    const std::filesystem::path gameCppPath = active.assetsDirectory / (active.name + "Game.cpp");
    const bool autoWired = TryAutoWireRegisterCall(gameCppPath, registerFn);

    outcome.success = true;
    outcome.createdFiles = { filePath.filename().string() };
    outcome.reminderMessage = autoWired
        ? ("Created Assets/" + filePath.filename().string()
              + " and automatically wired " + registerFn + "(core) into your project's RegisterProject() - "
              "compile to see the tint live!")
        : ("Created Assets/" + filePath.filename().string()
              + " - could not auto-wire it in (this project may pre-date auto-wiring support) - remember to call "
              + registerFn + "(core) from your project's RegisterProject() yourself!");
    GTE_LOG_INFO("ProjectLifecycle", "CreateAssetScaffold('" + name + "', ScreenPostProcessPass): "
        + outcome.reminderMessage);
    return outcome;
}

// Existing generic flow, unchanged, for RenderPass/ComputeShader/ShaderPair:
const std::vector<ScaffoldFileSpec> fileSpecs = BuildScaffoldFileSpecs(kind, name);
// ... collision scan, write loop, ReminderMessageForKind() ... (byte-for-byte unchanged)
```

Cross-check against every earlier phase before writing this, precisely:

- `ComputeNextScreenPassPriority`/`TryAutoWireRegisterCall`/
  `BuildScreenPostProcessPassCppContent` must all resolve to PHASE3/4/5's own
  exact function names and signatures — if any phase's own implementer
  changed a name/signature while actually writing the code (reasonable, e.g.
  during its own debugging), update this call site to match reality, not the
  other way around. `BuildScreenPostProcessPassCppContent()` remains in
  `EditorProjectLifecycleCapability.cpp`'s own anonymous namespace (PHASE3),
  so it needs no new `#include` here — but
  `ComputeNextScreenPassPriority()`/`TryAutoWireRegisterCall()` DO, per the
  Step 2 note above.
- `WriteTextFile()` is the SAME existing helper the generic flow already uses
  (lines 84-92) — do not write a second, parallel file-write helper.
- The `filePath.exists()` collision check here is DELIBERATELY simpler than
  the generic flow's own case-insensitive, multi-file collision scan (this
  branch only ever produces ONE file, `<Name>ScreenPass.cpp`) — a plain
  `std::filesystem::exists()` is sufficient and correct; do not port over the
  generic flow's `ToLowerAscii()` machinery unless a real Windows
  case-sensitivity gap is found during Step 4's own testing (unlikely — the
  underlying filesystem is already case-insensitive on this OS, which is
  exactly what `std::filesystem::exists()` already respects natively; the
  generic flow's own extra `ToLowerAscii()` scan exists for a DIFFERENT
  reason — checking MULTIPLE candidate filenames from one call against a
  pre-enumerated list — not needed here for a single-file check).
- `active.assetsDirectory` is NOT guaranteed to already exist (a
  hand-authored/externally-copied project might be missing `Assets/`) — the
  code above already calls
  `std::filesystem::create_directories(active.assetsDirectory, ...)`
  (best-effort, mirroring the generic flow's own identical call) BEFORE both
  `ComputeNextScreenPassPriority()` and `WriteTextFile()`, so neither one is
  ever handed a directory that might not exist yet. Do not remove this call
  even though `ComputeNextScreenPassPriority()` itself also tolerates a
  missing directory on its own (returns `0`) — `WriteTextFile()` does NOT,
  and would otherwise silently fail via `std::ofstream::is_open() == false`.

## Step 4: Required additional Tier-1 test

`tests/Editor/AssetScaffoldTemplateTests.cpp` already exists (confirmed) and
already carries `TempScratchProjectDirectory`/`ActiveProjectStateRestorer`
helper classes purpose-built for exactly this kind of test — EXTEND this
existing file (do not create a new one; no `tests/CMakeLists.txt` change is
needed since the file is already registered there) with new
`TEST(AssetScaffoldTemplateTest, ...)` cases covering
`AssetScaffoldKind::ScreenPostProcessPass` end to end through the real,
public `CreateAssetScaffold()` entry point, reusing those same two helper
classes exactly like every existing test in that file already does. At
minimum:

1. Per the source document's own Step 6 Definition-of-Done: a name that would
   push the generated debug name past 63 characters is rejected, with ZERO
   files written (confirm no `Assets/<Name>ScreenPass.cpp` appears on disk
   after the rejected call).
2. A first successful scaffold into a scratch project WITH both PHASE2 anchors
   present in its own `<ScratchProjectName>Game.cpp` (write that file
   yourself, by hand, inside the test, with the two exact anchor strings —
   this test fixture is not a real `CreateNewProjectAssembly()`-created
   project) — confirms `outcome.success`, the generated file's own
   `/*priority=*/0,` literal, AND that the scratch project's own Game.cpp now
   contains the correct forward declaration + call line.
3. A second scaffold (different name) into the SAME scratch project — confirms
   `/*priority=*/1,`, and confirms the FIRST scaffold's own forward
   declaration/call line is still present, unduplicated, alongside the new
   one.
4. A scaffold into a scratch project whose `<ScratchProjectName>Game.cpp`
   either does not exist at all, or exists but has no anchors — confirms
   `outcome.success` is STILL `true` (the `.cpp` file itself is written either
   way), but `outcome.reminderMessage` carries the fallback wording, and (for
   the "exists but no anchors" case) that file is confirmed byte-for-byte
   unchanged afterward.

## Step 5: Verification (this phase only)

1. Incremental build (`cmake --build build`).
2. `ctest` targeted at every test file this campaign has added so far
   (PHASE4/PHASE5's own new test files, plus this phase's own extension of
   `AssetScaffoldTemplateTests.cpp`) — all passing.
3. Live, end-to-end manual smoke test (this is the FIRST point in the
   campaign where this is meaningful): `run_app_background` the built
   Editor, create a brand-new throwaway test project (so PHASE2's anchors are
   present), open it, right-click -> Create -> Screen Post-Process Pass,
   type a name, click Create. Confirm:
   - The reminder message reports successful auto-wiring.
   - `read_file` the generated `Assets/<Name>ScreenPass.cpp` and the
     project's own `Assets/<ProjectName>Game.cpp` — confirm both look
     correct, byte-for-byte matching what PHASE2/PHASE3/PHASE4 each promised.
   - Create a SECOND Screen Post-Process Pass (different name) into the SAME
     project — confirm its own `/*priority=*/` literal reads `1`, not `0`.
   - `GET /get_logs` — confirm the new `GTE_LOG_INFO("ProjectLifecycle", ...)`
     line appears, and no unexpected `WARNING`/`ERROR` lines appear.
   - `stop_app_background` when done; delete this throwaway smoke-test
     project's folder afterward (this is NOT PHASE8's own permanent fixture —
     PHASE8 creates its own, fresh, deliberately-named permanent one later,
     specifically for the full, final Definition-of-Done proof, including an
     actual compiled-and-running visual confirmation this phase does not
     need to attempt yet, since PHASE7's HTTP route and PHASE8's full
     regression/compile-and-see-it-live proof still lie ahead).
4. Confirm the OLD-project fallback path still works safely: point this same
   feature at `Projects/ProjectAssemblyProbe/` (an old-style project with NO
   anchors) via the Create menu. **Important, confirmed-real gotcha**: that
   project's own Game-half source file is named `Assets/HelloGame.cpp`, NOT
   `Assets/ProjectAssemblyProbeGame.cpp` — it predates the `<Name>Game.cpp`
   naming convention `CreateNewProjectAssembly()` established (it was written
   by hand, early in the `editor-core-separation-11` campaign, before that
   convention existed). This means `TryAutoWireRegisterCall()`'s computed
   target path, `active.assetsDirectory / (active.name + "Game.cpp")` =
   `Projects/ProjectAssemblyProbe/Assets/ProjectAssemblyProbeGame.cpp`, does
   not exist AT ALL — the function will correctly, safely return `false` via
   its own "file cannot be opened" branch (PHASE4's Step 3.2, item 1), NOT
   via the "anchors missing" branch — a different reason, but the exact same
   safe, harmless, zero-file-touched observable outcome. Confirm the reminder
   message reports the fallback wording, and `read_file`
   `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` (NOT the
   never-existed `ProjectAssemblyProbeGame.cpp`) to confirm it is COMPLETELY
   UNMODIFIED (byte-for-byte identical to before this test) — never partially
   patched, and (obviously) never newly created either. Delete the throwaway
   generated `<Name>ScreenPass.cpp` from `ProjectAssemblyProbe/Assets/`
   afterward so this permanent fixture's own `Assets/` folder is left exactly
   as prior campaigns left it (do not leave test debris in a shared,
   permanent fixture).

## Step 6: Completion

Write `PHASE6_COMPLETION_REPORT.md` in this same folder, including every
piece of live evidence gathered in Step 5 (exact file contents read back,
exact log lines, exact HTTP/UI steps taken, and explicitly noting the real
`HelloGame.cpp` vs. `ProjectAssemblyProbeGame.cpp` filename distinction was
confirmed as described above). `git_add` + `git_commit`
covering the code change, the extended test file, and the report. Do not run
a full build/regression here (Locked Decision 2, PHASE0) — PHASE8 is where
that happens.

Given this phase's size and end-to-end nature, strongly consider a single
`delegate_task(position: "next")` self-double-check of the full wiring (the
new branch's own logic, plus a fresh read-through of PHASE1-5's own code
against what this branch actually calls) before writing the completion report
(Locked Decision 14, PHASE0) — instruct that sub-task to use `ask_questions`
for any ambiguity it finds, and to report back inline without creating its
own report file.
