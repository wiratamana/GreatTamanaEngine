# PHASE0 — MASTER STRATEGY
## Campaign: `editor-core-separation-11` — "Project Assembly" System (per-developer, user-authored C++/shader code that links straight into `GreatTamanaEditor.exe` with zero engine recompilation)

This is the orchestrator document. Every child phase (`PHASE1`..`PHASE8`) must read
this file FIRST before doing any work, and must re-read the previous phase's own
completion report (`PHASEn_COMPLETION_REPORT.md`) for continuity clues. This file
is the single source of truth for scope, locked decisions, and phase order. Do
not deviate from the Locked Design Decisions below without stopping and using
`ask_questions`.

**Source material this whole campaign is derived from** (read once for
background/history, never implement straight from them — they are the original
design/investigation, already superseded in several concrete, mechanically-
confirmed ways by this file and its 8 children):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl\PROJECT_ASSEMBLY_IMPL_00_MASTER_INDEX_2026-09-25.txt`
through `..._08_RENDER_PASS_CAPABILITY_BRIDGE_2026-09-25.txt` (9 files). Those 9
files were themselves derived from two even-older design/investigation docs
referenced inside them. **This campaign's own 9 files (this one + PHASE1..8)
REPLACE all of that — an implementer of this campaign only ever needs to read
this folder, never the `-Ideas` folder.**

---

## Step 1: The Goal (Where are we going?)

Give this engine a **"Project Assembly" system**: a per-developer, single-project,
`.gitignore`d folder (`Projects/<Name>/`) holding real, user-authored C++ and
GLSL shader source that compiles into two ordinary Windows `.dll`s
(`<Name>_Game.dll`, `<Name>_Editor.dll`) which `GreatTamanaEditor.exe` loads at
its own startup and which then call **real, live, non-ABI-wrapped engine types**
(`gte::Core&`, real ImGui, real `rg::RenderGraphBuilder`) directly — with **zero
recompilation of the engine itself** for a content change, and **zero new
per-feature ABI surface** to design/maintain (unlike the existing
`plugins/gte_plugin_abi` system, which this campaign never touches).

Two concrete, working, end-to-end proofs are the actual Definition of Done for
the whole campaign, both built as one throwaway test project,
`Projects/ProjectAssemblyProbe/`:

1. A real, dockable, genuinely interactive Editor panel ("Probe Panel", a click
   counter) contributed entirely from `ProjectAssemblyProbe_Editor.dll`, calling
   real `ImGui::*` functions directly — no ABI wrapper.
2. A real render-graph compute pass ("ProjectAssemblyProbe.FillTexture")
   contributed entirely from `ProjectAssemblyProbe_Game.dll`, using genuine
   `rg::RenderGraphBuilder`/`rg::RenderPipeline` types directly, with its own
   freshly-compiled `.spv` shader — visible in the Editor's real "Render Graph"
   panel and `GET /render_graph`.

Everything else in this campaign (toolchain verification, the link-against-exe
mechanism, CMake auto-discovery, the runtime loader, the "Compile" build
trigger) exists purely to make those two proofs possible, safely, and to leave
behind a system a real future project can actually be built on top of.

## Step 2: The Situation (Where are we now?)

### 2.1 — Why this is `editor-core-separation-11`, not `-10`

The original design/investigation docs (dated 2026-09-25, in the `-Ideas`
folder) were written assuming this work would BE `editor-core-separation-10`.
It was not — `task_manager/editor-core-separation-10/` already shipped a
small, unrelated campaign ("ImGui Widget ID Uniqueness", also dated
2026-09-25) before this campaign started. **Every new file this campaign adds
must cite `editor-core-separation-11` in its header comment, never `-10`** —
this is a mechanical, easy-to-miss correction; PHASE1-8 each restate it.

### 2.2 — The single biggest fact this campaign's own investigation changed
versus the original design docs (read this before anything else)

**The original docs' own PHASE01 ("toolchain switch") describes, at length, a
blocking prerequisite that turns out to be ALREADY DONE.** Confirmed directly,
mechanically, against the real, current repository state, right now:

- `build/CMakeCache.txt` → `CMAKE_CXX_COMPILER:FILEPATH=C:/Users/F5954/scoop/apps/mingw/current/bin/c++.exe`
  (the shared-CRT-capable GCC 16.2.0 `mingw-builds-binaries` toolchain — NOT
  the `scoop/apps/gcc/current` GCC that every campaign through
  `editor-core-separation-9`/`editor-enchancements-1` confirmed was still
  active, `--disable-shared`, at the time those campaigns ran).
- `build/CMakeCache.txt` → `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED:INTERNAL=TRUE`.
- `build/plugins/gte_plugin_abi/generated/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h`
  → `fp.sharedRuntimeLinkage = 1;` (previously, per every prior campaign's own
  completion report, this always read `0`).
- `build/GreatTamanaEditor.exe`, `libstdc++-6.dll`, `libgcc_s_seh-1.dll`,
  `libwinpthread-1.dll` all sit together in `build/`, all last written
  2026-09-25 20:24 — a real, complete, already-built shared-CRT binary, not a
  half-finished experiment.

**Nobody documented, in any `task_manager/` campaign folder, a deliberate
decision to perform this switch** (searched: no hit for "shared-CRT toolchain
switch" anywhere under `task_manager/`). It happened, quietly, sometime between
`editor-core-separation-9` and today (2026-09-28) — most plausibly as
uncommitted, ad-hoc local machine setup, not a tracked campaign. **This
campaign does not need to investigate WHO did it or WHY it was never written
up — it only needs to (a) mechanically re-confirm it is still true right now,
at the start of PHASE1, (b) never redundantly "re-switch" it (no new
`build-shared-crt` parallel tree — the existing default `build/` tree already
IS the shared-CRT tree), and (c) document this fact permanently in this
campaign's own `AGENTS.md` entry (PHASE8) so it is never silently lost/assumed-
reverted by a future reader who only skims the older campaigns' own honest
"no actual switch happened" caveats.**

**Consequence for every phase below: this whole system is SAFE for real
`std::string`/`std::vector` cross-boundary `.exe`/`.dll` traffic from PHASE1
onward.** The original docs' own repeated "this phase compiles but remains
unsafe until the toolchain switch happens" caveat is **DELETED** from this
campaign — do not repeat it. (PHASE1 still exists, narrower in scope now: a
mechanical re-confirmation, not a from-scratch switch — see PHASE1's own file.)

### 2.3 — Four more real, mechanically-confirmed gaps versus the original
design (three already known, one new — read all four, they are load-bearing)

**Finding A — `imgui`/`imguizmo` are linked `PRIVATE` into `gte_editor`**
(confirmed, root `CMakeLists.txt` line 1105:
`target_link_libraries(gte_editor PRIVATE imgui imguizmo)`). A Project
Assembly `_Editor.dll` needs ImGui's real headers (to call `ImGui::*`
directly) WITHOUT re-linking the compiled `imgui`/`imguizmo` targets a second
time (which would create a second, independent `GImGui` context pointer in the
process — the exact "two copies of one global" hazard this whole system exists
to avoid for `gte_core`/`gte_editor` itself). Resolved in PHASE7 via a
headers-only `target_include_directories(... $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>>)`
call, proven via a real, live, interactive ImGui widget (not just a pointer
-equality micro-probe).

**Finding B — `Core::RegisterOffscreenRenderPipelineProviders()` is `private`**
(confirmed, `src/Core/Core.h` line 420, inside the `private:` section that
starts at line 379) **and `Core` exposes no public method to register a new
render-graph provider from outside `Core` at all.** This is new code PHASE8
must add — a thin public pass-through, `Core::RegisterProjectRenderPassProvider()`,
mirroring `Core::LoadPlugins()`'s own existing "private member, public
one-line forwarder" shape exactly (confirmed precedent: `Core.h` line 295,
`Core.cpp` line 293, forwarding to the `private` `m_pluginHost` member at line
561).

**Finding C — `GreatTamanaEditor`'s own link to `gte_editor` is `PRIVATE`**
(confirmed, root `CMakeLists.txt` line 1313:
`target_link_libraries(GreatTamanaEditor PRIVATE gte_editor)`). Per this
campaign's own non-negotiable rule (§3.0 LDD2 below), a Project Assembly links
`GreatTamanaEditor` ONLY — but CMake usage-requirement propagation does not
flow past a `PRIVATE` link, so linking `GreatTamanaEditor` alone propagates
**zero** header search paths (not `gte_core`'s, not `gte_editor`'s, not any
third-party dependency's). The first `#include <volk.h>` (or any other
angle-bracket third-party include reachable through `Core.h`) fails outright
with "file not found." Resolved in PHASE3 via explicit
`$<TARGET_PROPERTY:<dep>,INTERFACE_INCLUDE_DIRECTORIES>` generator-expression
`target_include_directories()` calls against every dependency `gte_core`/
`gte_editor` themselves declare `PUBLIC` — headers only, never re-linking the
dependency's own compiled code.

**Finding D (NEW — not present in the original design docs at all) —
`gte_apply_plugin_shared_crt_linkage()`/`gte_apply_plugin_dll_shared_crt_linkage()`
(`cmake/MingwRuntime.cmake`) are gated by `GTE_ENABLE_PLUGINS`** (confirmed,
lines 136 and 176 of that file: `if(NOT GTE_ENABLE_PLUGINS) return() endif()`)
— **the CMake option for the OTHER, unrelated `gte_plugin_abi` system.** The
original design docs' own PHASE03 sketch calls
`gte_apply_plugin_dll_shared_crt_linkage(${NAME}_Game)` verbatim, unmodified,
for a Project Assembly target. **This is a real, silent correctness bug
waiting to happen**: a developer who sets `GTE_ENABLE_PLUGINS=OFF` (e.g. to
disable the old plugin system while still wanting Project Assemblies) would
silently get an UNLINKED/statically-mismatched CRT for their `_Game.dll`/
`_Editor.dll` — with a warning message that says
`"gte_apply_plugin_dll_shared_crt_linkage(...)"`, never mentioning "Project
Assembly" at all, actively misleading whoever reads the log. **Fix, resolved
concretely in PHASE3**: a genuinely new, separate function,
`gte_apply_project_assembly_shared_crt_linkage(target_name)`, added to
`cmake/MingwRuntime.cmake`, gated ONLY by the new `GTE_ENABLE_PROJECT_ASSEMBLIES`
option (§3.0 LDD10) and `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED` — never by
`GTE_ENABLE_PLUGINS`.

### 2.4 — Three more concrete resolutions this campaign's own investigation
found, that the original design docs left as open, unresolved questions for
whoever implemented them (all three now answered, mechanically, in advance)

**Finding E — PHASE08's own "how does a deferred `RenderPassDesc` create a
brand-new transient texture?" question, answered.** Confirmed, directly, by
reading `src/Renderer/RenderGraph/RenderGraphBuilder.h`:
`RenderGraphBuilder::PassBuilder` (the type `RenderPassDesc::setup` actually
receives, line 205) has **no** `CreateTexture()`/`ImportTexture()` method at
all — only `ReadTexture()`/`WriteColorAttachment()`/`WriteTexture()`/
`ReadBuffer()`/`WriteBuffer()`/`ReadVolumeTexture()`/`WriteVolumeTexture()`,
every one of which operates on an ALREADY-MINTED handle.
`CreateTexture()`/`CreateBuffer()`/`ImportTexture()` (lines 303-326) are
methods of `RenderGraphBuilder` itself, not `PassBuilder`. The concrete,
correct pattern (verified against the real, working `"GpuSkinning"` provider
in `Core.cpp` lines 411-455, which does the buffer equivalent of this exact
shape): **call `frame.builder.CreateTexture(...)` directly inside the
provider's own lambda body** (available because `RenderPassFrameContext::builder`,
`RenderPipeline.h` line 364, is a plain reference member — a non-`mutable`
reference member stays freely callable through a `const RenderPassFrameContext&`
regardless of the wrapping object's own constness, unlike a value member),
**obtain the resulting `TextureHandle` BEFORE constructing the
`RenderPassDesc`**, then reference that already-minted handle from inside
`desc.setup` via `pass.WriteTexture(handle)`. PHASE8 gives the full, concrete
code for this — no ambiguity left for the implementer.

**Finding F — a Project Assembly's own compiled shader path convention,
answered.** Confirmed: every existing internal `ComputePipeline`/`Pipeline`
construction call site (`AtmosphereLutRenderer.cpp`,
`PrimitiveGpuCatalog.cpp`, ...) passes a plain, bare relative path string, e.g.
`"shaders/AtmosphereTransmittanceLut.comp.spv"`, with **zero**
`gte::ExecutableDirectory()` prefix anywhere — this only works because the
process's own current working directory is assumed to already equal the
executable's own directory (true for every normal launch of
`GreatTamanaEditor.exe` from its own build output folder). A Project
Assembly's own shader, staged (PHASE4) at
`<exe dir>/project_assemblies/shaders/<name>.spv`, is therefore referenced,
consistently with this exact existing convention, as the literal string
`"project_assemblies/shaders/<name>.spv"` — never an absolute path, never
`gte::ExecutableDirectory()`-prefixed (that would be inconsistent with every
other shader load in this codebase and is not needed).

**Finding G (NEW — not present in the original design docs at all, found
during this campaign's own second-iteration double-check pass) — a Project
Assembly Editor panel registered through `EditorPanelRegistry::
RegisterPluginPanel()` (PHASE7) would silently never draw anything whenever a
developer has `GTE_ENABLE_PLUGINS=OFF`.** Confirmed, directly, by reading
`src/Editor/ImGuiEditorLayer.cpp`: the per-frame loop that actually calls
`entry.module->BuildPanel(drawContext)` for every entry in
`EditorPanelRegistry::Instance().PluginPanels()` sits inside an
`#if GTE_ENABLE_PLUGINS` block — the flag for the OTHER, unrelated
`gte_plugin_abi` system, exactly the same class of hazard as Finding D. This
is a real, silent, confirmed correctness gap, not merely a theoretical one:
`src/Editor/DockLayout.cpp`'s own, separate loop over the exact same
`PluginPanels()` list (the one that assigns a default dock slot to each
panel name) has NO such gate at all, so a Project Assembly panel would still
get a visible, empty, permanently-blank dock tab — actively confusing, since
it looks like a genuine bug rather than a configuration choice. **Fix,
resolved concretely in PHASE7**: widen `ImGuiEditorLayer.cpp`'s existing gate
to `#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES` — safe, because
the loop body only ever iterates whatever is ACTUALLY present in the
registry at runtime, regardless of which system (the old plugin ABI, or this
campaign's Project Assembly system) populated it.

### 2.5 — Relevant, confirmed, current engineering facts every phase below
assumes as ground truth (re-verify anchors via `search_in_dir` before editing
— these are correct as of 2026-09-28, but this repo changes fast)

- `GreatTamanaEditor` target: `add_executable(GreatTamanaEditor src/main.cpp)`
  at root `CMakeLists.txt` line 1298, immediately followed by
  `target_link_libraries(GreatTamanaEditor PRIVATE gte_editor)` at line 1313 —
  both inside an `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard that opens at
  line 1296 (re-locate its matching `endif()` before adding anything after it).
- `GTE_PLUGIN_RUNTIME_OUTPUT_DIR` is set at root `CMakeLists.txt` lines
  802-805 (BEFORE `GreatTamanaEditor` is defined — this is fine; the value
  uses the deferred generator expression `$<TARGET_FILE_DIR:GreatTamanaEditor>`,
  which CMake resolves at generate time, after the whole configure pass, not
  at the point this line is parsed).
- This repo's own convention for including a `cmake/*.cmake` helper is the
  BARE MODULE NAME via `CMAKE_MODULE_PATH` (`list(APPEND CMAKE_MODULE_PATH
  "${CMAKE_SOURCE_DIR}/cmake")` at line 118), e.g. `include(CompileShaders)`,
  `include(MingwRuntime)` (lines 127, 135) — **`include(GteProject)`, never
  `include(cmake/GteProject.cmake)`** (the original design docs' own PHASE03
  sketch used the wrong form; corrected in this campaign's PHASE3).
- Existing CMake options follow `option(GTE_ENABLE_<NAME> "<description>" <default>)`,
  declared near the top of root `CMakeLists.txt` (e.g. line 31
  `GTE_ENABLE_PROJECT_PANEL`, line 116 `GTE_ENABLE_PLUGINS`) — the new
  `GTE_ENABLE_PROJECT_ASSEMBLIES` option (PHASE3) must match this exact style
  and be placed in that same group.
- `tools/ci/` today holds exactly 4 permanent, standalone regression probes:
  `gte_core_player_link_probe/`, `gte_core_standalone_probe/`,
  `gte_plugin_abi_handshake_probe/`, `gte_plugin_isolation_probe/` — PHASE2
  adds a 5th, `gte_project_assembly_link_probe/`, matching their exact
  house style (own `CMakeLists.txt`, own `.gitignore` build-tree entry with a
  documented comment, never deleted after use).
- `PluginHost` (`src/Core/Plugins/PluginHost.h/.cpp`, confirmed, current,
  ~85 lines) is the exact class shape PHASE5's new, sibling
  `ProjectAssemblyHost` mirrors — same `void*`-stored `HMODULE` convention,
  same "safe on a non-existent directory" guarantee, same "never unloaded
  before process exit" lifetime rule.
- `EditorPanelRegistry` (`src/Core/EditorPanelRegistry.h`, confirmed current)
  is a plain Meyers singleton (`static EditorPanelRegistry& Instance()`) with
  a public `RegisterPluginPanel(const std::string&, IEditorPanelModule_v1*)` —
  directly callable from a Project Assembly `_Editor.dll` with no new hook
  needed (PHASE7 confirms this concretely, live, rather than assuming it).
- `JobSystem::RegisterBackgroundThread(std::thread, std::shared_ptr<std::atomic<bool>>)`
  (confirmed, current `src/Jobs/JobSystem.h` line 164, `.cpp` line 56) is the
  CORRECT mechanism for the "Compile" build trigger (PHASE6) — **never**
  `JobSystem::Schedule()`, which would tie up one of the fixed worker-pool
  threads for the build's entire, potentially multi-minute duration. A real,
  working precedent for this exact "dedicated background thread, not a pool
  job" pattern already exists: `JobContinuation.cpp` line 168.
- `GTE_LOG_INFO(category, message)` (confirmed, current `src/Core/Logging.h`
  line 162) takes exactly two arguments — the engine's ONLY sanctioned
  logging mechanism (`AGENTS.md`, "Logging"); every phase below uses this,
  never `printf`/`std::cerr`/`assert`, and every phase's own live verification
  step reads results back via `GET /get_logs` (`gte_send_request`), never a
  console window.
- `RenderPipeline`/`RenderPassProvider`/`ProviderScope`/`RenderPassFrameContext`
  live in `src/Renderer/RenderGraph/RenderPipeline.h` (confirmed — **not**
  under `src/Core/`, a path Correction versus a stray reference in the
  original docs). `ProviderScope` is `{ Once, PerActiveView }` (line 411).
  `RenderPassProvider` is `std::function<void(const RenderPassFrameContext&,
  std::vector<RenderPassDesc>&)>` (line 405).

## Step 3: The Plan (super-detailed strategy)

### 3.0 — Locked Design Decisions (confirmed; decided using best engineering
judgment for an AI-in-the-loop, minimal-human-intervention workflow — do not
silently re-litigate these in a later phase; if a later phase finds a genuine
reason one of these is wrong, stop and use `ask_questions` before deviating)

- **LDD1 — This is a NEW, ADDITIVE, PARALLEL system.** It NEVER modifies
  `plugins/gte_plugin_abi/`, `src/Core/Plugins/PluginHost.h/.cpp`, or any
  existing `IRenderFeatureModule_*`/`IEditorPanelModule_v1` ABI type. If any
  phase file below seems to require touching those files, stop and re-read —
  it always means a NEW, separate, sibling file/class, never an edit there.
- **LDD2 — A Project Assembly links `GreatTamanaEditor` (the executable's own
  import library) ONLY, never `gte_core`/`gte_editor` directly.** The single
  most important, non-negotiable rule in the whole system — this is what
  keeps exactly one physical copy of every `gte_core`/`gte_editor`/`imgui`
  global in the process. Every CMake snippet in every phase below already
  reflects this.
- **LDD3 — `Projects/` is `.gitignore`d, single-developer, same-toolchain,
  same-build-run only.** Never design anything to survive being zipped up and
  handed to a different machine/toolchain version.
- **LDD4 — No hot reload, anywhere, in any phase.** A Project Assembly `.dll`
  is scanned/loaded exactly once, at `GreatTamanaEditor.exe` startup. A
  changed/recompiled `.dll` requires a full close+relaunch. No file-watcher,
  no reload button, no `OnBeforeUnload` hook, ever.
- **LDD5 — Every new file carries a header comment stating WHY it exists and
  WHICH phase added it**, citing `editor-core-separation-11` (never `-10`),
  matching this repo's own extremely consistent house style (see
  `cmake/MingwRuntime.cmake`'s own top-of-file comment for the density/shape
  expected).
- **LDD6 — Never trust a cited line number as still-current.** Every anchor
  cited anywhere in this campaign's own 9 files was real and correct on
  2026-09-28 — always re-locate the real anchor via `search_in_dir` for the
  exact quoted text before editing, never blindly count to a line number.
- **LDD7 — The shared-CRT toolchain switch is ALREADY DONE (§2.2).** No phase
  creates a new parallel `build-shared-crt` tree. PHASE1 re-confirms this
  mechanically and moves on — it does not redo the switch.
- **LDD8 — Project Assembly CRT-linkage is its OWN, independent CMake
  mechanism (Finding D, §2.3), never reusing `gte_apply_plugin_*_shared_crt_linkage()`
  as-is.** A new `gte_apply_project_assembly_shared_crt_linkage()` function is
  added in PHASE3, gated by `GTE_ENABLE_PROJECT_ASSEMBLIES` +
  `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED` only.
- **LDD9 — A new CMake option, `GTE_ENABLE_PROJECT_ASSEMBLIES` (default
  `ON`), gates the ENTIRE system** — both the CMake auto-discovery
  `add_subdirectory()` loop (PHASE3) and the runtime `LoadProjectAssemblies()`
  call site (PHASE5) — mirroring this repo's own "everything optional is a
  named CMake option" convention (`GTE_ENABLE_PLUGINS`, `GTE_ENABLE_PROJECT_PANEL`,
  ...). Decided here, in PHASE3 (moved earlier than the original design docs'
  own PHASE05 placement, since PHASE3 already needs to gate its own
  auto-discovery block on SOMETHING, and inventing it there first, then
  reusing it in PHASE5, is simpler than inventing it in PHASE5 and
  retrofitting PHASE3 afterward).
- **LDD10 — CMake include convention is the bare module name,
  `include(GteProject)`, via `CMAKE_MODULE_PATH`** — never
  `include(cmake/GteProject.cmake)` (§2.5).
- **LDD11 — No full build / full `ctest` regression except in the final
  phase (PHASE8's own closeout).** PHASE1-7 use incremental/quick compile
  checks only (mirroring every prior campaign's own operating rule) — this
  machine's full build and full test suite are slow and must not be run
  repeatedly. Use `run_app_background`/`gte_send_request`/`stop_app_background`
  for live, HTTP-driven visual/log verification instead wherever a phase's
  own Definition of Done calls for it.
- **LDD12 — Two capabilities (Editor panel, render pass) are proven with ONE
  shared, permanent, throwaway test project, `Projects/ProjectAssemblyProbe/`**
  (never deleted after the campaign — it becomes this system's own permanent
  smoke-test fixture, mirroring how `plugins/demo_hello_world/` etc. already
  serve that exact role for the OTHER system).

### 3.1 — Non-Goals (explicit, so nobody "helpfully" expands scope — restated
from the original design docs' own Section 12/13, kept binding here)

- No gameplay/"MonoBehaviour"-style scripting bridge (per-entity
  `Update()`/`Start()`, ECS component authoring, Input access).
- No hot reload (LDD4).
- No cross-machine/cross-checkout portability (LDD3).
- No separate Player executable — both `_Game.dll`/`_Editor.dll` load into
  the one existing `GreatTamanaEditor.exe` process.
- No change of any kind to `plugins/gte_plugin_abi`, `PluginHost`, or any
  existing ABI-versioned interface (LDD1).
- No scaffolding/"New Project" wizard tool — PHASE3 defines the exact
  folder/file SHAPE a future scaffolding tool must produce; a human creates
  `Projects/<Name>/{Assets,Libraries}` by hand for now.
- No UI-design decision about WHERE the "Compile" button/menu item
  permanently lives — PHASE6 implements the build-trigger mechanism only.
- No resolution of every possible future render-graph capability (reading
  `SceneDepth`, multiple render targets, cross-Project-Assembly-pass
  chaining, ...) — PHASE8 proves ONE minimal, concrete, working example.

### 3.2 — Phase breakdown (authoritative; mirrors the original design docs'
own 8-phase split, corrected per §2.2-2.4 above)

1. **PHASE1 — Toolchain Verification (was "Toolchain Switch").** Mechanically
   re-confirm §2.2's facts are still true right now (not assumed from this
   document alone), and permanently document them (a completion report plus
   the `AGENTS.md` entry drafted here, finalized in PHASE8). Much lighter
   than the original design's own from-scratch switch — the switch itself is
   already done.
2. **PHASE2 — `ENABLE_EXPORTS` + Link-Against-`.exe` Feasibility Probe.**
   A small, permanent, standalone probe (`tools/ci/gte_project_assembly_link_probe/`)
   proving the "a `.dll` links against the already-running `.exe`" mechanism
   genuinely works on this repo's real toolchain, including the ImGui-global-
   state risk (Finding A).
3. **PHASE3 — Folder Layout + CMake Auto-Discovery + `gte_add_project()`.**
   The `Projects/` folder, `.gitignore` entry, the new `GTE_ENABLE_PROJECT_ASSEMBLIES`
   option, root `CMakeLists.txt`'s `ENABLE_EXPORTS`/`WINDOWS_EXPORT_ALL_SYMBOLS`
   flip on `GreatTamanaEditor` itself, the new `cmake/GteProject.cmake`
   (`gte_add_project()`, the header-propagation fix for Finding C, the new
   independent CRT-linkage function for Finding D), and an empty, buildable
   `ProjectAssemblyProbe_Game`/`_Editor.dll` pair.
4. **PHASE4 — Shader Compilation Wiring.** `gte_add_project_shaders()`, a
   thin wrapper reusing the engine's own, already-generic `gte_add_shader()`
   completely unmodified.
5. **PHASE5 — `ProjectAssemblyHost` Runtime Loader.** The class that scans
   the output folder, `LoadLibraryW()`s each `.dll`, and calls the one fixed
   `GTE_RegisterProject` export with a real, live `gte::Core&`
   (and, for `_Editor.dll`, a real `gte::EditorHost&`).
6. **PHASE6 — The "Compile" Trigger (Build Automation).** A callable
   `TriggerProjectAssemblyCompile()` running `cmake --build` as a real,
   non-blocking child process on a dedicated `JobSystem` background thread,
   streaming output into `GTE_LOG_INFO`/`GTE_LOG_WARNING`.
7. **PHASE7 — Capability #1: Custom Editor Panel.** Resolves Finding A
   concretely: `ProjectAssemblyProbe_Editor.dll` implements
   `IEditorPanelModule_v1` directly, ignoring the ABI's `ctx` parameter,
   calling real `ImGui::*` — proven with a genuinely interactive click
   counter, not a static text render. Also resolves Finding G: a small,
   targeted widening of `src/Editor/ImGuiEditorLayer.cpp`'s existing
   `#if GTE_ENABLE_PLUGINS` gate around the per-frame `PluginPanels()`
   `BuildPanel()` call loop, so the panel actually draws even when
   `GTE_ENABLE_PLUGINS=OFF`.
8. **PHASE8 — Capability #2: Custom Render Pass + Campaign Closeout.**
   Resolves Finding B and E concretely: the new
   `Core::RegisterProjectRenderPassProvider()` public method, a real compute
   pass filling a transient texture, visible in the Render Graph panel/HTTP
   endpoint — plus this campaign's OWN full regression pass (only phase
   allowed to run one, per LDD11), the permanent `AGENTS.md` entry, and
   `CAMPAIGN_COMPLETION_REPORT.md`.

### 3.3 — Dependency graph (do not reorder)

```
PHASE1 -> PHASE2 -> PHASE3 -> PHASE4
                       |
                       +-----> PHASE5 -> PHASE6
                                  |
                                  +-----> PHASE7
                                  |
                                  +-----> PHASE8
```

PHASE3 must exist before PHASE4/PHASE5 (both need the `_Game`/`_Editor` CMake
targets PHASE3 creates). PHASE5 must exist before PHASE6/7/8 (all three need a
working loader before they mean anything at runtime). PHASE7 and PHASE8 do not
depend on each other and may be implemented in either order once PHASE5 is
done — this campaign's own final-iteration delegation (see this folder's own
delegation plan, tracked outside these `.md` files) still runs them in
numeric order for simplicity, never in true parallel, to keep one linear,
easy-to-audit git history.

### 3.4 — File manifest (every file any phase creates or edits — the
authoritative list; each phase file repeats only its own rows)

**New files:**
- `tools/ci/gte_project_assembly_link_probe/{CMakeLists.txt,main.cpp,probe_module.cpp}` (PHASE2)
- `cmake/GteProject.cmake` (PHASE3, extended by PHASE4/PHASE7)
- `cmake/templates/ProjectAssemblyExports.h` (PHASE3)
- `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` (PHASE3, replaced by PHASE8)
- `Projects/ProjectAssemblyProbe/Assets/Editor/HelloEditorPanel.cpp` (PHASE3, replaced by PHASE7)
- `Projects/ProjectAssemblyProbe/Assets/ProbeCompute.comp` (PHASE4)
- `Projects/ProjectAssemblyProbe/Libraries/CMakeLists.txt` (PHASE3)
- `Projects/ProjectAssemblyProbe/Libraries/ProjectAssemblyExports.h` (PHASE3, copy of the canonical template)
- `src/Core/Plugins/ProjectAssemblyHost.h/.cpp` (PHASE5)
- `src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp` (PHASE6)
- `docs/conventions/project-assembly-system.md` (PHASE8)

**Edited files:**
- `.gitignore` — `/Projects/` entry (PHASE3), `tools/ci/gte_project_assembly_link_probe/`'s
  own build-tree entry (PHASE2)
- `CMakeLists.txt` (root) — new option (PHASE3), `ENABLE_EXPORTS`/
  `WINDOWS_EXPORT_ALL_SYMBOLS` on `GreatTamanaEditor` (PHASE3),
  `include(GteProject)` + auto-discovery block + `GTE_PROJECT_ASSEMBLY_OUTPUT_DIR`
  (PHASE3)
- `cmake/MingwRuntime.cmake` — new `gte_apply_project_assembly_shared_crt_linkage()`
  function (PHASE3)
- `src/Core/Core.h/.cpp` — forward-declare `EditorHost`, new
  `m_projectAssemblyHost` member + `LoadProjectAssemblies()` pass-through
  (PHASE5); new public `RegisterProjectRenderPassProvider()` (PHASE8)
- `src/Editor/EditorHost.cpp` — new `m_core.LoadProjectAssemblies(...)` call
  site (PHASE5)
- `src/Editor/ImGuiEditorLayer.cpp` — Finding G fix: widen the existing
  `#if GTE_ENABLE_PLUGINS` gate around the per-frame
  `EditorPanelRegistry::Instance().PluginPanels()` `BuildPanel()` call loop to
  `#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES` (PHASE7) — without
  this, a Project Assembly Editor panel gets a dock slot (`DockLayout.cpp`'s
  own loop over the same registry is NOT gated by either flag) but never
  actually draws anything whenever a developer has `GTE_ENABLE_PLUGINS=OFF`.
- `AGENTS.md` — one new section (PHASE8), including the permanent §2.2
  toolchain-switch-already-done disclosure

### 3.5 — How each phase should verify its own work (code-only; no full
build/test until PHASE8, per LDD11)

- Incremental compile check: `cmake --build build --target <specific-target>`
  (the existing default `build/` tree — never delete/recreate it; it is
  already the correct, shared-CRT-capable tree, §2.2).
- `search_in_dir` sweeps to confirm anchors before editing (LDD6) and to
  confirm no leftover stray references after a change.
- Live, HTTP-driven visual/log proof via `gte_send_request` wherever a
  phase's own Definition of Done is behaviorally observable: `run_app_background`
  to launch `build/GreatTamanaEditor.exe`, `GET /get_logs` to confirm expected
  log lines with zero unexpected warnings/errors, `GET /activate_tab` +
  `GET /get_game_view`/`/get_swapchain` for a screenshot of a new panel, `GET
  /render_graph` for the new pass's JSON, then `stop_app_background`.

### 3.6 — Reading order for whoever implements this

Read this file, then the phase you were assigned, in full, before writing any
code. Each phase file is self-contained but assumes everything in this file
(goal, situation, Locked Design Decisions, Findings A-G, non-goals) as shared
context — it will not repeat all of it.

- `PHASE1_TOOLCHAIN_VERIFICATION.md`
- `PHASE2_ENABLE_EXPORTS_FEASIBILITY_PROBE.md`
- `PHASE3_FOLDER_LAYOUT_AND_CMAKE_AUTODISCOVERY.md`
- `PHASE4_SHADER_COMPILATION_WIRING.md`
- `PHASE5_PROJECTASSEMBLYHOST_RUNTIME_LOADER.md`
- `PHASE6_COMPILE_TRIGGER_AND_BUILD_AUTOMATION.md`
- `PHASE7_EDITOR_PANEL_CAPABILITY_BRIDGE.md`
- `PHASE8_RENDER_PASS_CAPABILITY_BRIDGE_AND_CAMPAIGN_CLOSEOUT.md`
