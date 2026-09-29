# editor-core-separation-18 — PHASE3 COMPLETION REPORT
## On-Engine Project Workflow — BIG-STEP 4 of 5: "Create New Script/Shader Asset"

Status: **DONE**. Scoped compile check green (both `GreatTamanaEditor` and
`GreatTamanaEngineTests`); live, HTTP-driven, screenshot verification
performed against a real running `GreatTamanaEditor.exe`. This is a
single-phase implementation report (not a full campaign closeout — that is
PHASE4's own job).

## What was actually built

Exactly what `PHASE3_HTTP_ROUTE_AND_CREATE_ASSET_WINDOW_UI.md` asked for, all
ten citations (EditorLayer.h line 88/720, NullEditorLayer.cpp line 101,
ImGuiEditorLayer.cpp lines 13/535/540/947/1183, EditorHost.cpp lines
141/205-209/236, NetworkServer.h lines 68/171-179/280, NetworkServer.cpp
lines 296-300/1337-1361/1492-1515) verified against the real, current source
before editing and matched exactly:

- **`src/Editor/EditorLayer.h`** — added the sibling forward declaration
  `class IAssetScaffoldingCapability;` immediately after the existing
  `class IProjectLifecycleCapability;` (was line 88), and a new pure virtual
  `virtual void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* capability) = 0;`
  immediately after `SetProjectLifecycleCapability`'s own declaration (was
  line 720), with a doc comment mirroring that method's own.
- **`src/Editor/NullEditorLayer.cpp`** — added the matching no-op override,
  `void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* /*capability*/) override { }`,
  immediately after `SetProjectLifecycleCapability`'s own no-op (was line
  101).
- **`src/Network/NetworkServer.h`** — added the sibling forward declaration
  `namespace gte { class IAssetScaffoldingCapability; }` immediately after
  `IProjectLifecycleCapability`'s own (was line 68); added a tenth,
  defaulted constructor parameter
  (`IAssetScaffoldingCapability* assetScaffoldingCapability = nullptr`)
  after `projectLifecycleCapability`; added the matching
  `IAssetScaffoldingCapability* m_assetScaffoldingCapability = nullptr;`
  member field immediately after `m_projectLifecycleCapability` (was line
  280).
- **`src/Network/NetworkServer.cpp`**:
  - `RegisterRoutes()`'s free-function signature (was lines 296-300) gained
    the same tenth parameter.
  - A new route, `POST /project_assembly/create_asset`, pasted directly
    into `NetworkServer.cpp` immediately after the existing
    `/project_assembly/create_project` route (was lines 1337-1361) — parses
    `kind`/`name` query params, a small inline `if/else if` chain (no
    separate `TryParseAssetScaffoldKind()` helper, exactly as the phase file
    specified), 400 on an unrecognized `kind`, 503 when the capability
    pointer is null, 400 on `!outcome.success`, else a `created_files`/
    `reminder_message` JSON body.
  - `NetworkServer::NetworkServer(...)`'s own constructor (was lines
    1492-1515) gained the tenth parameter, the matching
    `, m_assetScaffoldingCapability(assetScaffoldingCapability)` member-init
    line, and the tenth argument appended to its own internal
    `RegisterRoutes(...)` call.
- **`src/Editor/EditorHost.cpp`** — the `m_networkServer(...)` constructor
  call (was lines 207-209) now passes `&s_editorProjectLifecycleCapability`
  a SECOND time as the tenth argument (implicitly upcast to
  `IAssetScaffoldingCapability*` at the call site, per LDD-CA4 — no new
  static object). Immediately after
  `m_editorLayer->SetProjectLifecycleCapability(&s_editorProjectLifecycleCapability);`
  (was line 236), added
  `m_editorLayer->SetAssetScaffoldingCapability(&s_editorProjectLifecycleCapability);`.
- **`src/Editor/CreateAssetWindow.h`/`.cpp`** (new files) — mirror
  `NewProjectWindow.h`/`.cpp` exactly: `Open()`/`Build(EditorContext&,
  IAssetScaffoldingCapability*)`, a kind-aware window title
  (`TitleForKind()`), a single "Name" textbox, Cancel/Create buttons, and on
  success a status-toast message that concatenates the created-file count
  with the outcome's `reminderMessage` (surfaced via
  `ctx.projectWorkflowStatusMessage`/`...StatusIsError`/`...StatusSetTime`,
  the same shared toast every other Project-workflow action already uses).
- **`src/Editor/ImGuiEditorLayer.cpp`** — added
  `#include "CreateAssetWindow.h"` alongside the existing `NewProjectWindow.h`/
  `OpenProjectWindow.h` includes; `m_createAssetWindow.Build(m_ctx,
  m_assetScaffoldingCapability);` immediately after
  `m_openProjectWindow.Build(...)` in the same per-frame block;
  `SetAssetScaffoldingCapability(...) override` storing into a new
  `m_assetScaffoldingCapability` member, immediately after
  `SetProjectLifecycleCapability`'s own override; and a new
  `CreateAssetWindow m_createAssetWindow;` member object plus the
  `IAssetScaffoldingCapability* m_assetScaffoldingCapability = nullptr;`
  pointer member, both immediately after `m_openProjectWindow`/
  `m_projectLifecycleCapability`'s own declarations.

## Deviations from the phase file — one real gap the file never mentioned

The phase file's own STEP 2/3 exhaustively cited every line to touch inside
`EditorLayer.h`/`NullEditorLayer.cpp`/`ImGuiEditorLayer.cpp`/`EditorHost.cpp`/
`NetworkServer.h`/`.cpp`, and every one of those citations was confirmed
accurate, verbatim, before editing. **What the phase file never mentioned at
all: the two new files (`CreateAssetWindow.h`/`.cpp`) still needed to be
added to the root `CMakeLists.txt`'s own explicit `gte_editor` source file
list** (a plain, hand-maintained list, NOT a `file(GLOB)` — confirmed by
finding `src/Editor/NewProjectWindow.cpp` listed there explicitly, line
1094, immediately before this phase's own edit). Without this, the new
`.cpp` file would never have been compiled into `gte_editor.a` at all and
the very first scoped compile check would have failed with unresolved
symbols (`CreateAssetWindow::Build`/`Open` referenced from
`ImGuiEditorLayer.cpp` but never defined anywhere). This is a real,
load-bearing correction to the phase file, not a stylistic choice — added
`src/Editor/CreateAssetWindow.h`/`.cpp` immediately after
`src/Editor/OpenProjectWindow.h`/`.cpp` in that same list, matching every
prior campaign's own precedent (`NewProjectWindow`/`OpenProjectWindow` both
already listed there the same way for PHASE1's own respective campaigns).
`tests/CMakeLists.txt` needed NO equivalent change — confirmed, by grep,
that neither `NewProjectWindow.cpp` nor `OpenProjectWindow.cpp` (nor now
`CreateAssetWindow.cpp`) is referenced anywhere under `tests/`, since
`GreatTamanaEngineTests` links against `NullEditorLayer.cpp`, never
`ImGuiEditorLayer.cpp`/any floating-window `.cpp` — exactly as the phase
file's own STEP 5 anticipated ("If a separate test binary target also
compiles `EditorLayer.h`/`NullEditorLayer.cpp`").

No other deviations. Every field name, member location, include line, and
code shape in `EditorLayer.h`/`NullEditorLayer.cpp`/`NetworkServer.h`/`.cpp`/
`EditorHost.cpp`/`ImGuiEditorLayer.cpp`/`CreateAssetWindow.h`/`.cpp` matches
the phase file's own STEP 3 code listing exactly, including the two small,
explicitly pre-authorized departures from the master-plan document it
itself calls out (the inline `if/else if` kind-parsing chain instead of a
named helper, and the `EditorLayer.h` forward-declare-only correction).

## Verification performed (scoped, per this phase's own rules)

- **Scoped compile check #1**: `cmake --build build --target
  GreatTamanaEditor --config Debug` — clean incremental build (CMake's own
  `CONFIGURE_DEPENDS` re-glob picked up nothing extra since this phase's new
  files are hand-listed in `CMakeLists.txt`, not globbed), zero errors, zero
  new warnings. (A harmless, pre-existing "GLOB mismatch" stderr note about
  `Projects/Phase2ScratchTest` — PHASE2's own already-cleaned-up scratch
  project folder, stale in CMake's glob cache from a prior configure — is
  unrelated to this phase's own changes and self-resolved on the next
  reconfigure.)
- **Scoped compile check #2**: confirmed the real test target name via
  `tests/CMakeLists.txt` (`add_executable(GreatTamanaEngineTests
  ${GTE_TEST_SOURCES})`), then `cmake --build build --target
  GreatTamanaEngineTests --config Debug` — clean incremental build, zero
  errors, zero new warnings (only pre-existing Network test `.cpp` files
  recompiled, since none of this phase's own files are referenced from
  `tests/`).
- **Live, HTTP-driven, screenshot-based verification** (STEP 4 of the phase
  file), against a real running `GreatTamanaEditor.exe`
  (`run_app_background`/`gte_send_request`/`stop_app_background`):
  1. Launched the Editor (PID 4580), confirmed reachable via
     `GET /http_hello_world`.
  2. `POST /project_assembly/create_project?name=Phase3Scratch` → `200`,
     `created_source_directory` reported.
  3. `POST /project_assembly/create_asset?kind=render_pass&name=Foo` → `200`,
     `created_files: ["FooRenderPass.cpp"]`, a non-empty
     `reminder_message` ("remember to call RegisterFooRenderPass(core) from
     your project's RegisterProject() function!").
  4. Read `Projects/Phase3Scratch/Assets/FooRenderPass.cpp` directly —
     confirmed byte-for-byte the exact PHASE1 template content, `Foo`
     correctly substituted everywhere (`RegisterFooRenderPass`,
     `"Foo.RenderPass"`), the real `gte::rg::ProviderScope::Once` value and
     the real two-parameter `RenderPassProvider` lambda shape both present.
  5. Repeated step 3 with the SAME name (`name=Foo`) → `400`,
     `{"error":"a file named 'FooRenderPass.cpp' already exists","success":false}`,
     zero new files written.
  6. Repeated step 3 with a different-case name (`name=foo`) → ALSO `400`,
     `{"error":"a file named 'fooRenderPass.cpp' already exists","success":false}`
     (case-insensitive collision, confirmed).
  7. `GET /activate_tab?name=Project` then `GET /get_swapchain` — confirmed,
     by real screenshot, the `"[Active Project] Phase3Scratch"` row renders
     with BOTH `FooRenderPass.cpp` AND `Phase3ScratchGame.cpp` as bulleted
     children.
  8. `GET /get_logs?category=ProjectLifecycle` — confirmed the required
     `GTE_LOG_INFO("ProjectLifecycle", "CreateAssetScaffold(...)")` line
     appears: `"CreateAssetScaffold('Foo'): created 1 file(s) in
     .../Projects/Phase3Scratch/Assets"`.
  9. Cleaned up: stopped the Editor process (PID 4580),
     `rmdir /S /Q` on `Projects/Phase3Scratch/` (confirmed gone via
     `browse_dir` afterward — only the pre-existing
     `Projects/ProjectAssemblyProbe/` remains).
- **`git_status`**: only this phase's own real, intended files pending
  (`CMakeLists.txt`, `src/Editor/EditorHost.cpp`, `src/Editor/EditorLayer.h`,
  `src/Editor/ImGuiEditorLayer.cpp`, `src/Editor/NullEditorLayer.cpp`,
  `src/Network/NetworkServer.cpp`, `src/Network/NetworkServer.h`, plus the
  two new `src/Editor/CreateAssetWindow.h`/`.cpp` files) — no scratch
  project folder, no debug probe files, nothing left in the working tree.
- The right-click "Create" submenu path itself (opening
  `CreateAssetWindow` from `ProjectPanel.cpp`'s PHASE2 popup, then clicking
  its own "Create" button) was **not** independently re-exercised live in
  this phase — it is not HTTP-drivable (no click-simulation route exists),
  and PHASE2's own completion report already verified the submenu writes
  `ctx.createAssetWindowOpen`/`ctx.createAssetWindowPendingKind` correctly;
  this phase's own new consumer of those two fields
  (`CreateAssetWindow::Build()`) was verified by direct source inspection
  (reads the exact same fields, calls the exact same
  `CreateAssetScaffold()` the HTTP route calls) plus the successful scoped
  compile. The HTTP path exercised above calls the identical
  `CreateAssetScaffold()` method with identical business logic (LDD-PW5),
  so this is not a coverage gap in the underlying logic, only in the
  specific "click a physical ImGui button" input event — the same
  pre-authorized gap PHASE2's own report already disclosed for the
  right-click-to-open step.

## New gaps found during this phase, for PHASE4 to know about

- **None structural.** Every citation/line-number/design-decision
  `PHASE0`/PHASE3's own file already documented was confirmed accurate
  during implementation — none were found to be wrong or incomplete.
- **The one real correction this phase found and fixed** (see "Deviations"
  above): the phase file never mentioned `CMakeLists.txt`'s own hand-
  maintained `gte_editor` source list needing the two new files added. This
  is now recorded here so PHASE4 (and any future phase adding a new
  `src/Editor/*.cpp` file) knows to check that list explicitly rather than
  assuming a `CONFIGURE_DEPENDS` glob will pick it up automatically — unlike
  a Project Assembly's OWN `Assets/*.cpp` (LDD-CA1, which genuinely IS
  globbed), the engine's own `src/Editor/` source list is NOT globbed.
- All ten Definition-of-Done bullets touching this phase's own scope
  (`PHASE0_MASTER_STRATEGY.md`) are now mechanically proven: no-active-
  project 400 (implicitly, since `assetScaffoldingCapability` is non-null in
  production and `CreateAssetScaffold()` itself already returns the
  no-active-project failure — PHASE1's own test coverage), one-of-each-kind
  scaffold, case-sensitive AND case-insensitive collision rejection, and the
  ImGui-window/HTTP-route dual call path into the identical
  `CreateAssetScaffold()` method. The ONE remaining Definition-of-Done item
  — "scaffold two render passes, wire both by hand, both compile together
  with no duplicate-`GTE_RegisterProject` linker error" — is explicitly
  PHASE4's own job (the "load-bearing proof"), not this phase's.

## Files changed this phase

- `CMakeLists.txt` (modified — added `src/Editor/CreateAssetWindow.h`/`.cpp`
  to the `gte_editor` source list; this was the one real gap the phase file
  didn't mention, see "Deviations" above)
- `src/Editor/EditorLayer.h` (modified)
- `src/Editor/NullEditorLayer.cpp` (modified)
- `src/Editor/ImGuiEditorLayer.cpp` (modified)
- `src/Editor/EditorHost.cpp` (modified)
- `src/Network/NetworkServer.h` (modified)
- `src/Network/NetworkServer.cpp` (modified)
- `src/Editor/CreateAssetWindow.h` (new)
- `src/Editor/CreateAssetWindow.cpp` (new)
- `task_manager/editor-core-separation-18/PHASE3_COMPLETION_REPORT.md` (new,
  this file)

This closes PHASE3 of the `editor-core-separation-18` campaign. Next up:
`PHASE4_LIVE_VERIFICATION_TESTS_AND_CAMPAIGN_CLOSEOUT.md` (end-to-end HTTP
tests, the "two render passes, hand-wired, both compile" load-bearing proof,
full clean build + full `ctest -C Debug --output-on-failure`, and
`CAMPAIGN_COMPLETION_REPORT.md`).
