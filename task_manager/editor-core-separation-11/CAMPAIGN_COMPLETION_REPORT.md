# CAMPAIGN COMPLETION REPORT — `editor-core-separation-11`
## "Project Assembly" System (per-developer, user-authored C++/shader code that links straight into `GreatTamanaEditor.exe` with zero engine recompilation)

**Status:** COMPLETE — all 8 phases done, verified, committed.

---

## What shipped

A per-developer, single-project, `.gitignore`d folder (`Projects/<Name>/`)
holding real, user-authored C++ and GLSL shader source that compiles into
two ordinary Windows `.dll`s (`<Name>_Game.dll`, `<Name>_Editor.dll`) which
`GreatTamanaEditor.exe` loads at its own startup and which then call real,
live, non-ABI-wrapped engine types (`gte::Core&`, real ImGui, real
`rg::RenderGraphBuilder`) directly — zero recompilation of the engine itself
for a content change, and zero new per-feature ABI surface to design/maintain
(unlike the existing `plugins/gte_plugin_abi` system, which this campaign
never touched, edited, or depended on).

Both concrete, working, end-to-end proofs the campaign's own PHASE0 set as
its actual Definition of Done are done, live, and permanently kept as this
system's own smoke-test fixture, `Projects/ProjectAssemblyProbe/`:

1. **A real, dockable, genuinely interactive Editor panel** ("Probe Panel", a
   click counter) contributed entirely from `ProjectAssemblyProbe_Editor.dll`,
   calling real `ImGui::*` functions directly — no ABI wrapper (PHASE7).
2. **A real render-graph compute pass**
   (`"ProjectAssemblyProbe.FillTexture"`) contributed entirely from
   `ProjectAssemblyProbe_Game.dll`, using genuine `rg::RenderGraphBuilder`/
   `rg::RenderPipeline` types directly, with its own freshly-compiled `.spv`
   shader — visible in the Editor's real "Render Graph" panel and
   `GET /render_graph` (PHASE8).

## Final build/ctest numbers (before/after this campaign)

- **Before** (`editor-core-separation-10`'s own documented baseline,
  `task_manager/editor-core-separation-10/CAMPAIGN_COMPLETION_REPORT.md`,
  the most recent campaign under `task_manager/` to run a full suite before
  this one): **1888 tests, 100% passing, 2 legitimate environment-gated
  skips** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  `CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`).
- **After** (this campaign's own PHASE8 full clean build + full regression
  pass): **1888 tests, 100% passing, the identical 2 legitimate skips.**
  **Zero delta** — this campaign added no new Tier-1 test file (a deliberate
  scope choice: every one of this campaign's own capabilities is an
  integration-level proof, verified live via `run_app_background`/
  `gte_send_request`, not a new pure-logic unit worth its own test), so an
  unchanged total with 100% passing is the correct, expected outcome, not a
  gap. Full clean build (`cmake -S . -B build` + `cmake --build build`, no
  target filter): **183/183 steps succeeded, zero errors.** Full `ctest -C
  Debug --output-on-failure`: 173.44 sec total.

## Real, mechanically-confirmed deviations/gaps found across all 8 phases
(none silently papered over — every one is documented, permanently, in its
own phase's completion report and/or `docs/conventions/project-assembly-system.md`)

1. **PHASE1** — the phase file's own `gte_core_player_link_probe` outer
   configure command omits `-G Ninja` (unlike its sibling command one line
   above); this machine's default CMake generator resolution
   (`NMake Makefiles`, no `nmake.exe` on `PATH`) makes the literal command
   fail outright. Fixed with a one-line, non-ambiguous `-G Ninja` addition,
   no probe file edited.
2. **PHASE2 (the single most important finding of the whole campaign)** —
   `WINDOWS_EXPORT_ALL_SYMBOLS` (CMake's own generic, MSVC-flavored
   "export every symbol from an `.exe`" property) is a **documented,
   intentional no-op on MinGW/GNU `ld`** (`cmake --help-property
   WINDOWS_EXPORT_ALL_SYMBOLS`: "implemented only for MS-compatible tools on
   Windows"). Confirmed, live, in a standalone probe
   (`tools/ci/gte_project_assembly_link_probe/`): setting the CMake property
   alone left `probe_module.dll` failing to link against un-annotated host
   symbols. The real, working mechanism is GNU `ld`'s own native
   `-Wl,--export-all-symbols` linker flag
   (`target_link_options(... PRIVATE "-Wl,--export-all-symbols")`), applied
   ALONGSIDE the (harmless, forward-compatible, but inert-here) CMake
   property. This exact recipe was then applied to the real
   `GreatTamanaEditor` target in PHASE3.
3. **PHASE3** — pulled PHASE4's `gte_add_project_shaders()` and PHASE7's
   `gte_project_assembly_apply_editor_header_paths()` (STEP 1 only) forward
   into the same session, per each phase file's own explicit permission to
   do so rather than leave a permanent stub — recorded so PHASE4/PHASE7
   knew exactly what had already been done for them.
4. **PHASE4** — a genuine, previously-undetected dependency-wiring gap in
   the SHARED `gte_add_shader()`/`cmake/CompileShaders.cmake` mechanism: an
   incremental, shader-only, target-scoped rebuild of a Project Assembly
   target does not reliably re-stage the compiled `.spv` next to the
   `.dll` (the compiled shader is wired into the consuming target's link
   edge as an ORDER-ONLY prerequisite, never a real staleness-tracked
   input, for a `SHARED` library target whose runtime output directory
   differs from `CMAKE_BINARY_DIR` — invisible for `GreatTamanaEditor`
   itself, since its own intermediate/staged shader paths are literally the
   SAME file). Documented as a real, silent-staleness risk rather than
   fixed (fixing it would mean touching shared, cross-cutting
   `cmake/CompileShaders.cmake` infrastructure every other internal shader
   also depends on — explicitly out of this campaign's own additive-only
   scope, LDD1). PHASE8 sidestepped it by always also editing the
   consuming `.cpp` file in the same build, which forces a real relink.
5. **PHASE5** — `gte_core`'s own `.cpp` source list in root `CMakeLists.txt`
   is explicit and hand-maintained (not a `file(GLOB ...)`), a gap neither
   phase file's own instructions called out: a brand-new `gte_core`-owned
   `.cpp` file (`ProjectAssemblyHost.cpp`) compiled cleanly but failed at
   the FINAL LINK step with undefined references until it was added to that
   list explicitly. Flagged proactively for PHASE6, which applied the same
   fix to `ProjectAssemblyBuildRunner.cpp` before ever attempting its own
   first compile.
6. **PHASE6** — a genuine Windows command-line-quoting bug: naively
   wrapping `gte::ExecutableDirectory()`'s own trailing-backslash path in a
   plain `"\"" + path + "\""` pair is a real, confirmed argv-parsing bug (a
   backslash immediately before a closing quote escapes that quote, per the
   standard MSVCRT/`CommandLineToArgvW()` rule), silently merging the rest
   of the command line into one argument. Fixed with a small, correct,
   standard Windows-argv-quoting helper (`QuoteWindowsArgument()`). Also
   discovered and documented a significant, permanent architectural
   consequence of LDD2+LDD4 combined: a Project Assembly `.dll` already
   loaded by the CURRENTLY RUNNING instance can never be successfully
   recompiled by that same instance (Windows locks a mapped DLL image
   against being overwritten by the linker) — confirmed mechanically in
   both directions (fails when locked, succeeds when not).
7. **PHASE7** — none of substance; the two functions PHASE3 had already
   pulled forward (Finding A's header propagation, Finding G's
   `ImGuiEditorLayer.cpp` gate widening) both worked correctly on the first
   real, live test, including a genuinely fresh, separate
   `GTE_ENABLE_PLUGINS=OFF` build tree proving Finding G's fix concretely
   (not merely by code inspection).
8. **PHASE8** — a genuine lambda-capture compile error in the phase file's
   own Step 2 code sketch (`desc.execute`'s inner lambda read
   `texDesc.width`/`.height` without capturing them — only `outputHandle`
   and `&renderer` were captured). Fixed by capturing
   `width = texDesc.width, height = texDesc.height` by value instead.

## Explicit, permanent, load-bearing facts (do not silently lose these again)

- **The shared-CRT MinGW toolchain switch this whole system depends on was
  never a tracked, deliberate decision anywhere under `task_manager/` — it
  happened as ad-hoc local machine setup, sometime between
  `editor-core-separation-9` and 2026-09-28.** PHASE1 mechanically
  re-confirmed it is still true, right now; this fact is now permanently
  recorded in `AGENTS.md`'s own new "Project Assembly System" section (and
  restated in `docs/conventions/project-assembly-system.md`) specifically so
  a future reader never mistakes an OLDER campaign's own honest "no actual
  switch happened yet" caveat for still being true today.
- **`WINDOWS_EXPORT_ALL_SYMBOLS` alone is insufficient on this toolchain** —
  `-Wl,--export-all-symbols` is the real, load-bearing mechanism (PHASE2/3).
- **`GTE_ENABLE_PROJECT_ASSEMBLIES` is completely independent of
  `GTE_ENABLE_PLUGINS`** — every CRT-linkage/registry-gate decision this
  campaign made was deliberately re-derived from scratch rather than reusing
  the OTHER system's flag, closing two real, silent-breakage hazards
  (Findings D and G) the original, pre-campaign design docs never
  anticipated.
- **A Project Assembly's own `.dll` can never be recompiled by the SAME
  running instance that already loaded it** (PHASE6) — a permanent,
  by-design consequence of this system's own architecture (LDD2+LDD4
  combined), not a bug to eventually fix.
- **An incremental, shader-only rebuild of a Project Assembly target may not
  re-stage the compiled `.spv`** (PHASE4) — always rebuild fully, or also
  touch the consuming `.cpp`, after any shader-only edit.

## Explicit, honest statement of what remains open (deliberately deferred,
never silently dropped)

**On-screen Game View compositing for a Project Assembly's own render pass
is confirmed NOT SAFE as this engine exists today** (PHASE8's own Step 3
investigation): `RenderGraph::EnsureTextureResolved()` tracks resource state
PER `TextureHandle`, never per underlying physical resource, so a second,
independent `ImportTexture()` of the same physical `RenderTexture*` the
engine's own internal Game-View-compositing chain already imports gets NO
automatic memory barrier against that chain's own reads/writes of the
identical image — a real, structural gap in this codebase (no mechanism
anywhere lets `RenderGraphBuilder` alias one handle onto another's
already-tracked physical resource), not merely an untried idea. This is
recorded, explicitly, as deliberately deferred future work for a SEPARATE,
future campaign that would need to add real handle-aliasing support to
`RenderGraphBuilder` first — this campaign's own Definition of Done never
promised on-screen compositing, only "visible in the Render Graph
panel/HTTP endpoint," which is fully proven.

Two smaller, permanent, honestly-disclosed limitations, both restated in
`docs/conventions/project-assembly-system.md` so a future reader does not
have to rediscover them:

- The `gte_add_shader()`/`cmake/CompileShaders.cmake` shader re-staging gap
  (PHASE4, above) — a real, silent-staleness risk for a shader-only
  incremental rebuild, deliberately left unfixed (shared, cross-cutting
  infrastructure outside this campaign's additive-only scope).
- The permanent "cannot recompile a currently-loaded Project Assembly `.dll`"
  architectural fact (PHASE6, above) — a real usability gap for whoever
  designs a future permanent "Compile" UI, flagged prominently rather than
  silently discovered by that future implementer from scratch.

## Per-phase summary

- **PHASE1 — Toolchain Verification.** Mechanically re-confirmed (not
  assumed from PHASE0 alone) that the shared-CRT MinGW toolchain switch is
  genuinely active: 6 concrete facts re-checked, both pre-existing standing
  regression probes (`gte_plugin_isolation_probe`,
  `gte_core_player_link_probe`) rebuilt and re-run, exit code 0 both times,
  a live HTTP smoke test of `GreatTamanaEditor.exe` showed zero regression.
  Wrote (but did not commit) the draft `AGENTS.md` disclosure text PHASE8
  later finalized.
- **PHASE2 — `ENABLE_EXPORTS` + Link-Against-`.exe` Feasibility Probe.** New,
  permanent, standalone probe
  (`tools/ci/gte_project_assembly_link_probe/`) proved the whole
  "a `.dll` links against the already-running `.exe`" mechanism genuinely
  works on this repo's real toolchain, including the ImGui-global-state risk
  (Finding A) — one `GImGui` context pointer confirmed shared, live, across
  the `.exe`/`.dll` boundary. Found the campaign's single most important
  technical correction (`WINDOWS_EXPORT_ALL_SYMBOLS` no-op on MinGW, above).
- **PHASE3 — Folder Layout + CMake Auto-Discovery + `gte_add_project()`.**
  The `Projects/` folder, `.gitignore` entry, `GTE_ENABLE_PROJECT_ASSEMBLIES`
  option, `GreatTamanaEditor`'s own `ENABLE_EXPORTS`/
  `-Wl,--export-all-symbols` flip, the new `cmake/GteProject.cmake`
  (`gte_add_project()`, Finding C's header-propagation fix, Finding D's own
  independent CRT-linkage function), and an empty, buildable
  `ProjectAssemblyProbe_Game`/`_Editor.dll` pair. Proved Finding C's
  header-propagation fix is genuinely load-bearing via a real
  positive/negative-control test (build fails without it, succeeds with
  it). Pulled PHASE4 and PHASE7's own Step 1 forward, per each phase file's
  own explicit permission.
- **PHASE4 — Shader Compilation Wiring.** `gte_add_project_shaders()`
  (implemented during PHASE3's own session, re-verified independently, from
  scratch, in this phase's own dedicated session — one prior claim
  corrected: shader-only incremental rebuilds do NOT reliably re-stage,
  see above). `ProbeCompute.comp` compiles and stages correctly on a full
  build.
- **PHASE5 — `ProjectAssemblyHost` Runtime Loader.** The class that scans
  the output folder, `LoadLibraryW()`s each `.dll`, and calls the one fixed
  `GTE_RegisterProject` export with a real, live `gte::Core&` (and, for
  `_Editor.dll`, a real `gte::EditorHost&`). Explicitly, mechanically
  confirmed the "no Project Assembly at all" no-crash guarantee TWICE (once
  incompletely — stale build output still present — caught and corrected;
  once genuinely, with both the source folder and its build output absent).
- **PHASE6 — The "Compile" Trigger (Build Automation).**
  `TriggerProjectAssemblyCompile()`, a real, non-blocking `cmake --build`
  child process on its own dedicated `JobSystem::RegisterBackgroundThread()`
  thread (never `Schedule()`). Live-verified: real streamed build output for
  both a genuine success and a genuine, correctly-classified failure, a
  working in-flight guard, the main window staying fully responsive
  throughout (frame counter visibly advancing). Found and fixed a real
  Windows command-line-quoting bug; discovered and documented the permanent
  "cannot recompile a currently-loaded `.dll`" architectural fact.
- **PHASE7 — Capability #1: Custom Editor Panel.** Resolves Finding A
  concretely: `ProjectAssemblyProbe_Editor.dll` implements
  `IEditorPanelModule_v1` directly, ignoring the ABI's `ctx` parameter,
  calling real `ImGui::*` — proven with a genuinely interactive click
  counter. Also resolves Finding G: widened `ImGuiEditorLayer.cpp`'s
  `#if GTE_ENABLE_PLUGINS` gate to
  `#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES`, verified
  concretely with a genuine, fresh `GTE_ENABLE_PLUGINS=OFF` build showing
  the panel still draws real content, not just an empty dock tab.
  `gte::EditorHost&` confirmed NOT needed for this specific capability
  (resolved by testing, not assumed).
- **PHASE8 — Capability #2: Custom Render Pass + Campaign Closeout.**
  Resolves Finding B (`Core::RegisterProjectRenderPassProvider()`, a new
  public thin pass-through onto `m_offscreenRenderPipeline.Register()`) and
  Finding E (`RenderGraphBuilder::CreateTexture()` called directly inside
  the provider's own lambda, before constructing the deferred
  `RenderPassDesc`) concretely — a real compute pass filling a transient
  256x256 texture with a solid color, confirmed live, stable across
  multiple polls, in BOTH `GET /render_graph`'s JSON and the Editor's real
  "Render Graph" panel (the latter required a genuine OS-level window
  resize via `run_shell`/PowerShell `MoveWindow()`, since no HTTP mechanism
  exists to scroll a docked ImGui panel — a real, non-input-simulating
  technique, not an input-simulation workaround). Investigated and
  definitively resolved Step 3's on-screen-compositing open question as
  confirmed NOT safe, rather than leaving it untried. Ran the campaign's
  ONE full clean build (183/183 steps, zero errors) and ONE full `ctest`
  regression pass (1888/1888 tests passing, unchanged from the prior
  baseline, zero regressions), finalized the `AGENTS.md` entry (including
  the permanent toolchain-switch disclosure), wrote
  `docs/conventions/project-assembly-system.md`, and wrote both completion
  reports.

See each phase's own `PHASEn_COMPLETION_REPORT.md` in this same folder for
the full, itemized, per-phase detail this summary condenses.
