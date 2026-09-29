# editor-core-separation-18 — CAMPAIGN COMPLETION REPORT
## On-Engine Project Workflow — BIG-STEP 4 of 5: "Create New Script/Shader Asset"

Status: **DONE**. All four phases complete; full clean build + full `ctest`
regression pass performed in PHASE4 with zero regressions.

## What was actually built, phase by phase

**PHASE1 — Asset Scaffolding Capability + Corrected Templates.**
`enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair };` and
a new sibling interface, `IAssetScaffoldingCapability` (`ScaffoldOutcome`
struct + `CreateAssetScaffold(kind, name)`), added to `Core/EditorCapabilities.h`
right after `IProjectLifecycleCapability`. `EditorProjectLifecycleCapability`
gained a second base (multiple inheritance) and its real implementation:
`BuildRenderPassCppContent()` (Kind 1, ONE file) using the master-plan
document's own factually-wrong Kind-1 sketch **corrected** against
`Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`'s own real, live call
site — the real `gte::rg::ProviderScope::Once` enum value (there is no
`ProviderScope::Game`) and the real two-parameter `RenderPassProvider`
lambda signature `(const RenderPassFrameContext&, std::vector<RenderPassDesc>&)`
(never a single-parameter lambda); `BuildComputeShaderGlslContent()` +
`BuildComputePassCppContent()` (Kind 2, two files); `BuildVertexShaderContent()`
+ `BuildFragmentShaderContent()` (Kind 3, two plain shader files, no
companion `.cpp`); an all-or-nothing, explicit-ASCII-lowercase
case-insensitive collision scan run BEFORE any file is written; and a
"no active project" guard reading `ActiveProjectAssemblyState::Instance().GetActive()`.
6 new unit tests in `tests/Editor/AssetScaffoldTemplateTests.cpp`, exercised
exclusively through the public `CreateAssetScaffold()` entry point (every
template-builder/collision helper has internal linkage).

**PHASE2 — `ProjectPanel.cpp` Synthetic Row + Create Submenu.**
`EditorContext.h` gained a forward-declared `enum class AssetScaffoldKind;`
(never `#include`-ing the whole of `Core/EditorCapabilities.h`) plus
`createAssetWindowOpen`/`createAssetWindowPendingKind`. `ProjectPanel.cpp`
gained `RenderActiveProjectAssetsRow()` (the synthetic `"[Active Project]
<Name>"` tree row, its own `Create` submenu, and a plain, display-only
`BulletText` file listing per LDD-CA3), `RescanActiveProjectAssetsIfNeeded()`
(a one-level, non-recursive directory scan on the same 500ms throttle as
the panel's pre-existing scans), and a `m_suppressPaneContextMenuThisFrame`
guard that keeps the new popup and the panel's pre-existing
`BeginPopupContextWindow()`-based context menu mutually exclusive.

**PHASE3 — `IEditorLayer` Wiring + `CreateAssetWindow` + HTTP Route.**
`IEditorLayer::SetAssetScaffoldingCapability()` (new pure virtual, no-op in
`NullEditorLayer`, stored in `ImGuiEditorLayer`); `NetworkServer`'s
constructor/`RegisterRoutes()` gained a tenth parameter
(`IAssetScaffoldingCapability*`, pointing at the SAME
`s_editorProjectLifecycleCapability` static object as the ninth,
`IProjectLifecycleCapability*`, argument — multiple inheritance, one
object, two base-sub-object pointers); `POST /project_assembly/create_asset`
(`kind`/`name` query params, 400 on an unrecognized `kind` or scaffold
failure, 503 when the capability pointer is null, `created_files`/
`reminder_message` JSON body on success); `EditorHost.cpp` wired the tenth
constructor argument and the new setter; new `src/Editor/CreateAssetWindow.h/.cpp`
(mirrors `NewProjectWindow`/`OpenProjectWindow` exactly).

**PHASE4 — Tests, Live Verification, Full Regression, Campaign Closeout
(this phase).** New end-to-end test file,
`tests/Network/CreateAssetEndpointEndToEndTests.cpp` (6 new tests: no
active project → 400 "no active project" [explicit `Clear()`/restore of
the shared `ActiveProjectAssemblyState` singleton, mirroring
`OpenProjectEndpointEndToEndTests.cpp`'s own idiom]; invalid `kind` → 400
naming all 3 valid values; one successful scaffold per kind against a real
scratch temp directory, asserting the exact `created_files` list and every
byte-for-byte substituted file content; same-name-same-kind called twice →
400 "already exists" with the original file's content/mtime provably
unchanged; same-name-different-case called twice → also 400; a
no-capability-pointer 503 test). Full live, HTTP-driven verification
against a real running `GreatTamanaEditor.exe`: created a real project
(`DualPassProof`), scaffolded two independent Render Passes over HTTP
(`First`/`Second`), manually hand-wired both into `RegisterProject()` (the
one, explicitly-sanctioned exception to "no in-engine text editor" this
whole 5-file plan carries), compiled — **exit code 0, zero
duplicate-`GTE_RegisterProject` linker error** — this campaign's own
single most load-bearing proof. Also exercised the collision path live
(re-scaffolding `First` → 400 `"a file named 'FirstRenderPass.cpp' already
exists"`) and confirmed, via a real `/get_swapchain` screenshot, the
`"[Active Project] DualPassProof"` row showing all 3 files
(`DualPassProofGame.cpp`/`FirstRenderPass.cpp`/`SecondRenderPass.cpp`). A
full clean build (**597/597 steps, zero errors, zero new warnings**) and a
full `ctest -C Debug --output-on-failure` regression (**1986/1986 tests
"passed" per ctest's own accounting — i.e., zero failures — 8 legitimate
environment-gated skips, unchanged in kind/count from
`editor-core-separation-17`'s own baseline**, see the arithmetic below)
both passed cleanly.

## Every real, mechanically-confirmed deviation from any phase's own file, and why

- **The `ProviderScope`/`RenderPassProvider` signature correction (PHASE0's
  own finding, carried through PHASE1's real implementation).** The
  external master-plan `.txt` document's own Kind-1 code sketch used a
  non-existent `gte::rg::ProviderScope::Game` enum value (the real enum is
  `enum class ProviderScope { Once, PerActiveView };` — no `Game` member
  exists) and a single-parameter provider lambda
  (`[](const RenderPassFrameContext& frame) { ... }`), when the real,
  live, currently-compiling `RenderPassProvider` type is
  `std::function<void(const RenderPassFrameContext& frame,
  std::vector<RenderPassDesc>& outPasses)>` — a TWO-parameter lambda that
  APPENDS to an out-vector rather than building/returning one desc
  directly. Neither would have compiled as originally sketched. PHASE0
  found and documented this BEFORE any code was written (grounded directly
  in `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`'s own real call
  site); PHASE1 shipped the corrected version; this phase's own live
  verification (STEP 3.3) mechanically re-confirmed it compiles cleanly —
  `Register Foo/First/SecondRenderPass(gte::Core& core)` bodies calling
  `core.RegisterProjectRenderPassProvider(name, gte::rg::ProviderScope::Once,
  [](const gte::rg::RenderPassFrameContext& frame,
  std::vector<gte::rg::RenderPassDesc>& outPasses) { ... })` compiled and
  linked with zero errors across all 3 scaffolded Render Passes exercised
  during this whole campaign (`Foo` in PHASE3, `First`/`Second` in this
  phase).
- **The `BeginPopupContextItem`-vs-`BeginPopupContextWindow` mutual-exclusion
  fix (PHASE0's own finding, carried through PHASE2's real implementation).**
  The master-plan document's own STEP 1 code sketch used
  `ImGui::BeginPopupContextItem(...)` for the new synthetic row's own
  Create-submenu popup, extrapolating from a different, unrelated
  precedent elsewhere in the codebase. The REAL, current
  `ProjectPanel.cpp::RenderContextMenu()` convention is
  `ImGui::BeginPopupContextWindow(popupId)`, called once per pane
  (left/right), each with its own distinct popup ID string — and calling
  BOTH a `BeginPopupContextItem` on the new row AND the pre-existing
  `BeginPopupContextWindow` on the same pane, on the same right-click,
  would be a genuine ImGui double-popup hazard (two popups fighting to
  open from the same input event). PHASE2's concrete, mechanical fix: a
  per-frame `m_suppressPaneContextMenuThisFrame` bool, reset to `false` at
  the very top of every `Build()` call, set `true` only from inside
  `RenderActiveProjectAssetsRow()`'s own popup block, and checked by
  `RenderContextMenu()` as a plain early-`return` BEFORE it ever calls
  `BeginPopupContextWindow()` — so the pre-existing "New Folder"/"Delete
  Selected" menu logic is completely untouched code, suppressed only on a
  frame where the NEW popup is genuinely open. Confirmed correct by direct
  source inspection during PHASE2 (compile-verified, not click-tested — see
  below) and unaffected by any later phase's own changes.
- **PHASE2's own genuinely POSITIVE deviation (more verification than the
  phase file asked for, not less).** The phase file assumed no HTTP route
  could screenshot the new synthetic row at all; PHASE2 found the
  pre-existing `GET /activate_tab?name=<PanelName>` route
  (`network-impl-7` campaign) already solves the "which docked tab is
  focused" problem, and used it to capture two REAL, on-screen screenshots
  (no active project → no synthetic row; a real active project → the row
  with its one bulleted child) rather than substituting a source-code read
  for that half of the verification. The right-click-to-open-the-submenu
  interaction itself (genuinely not HTTP-drivable — no click-simulation
  route exists anywhere in this codebase) remained a source-code-verified
  gap, exactly as expected.
- **PHASE3's one real, undocumented gap it found and fixed itself:** the
  phase file's own STEP 2/3 never mentioned that the root `CMakeLists.txt`'s
  hand-maintained `gte_editor` source file list (NOT a `file(GLOB)`) needed
  the two new `CreateAssetWindow.h/.cpp` files added — without this, the
  very first scoped compile check would have failed with unresolved
  symbols. PHASE3 found and fixed this before its own scoped build ran.
- **This phase's own answer to STEP 3.3's explicit "is `stop_app_background`
  actually necessary here" question: NO, confirmed empirically, live, not
  guessed.** The load-bearing proof (STEP 3.3) was run end to end WITHOUT
  ever stopping the running `GreatTamanaEditor.exe` instance between
  creating `DualPassProof`, scaffolding both Render Passes over HTTP,
  hand-editing `DualPassProofGame.cpp`, and triggering
  `POST /project_assembly/debug/compile_only?name=DualPassProof` — the
  compile completed with exit code 0 and no linker error. This matches
  `docs/conventions/project-assembly-system.md`'s own documented
  "permanent limitation" reasoning exactly: `Core::LoadProjectAssemblies()`'s
  own unconditional STARTUP-ONLY scan means a project that was only ever
  `create_project`'d (never `open_project`'d/loaded) through THIS running
  instance has no `.dll` mapped into this process at all, so Windows never
  locks anything this instance's own recompile would need to overwrite.
  Had `DualPassProof` instead been `ProjectAssemblyProbe` (loaded
  unconditionally at every startup), the same compile step would have
  failed with a linker "permission denied" error unrelated to this
  campaign's own duplicate-symbol hazard — this phase deliberately avoided
  that trap per STEP 2's own explicit warning.
- **No other new structural deviations found.** Every citation/design
  decision `PHASE0`/PHASE1-3's own files already documented (LDD-CA1
  through LDD-CA4, the risk register's 5 items) was confirmed accurate
  during this phase's own implementation and live verification — none
  were found to be wrong or incomplete.

## New gaps found during this campaign, for the NEXT campaign (BIG-STEP 5, "Compile menu") to know about

- **`Core::LoadProjectAssemblies()`'s own unconditional, startup-only scan
  is still true and unchanged after this campaign** — confirmed, again,
  independently, by this phase's own STEP 3.3 live proof (a
  `create_project`'d-but-never-`open`'d project stayed genuinely unloaded
  in the running instance throughout the whole load-bearing test, letting
  its own recompile succeed with zero `.dll`-lock conflict). Per
  `editor-core-separation-17`'s own completion report, BIG-STEP 5's
  "Compile" menu action remains the FIRST production feature that will
  ever cause a real, live `Compiled`-tier project (one compiled WHILE the
  Editor is already running, needing an actual load) to be reachable in
  genuine end-user use — this campaign's own scaffolding feature never
  triggers that transition either (scaffolding a file into an
  ALREADY-active project never changes its own tier from the perspective
  of a fresh `LoadProjectAssemblies()` scan, and LDD-CA1 deliberately skips
  any explicit reconfigure). BIG-STEP 5's implementer should budget
  explicit, live verification time for this exact transition, exactly as
  `-17`'s own report already flagged.
- **No HTTP-drivable way to open/screenshot a floating, on-demand ImGui
  window (`CreateAssetWindow` here) still stands, unchanged** — the SAME
  gap `editor-core-separation-16`/`-17` each already flagged for
  `NewProjectWindow`/`OpenProjectWindow`, now confirmed a THIRD time,
  independently, for `CreateAssetWindow`. This is now a repeated pattern
  across THREE consecutive campaigns' worth of brand-new floating windows.
  If BIG-STEP 5 ("Compile menu") needs no new floating window at all (a
  plain menu-item action, per its own name), this gap may simply not
  recur — but if it does need one, the same substitute-verification
  pattern (a real screenshot proving the Editor's menu bar/panels are
  intact + a direct source-code read of the enabling condition) will be
  needed again, unless a dedicated `GET /..._window/open`-style route
  (mirroring the Frame Debugger's own `GET /frame_debugger/open`
  precedent) is finally built generically.
- **A new, genuinely reachable end-user workflow this campaign's own
  scaffolding feature exposes, worth flagging forward:** a scaffolded
  `.comp`/`.vert`/`.frag` shader file is written as PLAIN TEXT with zero
  compilation step of its own (LDD-CA1 only concerns the COMPANION `.cpp`
  file's CMake glob discovery) — the actual SPIR-V compile of a
  hand-authored shader only happens via `gte_add_project_shaders()`'s own
  glob-and-compile step at the NEXT `cmake --build`, exactly like the
  companion `.cpp`. This was already correctly anticipated by LDD-CA1's
  own reasoning and is not a gap in THIS campaign — but BIG-STEP 5's
  "Compile" menu action is the first production UI surface that will let
  an end user trigger that build without going through
  `/project_assembly/debug/compile_only` directly, so its own
  implementer should be aware a scaffolded-but-unwired shader/compute
  pass sitting in `Assets/` is fully expected input to that action, not
  an edge case.
- **No other new structural gaps found.** Every finding/correction
  `PHASE0` itself already documented remained accurate and unchanged
  throughout all four phases — none of them were found to be wrong or
  incomplete during implementation.

## Verification summary

- Full clean build (`cmake --build build --target clean` then
  `cmake --build build`): **597/597 steps, zero errors, zero new compiler
  warnings** (up from `editor-core-separation-17`'s own 594/594 baseline —
  this campaign's own 3 new compiled `.cpp` files: `CreateAssetWindow.cpp`,
  `AssetScaffoldTemplateTests.cpp`, `CreateAssetEndpointEndToEndTests.cpp`).
- Full `ctest -C Debug --output-on-failure`: **1986/1986 tests accounted
  for, zero failures ("100% tests passed"), 8 legitimate
  environment-gated skips** — unchanged in kind/count from
  `editor-core-separation-17`'s own 8-skip baseline
  (`StlLoaderRealModelSmokeTest`, `PmxLoaderRealModelSmokeTest`,
  `ProjectAssemblyHostTest` x2, `ProjectAssemblyRegistrationLedgerTest` x2,
  `CoreHeadlessConstructionTest`, and
  `OpenProjectEndpointEndToEndTest.OpenProjectOnRealCompiledProbeMarksItActiveWithoutTouchingItsFiles`).
  Exact arithmetic: 1974 (prior baseline, itself already including all 8
  skips) + 6 (PHASE1's `AssetScaffoldTemplateTests.cpp`) + 6 (this phase's
  `CreateAssetEndpointEndToEndTests.cpp`, including the no-capability
  test) = **1986**, zero unexplained delta, zero regressions.
- Scoped `ctest -R "AssetScaffoldTemplateTest|CreateAssetEndpoint"` (STEP
  3.2, run BEFORE the full suite per this phase's own file): **12/12
  passed**, confirming this campaign's own new tests cleanly before
  committing to the slower full run.
- Live, HTTP-driven, end-to-end verification against a real running
  `GreatTamanaEditor.exe`: `POST /project_assembly/create_project?name=DualPassProof`
  → 200; `POST /project_assembly/create_asset?kind=render_pass&name=First`
  and `...&name=Second` → both 200, `created_files`/`reminder_message`
  populated correctly; **the load-bearing proof** — `DualPassProofGame.cpp`'s
  `RegisterProject()` hand-edited to forward-declare and call both
  `RegisterFirstRenderPass(core)`/`RegisterSecondRenderPass(core)`, then
  `POST /project_assembly/debug/compile_only?name=DualPassProof` (WITHOUT
  first stopping the running Editor instance — see the deviations section
  above for why this was empirically confirmed safe), polled via
  `GET /get_logs?since_id=...` until `"Build finished with exit code 0 -
  relaunch GreatTamanaEditor.exe to use the result"` appeared, with **zero
  "multiple definition of `GTE_RegisterProject`" text anywhere in the
  build log** (confirmed by direct inspection of every log line from the
  reconfigure through the final link step); the collision path re-exercised
  live (`POST .../create_asset?kind=render_pass&name=First` a second time
  → 400 `"a file named 'FirstRenderPass.cpp' already exists"`); a real
  `GET /activate_tab?name=Project` + `GET /get_swapchain` screenshot
  confirmed the `"[Active Project] DualPassProof"` row showing all 3 real
  files. Cleaned up afterward: Editor process stopped, `Projects/DualPassProof/`
  removed (confirmed gone via `browse_dir` — only the pre-existing
  `Projects/ProjectAssemblyProbe/` remains).
- `git_status`: only this phase's own real, intended files pending before
  this final commit (`tests/CMakeLists.txt` modified;
  `tests/Network/CreateAssetEndpointEndToEndTests.cpp` new/untracked, plus
  this report) — every earlier phase's own files (PHASE1/2/3's source
  changes and completion reports) were already committed by those phases
  themselves. No scratch project folder or throwaway debug code survives
  anywhere in the working tree.

## Explicit Non-Goals this campaign correctly stayed within (per PHASE0's own risk register / master-plan file's own Non-Goals section)

- Did NOT add any in-engine text/code editor — the load-bearing proof's
  own "manually wire in `RegisterFirstRenderPass(core)`/
  `RegisterSecondRenderPass(core)`" step was performed with plain,
  ordinary file tools (`write_file`), exactly the one, explicitly-sanctioned
  exception this whole 5-file plan carries, never a new in-engine editor
  feature.
- Did NOT make the synthetic "[Active Project] `<Name>`" row's children
  independently selectable/navigable/drag-droppable (LDD-CA3) — confirmed
  unchanged by PHASE2, never revisited by PHASE3/PHASE4.
- Did NOT support deleting/renaming an already-scaffolded file, and did
  NOT attach a context menu to an individual child file row — only the
  synthetic ROOT row carries the Create submenu.
- Did NOT invent a second, lower-cased "GLSL-friendly" debug-name
  convention (LDD-CA2) — every scaffolded provider/debug name stays
  PascalCase, matching `HelloGame.cpp`'s own real convention exactly.
- Did NOT perform an explicit CMake reconfigure anywhere in
  `CreateAssetScaffold()` (LDD-CA1) — confirmed, mechanically, by this
  phase's own live proof: the scaffold-then-compile sequence never called
  any reconfigure step of its own, relying entirely on
  `gte_add_project()`'s pre-existing `CONFIGURE_DEPENDS` glob picking up
  the two new `.cpp` files automatically on the very next
  `cmake --build`/`compile_only` trigger.
- Did NOT implement BIG-STEP 5 ("Compile menu") — the "Project" menu's
  "Compile" item remains the sole remaining disabled, reserved placeholder.

This closes the `editor-core-separation-18` campaign (On-Engine Project
Workflow plan, BIG-STEP 4, "Create New Script/Shader Asset") for good.
