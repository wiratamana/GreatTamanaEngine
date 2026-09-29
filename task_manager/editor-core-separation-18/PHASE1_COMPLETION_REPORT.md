# editor-core-separation-18 — PHASE1 COMPLETION REPORT
## On-Engine Project Workflow — BIG-STEP 4 of 5: "Create New Script/Shader Asset"

Status: **DONE**. Scoped compile check + scoped `ctest` run both green. This
is a single-phase implementation report (not a full campaign closeout — that
is PHASE4's own job).

## What was actually built

Exactly what `PHASE1_ASSET_SCAFFOLDING_CAPABILITY_AND_TEMPLATES.md` asked
for, no UI/HTTP/`ProjectPanel.cpp` touched:

- **`src/Core/EditorCapabilities.h`** — added `enum class AssetScaffoldKind
  { RenderPass, ComputeShader, ShaderPair };` and a new
  `IAssetScaffoldingCapability` interface (with its own `ScaffoldOutcome`
  struct and single `CreateAssetScaffold(kind, name)` pure-virtual method),
  inserted immediately after `IProjectLifecycleCapability`, before the
  closing `} // namespace gte` — a sibling interface, not folded into the
  existing one, per this file's own "one interface per genuinely new
  capability gap" convention.
- **`src/Editor/EditorProjectLifecycleCapability.h`** — `class
  EditorProjectLifecycleCapability` now derives from BOTH
  `IProjectLifecycleCapability` AND `IAssetScaffoldingCapability` (multiple
  inheritance), with a new `ScaffoldOutcome CreateAssetScaffold(...)
  override;` declaration.
- **`src/Editor/EditorProjectLifecycleCapability.cpp`** — added the exact
  corrected templates the phase file specified:
  - `ReplaceAll()` — the shared "replace every literal token" helper.
  - `BuildRenderPassCppContent()` — Kind 1 (`<Name>RenderPass.cpp`, one
    file), using the REAL `gte::rg::ProviderScope::Once` enum value and the
    REAL two-parameter `RenderPassProvider` lambda signature
    (`(const RenderPassFrameContext& frame, std::vector<RenderPassDesc>&
    outPasses)`) — the master-plan document's own Kind-1 sketch (a
    non-existent `ProviderScope::Game` value, a single-parameter lambda)
    would not have compiled; this phase's corrected version is grounded
    directly in `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`'s own
    real, live call site.
  - `BuildComputeShaderGlslContent()` + `BuildComputePassCppContent()` —
    Kind 2 (`<Name>.comp` + `<Name>ComputePass.cpp`, two files).
  - `BuildVertexShaderContent()` + `BuildFragmentShaderContent()` — Kind 3
    (`<Name>.vert` + `<Name>.frag`, two plain shader text files, no
    companion `.cpp`).
  - `BuildScaffoldFileSpecs()` / `ReminderMessageForKind()` / `ToLowerAscii()`
    — the per-kind file-list, reminder-message, and case-insensitive-collision
    helpers, exactly as specified.
  - `CreateAssetScaffold()` itself — no-active-project guard, name
    validation via the existing, reused `IsValidProjectAssemblyIdentifierName()`,
    an all-or-nothing case-insensitive collision scan across every file the
    requested kind would produce (checked BEFORE any file is written), a
    best-effort `create_directories()` on `Assets/` (for the documented
    edge case of a `NotBuildable`-tier project missing that folder), then
    the actual write loop, `GTE_LOG_INFO`/`GTE_LOG_ERROR` calls, and the
    structured `ScaffoldOutcome` return. Deliberately NO
    `#if !GTE_ENABLE_PROJECT_ASSEMBLIES` guard and NO explicit CMake
    reconfigure call, both per the phase file's own explicit reasoning
    (LDD-CA1).
  - New includes added: `<algorithm>` (`std::transform`/`std::find`) and
    `<cctype>` (`std::tolower`).
- **`tests/Editor/AssetScaffoldTemplateTests.cpp`** (new file) — 6 tests,
  all exercised ONLY through the public `CreateAssetScaffold()` entry point
  (never a private helper — see "Deviation" below): no-active-project
  failure; RenderPass/ComputeShader/ShaderPair each producing the exact
  expected file count/names, substituted content (`__NAME__` never
  literally present, real `ProviderScope`/`RenderPassProvider` shapes
  present in the RenderPass case), and the correct reminder-message
  presence/absence; same-name-same-kind called twice rejected with the
  original file's content/mtime provably unchanged; same-name-different-case
  called twice also rejected (case-insensitive collision).
- **`tests/CMakeLists.txt`** — `Editor/AssetScaffoldTemplateTests.cpp` added
  to the `if(TRUE)` always-built bucket, immediately after
  `Network/OpenProjectEndpointEndToEndTests.cpp` (the same bucket
  `Editor/LoggerTests.cpp`/`Network/CreateProjectEndpointEndToEndTests.cpp`/
  `Network/OpenProjectEndpointEndToEndTests.cpp` already live in) — NOT
  `tests/Core/Plugins/`, per the phase file's own explicit correction
  (`EditorProjectLifecycleCapability` is `gte_editor`-tier code).

## Deviations from the phase file — none load-bearing

There were **zero substantive deviations** from
`PHASE1_ASSET_SCAFFOLDING_CAPABILITY_AND_TEMPLATES.md` as written — this
phase file had already been corrected (per the task instructions) before
implementation started, specifically around the STEP 4 unit-test plan (test
only through the public `CreateAssetScaffold()`, never a private
anonymous-namespace helper) and the test file's location/CMake bucket
(`tests/Editor/`, not `tests/Core/Plugins/`). Both corrections were followed
exactly:

- Confirmed, by direct inspection of `EditorProjectLifecycleCapability.cpp`,
  that `BuildScaffoldFileSpecs()`/`ReminderMessageForKind()`/every
  `Build...Content()` template builder/`ToLowerAscii()`/`ReplaceAll()` do
  in fact have internal linkage (anonymous namespace) and are therefore
  unreachable from a separate test `.cpp` — the new test file tests
  everything exclusively through `CreateAssetScaffold()`, matching this same
  file's own pre-existing convention for `BuildGameStubCppContent()`/
  `WriteTextFile()`/`ToTierName()` (zero direct unit tests, only ever
  exercised indirectly).
- One implementation-level choice not spelled out verbatim by the phase
  file's own literal code listing, but consistent with its written intent:
  rather than opening a second, separate anonymous namespace block for the
  3.4 collision/file-spec logic (as the phase file's own code listing shows
  side-by-side, unconnected snippets), this phase kept ALL of 3.3 and 3.4's
  helper functions inside the SAME, single anonymous namespace already
  present in the file (alongside the pre-existing `BuildGameStubCppContent()`/
  `WriteTextFile()`/`ToTierName()`) — functionally identical in C++ (multiple
  anonymous-namespace blocks in one translation unit all merge into the same
  actual namespace), just fewer redundant `namespace { ... }` open/close
  pairs in one file.
- The unit test file additionally records the exact substituted
  provider-registration string (`"Foo.RenderPass"`) and the corrected,
  real `ProviderScope`/`RenderPassProvider` shapes as an explicit regression
  check (`EXPECT_NE(content.find("gte::rg::ProviderScope::Once"), ...)`,
  etc.) — this goes slightly beyond the phase file's own minimum ("contains
  the substituted name... never the literal token `__NAME__`"), added
  because this exact defect (the master-plan document's non-compiling Kind-1
  sketch) is this whole campaign's own headline risk-register item, and a
  plain string-presence check on the corrected API shapes is a cheap,
  permanent regression guard against ever silently reverting to the wrong
  signature.

## Verification performed (scoped, per this phase's own rules)

- **Scoped compile check**: `cmake --build build --target
  GreatTamanaEngineTests --config Debug` — confirmed the real target name
  via `tests/CMakeLists.txt` (`add_executable(GreatTamanaEngineTests ...)`)
  first. Clean incremental build, zero errors, zero new warnings; CMake's
  own `CONFIGURE_DEPENDS` re-glob picked up the new test file automatically
  on the very next build invocation (no manual reconfigure needed).
- **Scoped test run**: `ctest -C Debug --output-on-failure -R
  AssetScaffoldTemplateTest` — **6/6 tests passed**:
  - `NoActiveProjectFailsWithClearMessage`
  - `RenderPassKindCreatesOneFileWithSubstitutedNameAndReminder`
  - `ComputeShaderKindCreatesTwoFilesWithSubstitutedNameAndReminder`
  - `ShaderPairKindCreatesTwoFilesWithNoReminderAndNoCompanionCpp`
  - `SameNameSameKindCalledTwiceRejectsSecondCallAndLeavesOriginalFileUntouched`
  - `SameNameDifferentCaseCalledTwiceRejectsSecondCallCaseInsensitively`
- Did **not** run a full clean build or the full `ctest` regression suite —
  explicitly reserved for PHASE4, per the campaign's own workflow rules.
- No live/visual/HTTP-driven verification was performed — correctly out of
  scope for this phase (no UI/HTTP route exists yet to drive; PHASE1 is pure
  C++ interface/implementation/unit-tests only, as the phase file itself
  states in its own opening paragraph).

## New gaps found during this phase, for PHASE2/PHASE3/PHASE4 to know about

- None. Every finding/correction PHASE0/PHASE1 already documented (the
  `ProviderScope`/`RenderPassProvider` signature mismatch, the case-
  insensitive collision requirement, the "no CMake reconfigure needed" design
  decision, the test-isolation hazard around the shared
  `ActiveProjectAssemblyState` singleton) was confirmed accurate during
  implementation — none were found to be wrong or incomplete.
- One thing worth flagging forward, though not a gap in this phase's own
  work: `CreateAssetScaffold()` is now callable and fully tested in
  isolation, but nothing calls it yet in production (no `ProjectPanel.cpp`
  Create-submenu, no HTTP route, no `CreateAssetWindow`) — PHASE2/PHASE3 are
  each still fully required before this capability is reachable by an actual
  user or AI agent. `EditorHost.cpp` also does not yet construct/wire an
  `IAssetScaffoldingCapability*` anywhere (no `SetAssetScaffoldingCapability`
  setter exists on `IEditorLayer` yet) — that wiring is explicitly PHASE3's
  job per PHASE0's own plan, not overlooked here.

## Files changed this phase

- `src/Core/EditorCapabilities.h` (modified)
- `src/Editor/EditorProjectLifecycleCapability.h` (modified)
- `src/Editor/EditorProjectLifecycleCapability.cpp` (modified)
- `tests/CMakeLists.txt` (modified)
- `tests/Editor/AssetScaffoldTemplateTests.cpp` (new)
- `task_manager/editor-core-separation-18/PHASE1_COMPLETION_REPORT.md` (new,
  this file)

This closes PHASE1 of the `editor-core-separation-18` campaign. Next up:
`PHASE2_PROJECT_PANEL_SYNTHETIC_ROW_AND_CREATE_MENU.md`.
