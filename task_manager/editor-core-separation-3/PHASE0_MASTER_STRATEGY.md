# PHASE0 — MASTER STRATEGY: A Real, Runtime-Loadable Plugin Architecture

Campaign folder: `task_manager/editor-core-separation-3/`

Source design doc (READ THIS FIRST, in full, before touching any phase):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\MODULAR_PLUGIN_ARCHITECTURE_STRATEGY_v1.md`
— every phase file in this folder cites that document's own Section numbers
(e.g. "Section 3.1") constantly; keep it open side-by-side while reading any
phase below.

Predecessor campaigns (read their own `CAMPAIGN_COMPLETION_REPORT.md` before
starting PHASE1 — they are the reason `gte_core.a`/`gte_editor.a` are two
separately-linked static libraries today, which this campaign builds on top
of, unchanged):
`task_manager/editor-core-separation-1/CAMPAIGN_COMPLETION_REPORT.md`,
`task_manager/editor-core-separation-2/CAMPAIGN_COMPLETION_REPORT.md`.

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE6_*.md`) refers back to this file as its Parent. Read this file in full
before starting any child phase.

Branch: stay on `feature/editor-core-separation`. Never switch branches.

---

## Step 1: The Goal (Where are we going?)

Give this engine a **real, dynamic, runtime-loadable plugin system**: a
feature can ship as one or more `.dll`s, dropped into a `plugins/` folder next
to the built executable, and the running engine discovers and uses them with
**zero recompilation of the engine itself** and **zero per-plugin code inside
`gte_core`/`gte_editor`**. Concretely, by the end of this campaign:

1. A `plugins/` folder containing a "runtime" `.dll` implementing a render
   feature capability makes the running `GreatTamanaEditor.exe` render that
   feature's own render-graph pass — with **zero hardcoded knowledge of that
   specific plugin anywhere in `Core`**.
2. A companion "editor" `.dll` in the same folder makes the Editor's dock
   layout show that plugin's own custom ImGui panel — with **zero hardcoded
   knowledge of that specific plugin anywhere in `gte_editor`**.
3. The exact same `plugins/` folder, loaded by a thin, headless, Player-shaped
   probe process (never having seen `gte_editor`'s source, exactly like
   `tools/ci/gte_core_player_link_probe` already proves for `gte_core.a`
   itself), still gets the runtime-tier plugin's behavior, while the
   editor-tier plugin sits there loaded but **inert** — never crashing,
   never doing anything, simply never asked for anything by Player-side code.
4. All of this rests on one carefully-designed, versioned, fingerprint-gated
   ABI boundary that is the single most-guarded surface in this whole design
   (Section 0.1 of the source design doc explains why, in full, and every
   phase below treats it that way).

This campaign implements **Milestones 0–3** of the source design doc's own
Section 11 phased roadmap. **Milestone 4 (hot reload — swapping a plugin's
`.dll` for a rebuilt one while the engine keeps running) is explicitly,
permanently OUT OF SCOPE for this campaign** — confirmed directly with the
project owner before writing this strategy. Section 10 of the source design
doc already recommends exactly this split; nothing here contradicts it.

**No specific real engine feature is migrated onto this mechanism by this
campaign.** Every plugin this campaign builds is a deliberately tiny,
throwaway-quality proof (a solid-color clear pass; one "hello from a plugin"
ImGui panel) — exactly what the source design doc's own Section 11 and
Section 12 ("Non-goals") both insist on. Picking a real feature to migrate is
a separate, later decision, not this campaign's job.

---

## Step 2: The Situation / The Problem (Where are we now?)

Re-confirmed by direct reading of the real, current source during this
campaign's own investigation, on this exact branch:

### 2.1 — There is no dynamic loading anywhere in this codebase today

`search_in_dir(src, "LoadLibrary")` returns zero matches. Every one of this
engine's ~15 "subsystem on/off" switches
(`GTE_ENABLE_PROFILER`/`GTE_ENABLE_JOB_SYSTEM`/`GTE_ENABLE_NETWORK`/
`GTE_ENABLE_PROJECT_PANEL`) is a **compile-time** CMake option gating code
that is still always statically linked into one of exactly two final
binaries. There is no precedent anywhere in this repo for a third,
independently-compiled, independently-loaded binary artifact. This campaign
introduces that concept for the first time.

### 2.2 — `gte_core.a`/`gte_editor.a` are exactly what the source design doc's Section 1 assumes, already proven standalone-linkable

`editor-core-separation-1`/`-2` already delivered: `gte_core.a` (engine —
Renderer/ECS/Game/RenderGraph/`gte::Core` facade), `gte_editor.a` (ImGui/
gizmos/Frame Debugger/panels, depends on `gte_core.a` one-way, never the
reverse), a real, mechanically-proven standalone link
(`tools/ci/gte_core_player_link_probe`, a genuine `.exe` linked against
`gte_core.a` **alone**), and an established "Bucket-B capability interface"
pattern (`src/Core/EditorCapabilities.h`'s `ISceneIOCapability`/
`ILogQueryCapability`, each a tiny pure-virtual interface a `gte_core`-tier
call site holds a nullable pointer to, asking a plain runtime null-check —
**this is the exact same shape `IPluginModule::QueryCapability()` generalizes
to a string-keyed lookup**, so this campaign is not inventing a new idiom,
it is generalizing one this codebase already uses in two places).

`Core`'s own per-frame orchestration (`Core::BuildFrame()`,
`src/Core/Core.cpp`) already has a real, generic, pluggable extension point
that did not exist when the source design doc's Section 5 was written in the
abstract: `rg::RenderPipeline`/`RenderPassProvider`
(`src/Renderer/RenderGraph/RenderPipeline.h`,
`Core::RegisterOffscreenRenderPipelineProviders()`) — a named, ordered list of
provider lambdas, each declaring zero or more render-graph passes, looked up
generically every frame. **Section 5's abstract "Core::BuildFrame() loop"
becomes, concretely in this codebase, one more named provider registered onto
`m_offscreenRenderPipeline`** — see PHASE3.

`src/Core/EditorPanelCatalog.h` is exactly the "closed list a plugin can never
add itself to" the source design doc's Section 6 already predicts by name —
a hand-written `constexpr` array of panel-name strings. This campaign evolves
it into the registry Section 6 describes — see PHASE4.

### 2.3 — Two real gaps the source design doc leaves open, resolved by direct discussion before writing this strategy (see PHASE0's own "Locked Design Decisions" below for the full reasoning)

1. **How does a plugin `.dll` actually call engine code at all?** The source
   design doc's Section 3.3/3.4 talks about a "curated subset of `gte_core`'s
   headers" a plugin may depend on, but never actually resolves whether that
   means the plugin **links** `gte_core` symbols directly (which would mean
   `gte_core` becomes a shared library, a structural change bigger than
   anything this campaign should attempt) or never touches a real `gte_core`
   symbol at all. **Resolved (confirmed via `ask_questions`): Option A — a
   plugin NEVER links or calls a real `gte_core`/`gte_editor` function
   directly, ever.** It only ever receives small, curated, pure-virtual
   wrapper interfaces (defined in the new `gte_plugin_abi`), whose real
   implementations live inside `gte_core`/`gte_editor` and forward to the
   real internal types. `gte_core.a`/`gte_editor.a` stay plain static
   libraries, byte-for-byte unchanged in their own CMake target `TYPE`. See
   "Locked Design Decision #2" below.
2. **The referenced sibling document does not exist.** Section 11's own
   Milestone 3 text says "Build the minimal Player host probe already
   described in the sibling static-modularity document's own Phase 2" —
   `browse_dir` on
   `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\`
   confirms only `MODULAR_PLUGIN_ARCHITECTURE_STRATEGY_v1.md` itself exists in
   that folder; no `PLAYER_BUILD_CLEAN_EVICTION_STRATEGY_v1.md` (or any
   sibling doc) is actually present anywhere in this repository or its
   sibling ideas folder. **Resolved**: PHASE5 builds directly on top of
   real, already-existing, already-committed infrastructure instead —
   `tools/ci/gte_core_player_link_probe` (a real, minimal, Player-shaped
   executable proving `gte_core.a` links standalone, `editor-core-separation-2`
   campaign) and `tests/Fakes/HeadlessSurfaceProvider.h` (a real
   `VK_EXT_headless_surface`-based `ISurfaceProvider`, `editor-core-separation-1`
   campaign, PHASE18) — see PHASE5 for the exact, concrete plan this
   resolves into.

### 2.4 — A third real gap the source design doc's Section 6 glosses over entirely: ImGui's own single, shared, global context

Section 6's sketch (`panel->GetPanelName()`, `dockLayout.RegisterPanel(...)`)
never addresses HOW a plugin's `IEditorPanelModule` is supposed to actually
draw ImGui widgets. Dear ImGui keeps exactly one live, mutable global context
pointer (`GImGui`) per process; every `ImGui::*` call implicitly reads/writes
through it. If an editor-tier plugin `.dll` were to call `ImGui::Begin()`/
`ImGui::Text()` directly, its own statically-linked copy of the ImGui library
(a plugin, per Locked Design Decision #2, cannot link `gte_editor.a` either)
would operate on **its own separate, never-initialized `GImGui` pointer** —
an immediate, guaranteed crash the moment any editor-tier plugin's panel is
first drawn, completely independent of anything the ABI fingerprint gate
(Section 8) can ever catch (a fingerprint match proves layout compatibility,
not "these two separately-linked copies of ImGui share one live context").
This campaign's PHASE4 closes this gap the same way it closes the
`RenderGraphBuilder` gap: an editor-tier plugin never calls a real `ImGui::*`
function at all — only a small, curated `IPluginPanelDrawContext` interface,
implemented host-side inside `gte_editor.a` (which already correctly shares
the one true `GImGui` context, being all one binary), forwarding to the real
ImGui calls. See PHASE4, "Locked Design Decision — the ImGui-context hazard."

---

## Step 3: The Plan — Locked Design Decisions (do not re-litigate these)

1. **True dynamic, runtime-loadable `.dll` plugins, self-registering,
   always-all-in** (source design doc Section 0, decisions #1-#4, unchanged,
   confirmed still wanted). A plugin's own code registers its capabilities
   the moment it's loaded; nothing outside the plugin is edited to add a new
   plugin; every `.dll` found in `plugins/` is loaded, unconditionally, no
   per-project manifest/toggle UI.
2. **A plugin `.dll` NEVER links or calls a real `gte_core`/`gte_editor`
   symbol directly (confirmed via `ask_questions`).** Every single
   cross-boundary operation, in EITHER direction, goes through a small,
   curated, pure-virtual interface declared in the new `gte_plugin_abi`
   module — implemented, on the host side, by a thin adapter class living
   inside `gte_core`/`gte_editor` that forwards to the real internal type
   (`rg::RenderGraphBuilder`, ImGui, ...). `gte_core.a`/`gte_editor.a` remain
   plain CMake `STATIC` libraries — this campaign changes their own
   `target_sources()` lists (adding new adapter/registry files) but never
   their `TYPE`, and never turns either into a `SHARED`/`.dll` target. This
   is a strictly MORE conservative reading of the source design doc's own
   Section 0.2 Option (B) — see decision #4 below for why.
3. **Every cross-boundary interface method signature uses ONLY plain,
   built-in C++ types — `const char*`, `bool`, `float`, `int`, fixed-size POD
   structs, raw non-owning pointers/references to `gte_plugin_abi`'s own
   interface types.** Never `std::string`, `std::vector`,
   `std::filesystem::path`, or any real `gte_core`/`gte_editor` class type, by
   value or by reference, anywhere in `gte_plugin_abi`. This is a
   deliberately STRONGER rule than the source design doc's own Section 3.4
   (which permits rich STL types under its Option (B) fingerprint gate) —
   chosen because, combined with decision #2 above, it means `gte_plugin_abi`
   needs ZERO real `gte_core` header at all, anywhere, ever (every rich type
   a plugin might conceptually want — a `RenderGraphBuilder&`, an `ImGuiIO&`
   — already never crosses the boundary in the first place, per decision #2).
   This closes a whole class of STL-ABI risk (Section 0.1's own second bullet)
   for zero real cost, since the curated wrapper always converts to/from the
   rich internal type entirely on the host side of the call. `const char*`
   returned across the boundary must always be a stable, static-duration
   string literal (documented at each such declaration) — never a
   freshly-heap-allocated buffer, so there is never an ownership question to
   resolve for it.
4. **Because of decisions #2 and #3, Section 8.1's "Iron Rule" simplifies
   for this campaign's own real call shape — resolved and documented here,
   not deferred:**
   - The **shared/DLL CRT requirement** (Section 8.1, point 1) is still
     real and still enforced (see PHASE1) — it is what guarantees one
     process-wide heap, needed the moment ANY memory could conceivably be
     allocated on one side and freed on the other, even indirectly.
     **Confirmed via `ask_questions`**: this repository's build is
     currently, mechanically confirmed (`objdump -p` on the real, built
     `GreatTamanaEditor.exe` shows zero dependency on `libstdc++-6.dll`/
     `libgcc_s_seh-1.dll`/`libwinpthread-1.dll`), fully static-linked —
     flipping to shared libgcc/libstdc++ is a REAL, visible packaging
     change (three new runtime `.dll`s must now be staged next to the
     built executable, mirroring the already-existing
     `sdl3_copy_runtime_dll()` precedent, `cmake/FetchSDL3.cmake`). This is
     accepted, and PHASE1 implements the staging step so the built `.exe`
     keeps running correctly on a machine with no MinGW installed, exactly
     as `sdl3_copy_runtime_dll()` already keeps `SDL3.dll` working.
     **This flip is a JOINT requirement, not a host-only one — restated
     explicitly here so no later phase mistakenly treats it as satisfied by
     `GreatTamanaEditor.exe` alone**: EVERY binary that can ever sit on
     either side of the plugin ABI boundary in a `GTE_ENABLE_PLUGINS=ON`
     configuration — the host executable (`GreatTamanaEditor`), every
     plugin `.dll` (`demo_hello_world`, `demo_render_feature`,
     `demo_editor_panel`), and every standalone probe executable that ever
     calls `PluginHost::LoadPlugins()`/`LoadLibraryW()` on a real plugin
     `.dll` (PHASE2's handshake probe, PHASE5's isolation probe, and
     PHASE5's own extension of `tools/ci/gte_core_player_link_probe`) —
     must consistently get the SAME shared/DLL CRT link mode applied. A
     plugin `.dll` left with its own private, statically-linked CRT copy
     while the host it loads into uses the shared CRT is NOT a safe
     "matching pair" (see Section 8.1's own point 1 in the source design
     doc) even though the fingerprint's `sharedRuntimeLinkage` field might
     otherwise be baked identically into both from the same build
     configure — PHASE1 defines one reusable CMake helper for this reason,
     and every later phase that adds a new plugin `.dll` or a new
     plugin-loading probe executable must call it for that new target too,
     never assume `GreatTamanaEditor`'s own flip is sufficient on its own.
   - **Owned-handle wrapper types / debug-build allocation tagging**
     (Section 8.1, points 2-3): **honestly deferred, not built, for a
     concrete, stated reason** — decision #3 above means this campaign's
     ENTIRE curated ABI surface (Milestones 0-3) has **zero cross-boundary
     calls that transfer heap ownership in either direction** — every
     method either borrows (a `const&`/pointer the callee never frees),
     fills in a caller-owned buffer, or returns a stable string literal /
     plain value. Building a generic owned-handle-with-bundled-deleter
     mechanism today, with no real call site that needs it yet, is exactly
     the "pre-paying for a problem you don't have yet" this codebase's own
     established philosophy (see `AGENTS.md`'s GPU-timing/Profiler
     precedents) argues against. **This is flagged as a real, explicit,
     load-bearing Non-Goal (see below) — the moment a future capability
     needs to hand over real ownership (e.g. a plugin allocating a texture
     the host must eventually free), THAT is the point this mechanism must
     be designed for real, not before.**
   - **Section 8.1, point 4 ("never return an owned object BY VALUE across
     the boundary")** is trivially, permanently satisfied by decision #3
     (nothing not-plain ever crosses at all).
5. **The fingerprint gate (Section 8) is real, mandatory, and checked FIRST,
   always** — `GtePluginAbiFingerprint`, a fixed-size POD, generated at
   CMake configure time from the real, live compiler id/version/build config
   this exact build uses (see PHASE1 for its exact fields). A mismatch is
   always a clean, logged skip of that one `.dll`, never a crash, never a
   partial load.
6. **`GTE_ENABLE_PLUGINS` is a new CMake option, default `ON`, mirroring
   `GTE_ENABLE_PROFILER`/`GTE_ENABLE_JOB_SYSTEM`/`GTE_ENABLE_NETWORK`
   exactly** (confirmed via `ask_questions`) — "always compiles, the switch
   only changes whether `Core::LoadPlugins()` is ever actually called with a
   real, existing folder at runtime." Turning it `OFF` means the engine
   never scans/loads any `.dll` at all, at zero cost, while
   `PluginHost`/every capability interface still compiles and is still
   testable.
7. **Where everything lives, physically (confirmed via `ask_questions`)**: a
   new top-level `plugins/` folder holds BOTH `gte_plugin_abi`'s own headers
   (`plugins/gte_plugin_abi/`) AND every demo plugin's own small CMake
   sub-project (`plugins/demo_hello_world/`, `plugins/demo_render_feature/`,
   `plugins/demo_editor_panel/`) — mirroring `tools/ci/`'s own existing
   "small, focused sub-projects living outside `src/`" precedent, but as its
   own clearly-named top-level concern rather than living under `tools/ci/`
   (which is reserved for CI-probe-flavored projects, not feature-demo code).
   The RUNTIME output folder every built demo plugin's `.dll` is copied into
   (the folder `Core::LoadPlugins()` actually scans at engine startup) is
   `<build-dir>/plugins/` (next to the built `GreatTamanaEditor.exe`), kept
   textually distinct in every phase file from the SOURCE folder
   `plugins/` at the repo root, to avoid any ambiguity.
8. **`gte_core` owns exactly ONE `PluginHost` instance per process — not
   two.** Re-reading the source design doc's Section 1/6/7 together: "both
   `Core::BuildFrame()` and `EditorHost`/`DockLayout` call into" the SAME
   Plugin Host, and "both `GreatTamanaEditor.exe` and a future Player
   executable run their OWN Plugin Host instance" — read together, this
   means one scan/load pass per PROCESS (Editor process: one; a future
   Player process: a separate one, in its own separate process), never two
   independent scans of the same folder inside the SAME process (which
   would double-`LoadLibrary()` every `.dll` and construct two independent,
   inconsistent `IPluginModule` instances per plugin — a real, avoidable
   correctness hazard the source design doc's own wording could be
   misread into). `gte_editor`'s own code (`DockLayout`/`EditorHost`) reads
   the SAME registry `Core` already populated, querying a different
   capability string, never re-scanning the folder itself.
9. **Plugins load once, at host-construction time, never re-scanned per
   frame.** `Core` gains one new, small, explicitly-called method,
   `Core::LoadPlugins(const std::filesystem::path& pluginsDirectory)` —
   called exactly once by each host (`EditorHost`'s constructor; the PHASE5
   probe's own `main()`), mirroring `Core::SetEditorLayerHook()`'s own
   existing "host explicitly wires this in after construction" convention
   exactly, rather than growing `Core`'s own frozen 2-parameter constructor
   signature (`ISurfaceProvider&`, `IHostServices&`) with a third mandatory
   parameter.
10. **Every new interface/class follows this codebase's own existing
    conventions** (`AGENTS.md`): `namespace gte { ... }`, RAII, a pure
    interface has no data members and a `virtual ~Interface() = default;`,
    Windows-only APIs (`LoadLibraryW`/`GetProcAddress`/`FreeLibrary`) are used
    directly with no extra platform-abstraction layer (this repository's own
    root `CMakeLists.txt` already hard-fails on any non-Windows platform —
    see `if(NOT WIN32)` — so there is no existing precedent, and no present
    need, to abstract over a platform this codebase does not support).

---

## Step 3 (continued) — Phase Sequence and Dependency Order

Execute STRICTLY in this order — each phase assumes every earlier phase is
already done.

| # | File | One-line summary | Maps to source-doc Milestone | Risk |
|---|------|-------------------|-------------------------------|------|
| 1 | `PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md` | New `plugins/gte_plugin_abi/` headers: fingerprint struct, `IPluginModule`, the 3 fixed exports, CRT-linkage flip + runtime-DLL staging, `docs/conventions/plugin-architecture.md` | Milestone 0 (design) | Medium |
| 2 | `PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md` | New `gte::PluginHost` (`src/Core/Plugins/`), wired into `Core`; one real, trivial "hello world" demo plugin `.dll`; a standalone handshake-proving probe | Milestone 0 (proof) | Medium-High |
| 3 | `PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md` | `IRenderFeatureModule_v1` + `IPluginRenderPassBuilder` wrapper; new `"PluginRenderFeatures"` `RenderPipeline` provider inside `Core`; one throwaway render-feature demo plugin | Milestone 1 | High — the core of this campaign |
| 4 | `PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md` | `IEditorPanelModule_v1` + `IPluginPanelDrawContext` wrapper (closes the ImGui-context hazard, Step 2.4); `EditorPanelCatalog.h` evolves into a real registry; one throwaway editor-panel demo plugin | Milestone 2 | High |
| 5 | `PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md` | Extends the Milestone-0 handshake probe into a Player-shaped isolation probe (no GPU needed) proving "always all-in, editor-tier stays inert"; optional GPU-gated bonus extension of `tools/ci/gte_core_player_link_probe` | Milestone 3 | Medium |
| 6 | `PHASE6_FINAL_REGRESSION_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md` | Full clean build + full `ctest`; re-run every probe fresh; live HTTP smoke test; `CAMPAIGN_COMPLETION_REPORT.md` | — (verification) | Low (but mandatory) |

---

## Universal Rules Every Phase Must Follow

1. **Read this file (Parent) in full before starting.** Read every
   `PHASEn_COMPLETION_REPORT.md` left by a prior phase in this same folder —
   it may contain a clue, a deviation, or a discovered fact the next phase
   needs.
2. **Stay on branch `feature/editor-core-separation`.** Never switch
   branches.
3. **Namespace/RAII/Clean-Architecture conventions from `AGENTS.md` apply to
   every new file**, including every file under the new `plugins/` folder
   (a plugin's own `.cpp`/`.h` files still live inside `namespace gte { ... }`
   — a plugin is still this project's own code for now, just packaged
   differently, not a genuinely external vendor's code).
4. **No full build/full `ctest` except PHASE6's own final checkpoint.**
   Every other phase does an incremental, TARGETED compile check only —
   e.g. `cmake --build build --target gte_core`, then
   `cmake --build build --target gte_editor`, then
   `cmake --build build --target GreatTamanaEditor`, then the specific new
   plugin `.dll` target that phase added — plus a targeted
   `run_app_background` + `gte_send_request` smoke check where the phase
   touched runtime behavior. Do not run the full `GreatTamanaEngineTests`
   suite except PHASE6.
5. **Use the engine's own logging + `gte_send_request` for debugging. Never
   add `std::cout`/`printf`/`OutputDebugString` debug prints inside any
   `gte_core`/`gte_editor` code.** Use `GTE_LOG_*` and pull logs back via
   `GET /get_logs`. **Exception, stated explicitly**: a plugin `.dll`'s own
   demo code, and any small, throwaway, standalone CI-style probe `main()`
   (mirroring `tools/ci/gte_core_standalone_probe`'s own existing precedent
   of being a genuinely separate, non-engine executable), MAY use plain
   `std::printf`/return codes for its own pass/fail reporting — it is not
   `gte_core`/`gte_editor` code and has no `GTE_LOG_*`/`NetworkServer` to
   report through in the first place. Never let this exception leak into
   any file that is part of `gte_core.a`/`gte_editor.a`'s own
   `target_sources()` list.
6. **Every phase must end with**: a compile-check result, a
   `PHASEn_COMPLETION_REPORT.md` written into this same folder, and a git
   commit (`git_add` + `git_commit`) of both the code changes and that
   report.
7. **Every phase-implementation task must call `ask_questions`** if it hits
   a genuine ambiguity this strategy doc does not resolve — do not guess
   silently on anything architecturally significant. Every task delegated
   from this campaign (including any task THAT task itself further
   delegates, if ever allowed to) must also be instructed to use
   `ask_questions` under the same rule.
8. **Implementation phases (PHASE1-PHASE6) must NOT call `delegate_task`, with
   exactly ONE narrow, explicit carve-out**: PHASE6 alone may call
   `delegate_task`, and only for the single purpose of spinning off a
   dedicated fix task for a genuine regression its own Step 3.2 full `ctest`
   pass surfaces (mirroring `editor-core-separation-2` PHASE5's own identical
   precedent) — see PHASE6's own Universal Rules section for the exact
   restatement of this carve-out. Every other implementation phase
   (PHASE1-PHASE5) must never call it at all, under any circumstance. Only
   the orchestrating session that authored this campaign's strategy files is
   otherwise allowed to delegate. A phase that discovers it is too large
   mid-flight should say so honestly in its own completion report rather
   than spawning sub-tasks itself.
9. **If a phase's own exact file paths/line numbers differ from what this
   strategy assumed** (real source drifts between when this strategy was
   written and when a phase actually runs), use `search_in_dir` to
   re-confirm the real, current location before editing — never edit blind
   against a guessed path/line number.
10. **Use the engine's network debugging features proactively.** After any
    phase that touches runtime behavior (PHASE2, PHASE3, PHASE4, PHASE5),
    launch the built executable via `run_app_background` and drive it via
    `gte_send_request` (`GET /get_swapchain`, `GET /get_game_view`,
    `GET /get_logs`, `GET /list_tabs`, `GET /activate_tab`) to visually/
    programmatically confirm the new plugin behavior is real and nothing
    regressed, then `stop_app_background` it. Do not skip this in favor of
    "it compiled, so it must be fine."
11. **Every new `.dll` (host executable AND every plugin) must be confirmed,
    mechanically, to actually exist on disk and actually load**, exactly
    like `editor-core-separation-2`'s own PHASE4/PHASE5 insisted for its own
    probe executable — "the build produced a file" is not the same claim as
    "the file is a real, correctly-exporting plugin `.dll`"; use
    `run_shell`'s `dumpbin`/`objdump` where a phase's own completion report
    needs to prove an export table shape, exactly as PHASE1/PHASE2 need to.

---

## Non-Goals (repeat, do not implement these under this campaign)

- **Milestone 4 — hot reload** (swap a plugin `.dll` for a rebuilt version
  while the engine keeps running) is explicitly out of scope, confirmed via
  `ask_questions`. `OnBeforeUnload()`/`OnAfterReload()` lifecycle hooks are
  not designed or implemented by this campaign.
- **No cross-process sandboxing.** A plugin bug still crashes the whole
  engine process — unchanged from the source design doc's own Section 9.
- **No per-project plugin selection UI/manifest.** "Always all-in" — every
  `.dll` in `plugins/` loads, unconditionally.
- **No cross-compiler/cross-vendor third-party plugin SDK.** Every plugin
  must be rebuilt against this exact repository's own toolchain/build
  fingerprint — there is no support, and no design accommodation, for an
  outside vendor shipping a plugin built with a different compiler.
- **No owned-handle-with-bundled-deleter mechanism, no debug-build
  allocation-tagging net** — deliberately deferred (Locked Design Decision
  #4 above) until a real, future capability actually needs to transfer heap
  ownership across the boundary. Flagged here, honestly, as real,
  intentionally-unbuilt scope, not silently dropped.
- **No real, existing engine feature (Atmosphere, GPU-Driven Batching, Frame
  Debugger, ...) is migrated onto this plugin mechanism.** Every plugin this
  campaign ships is a deliberately tiny, throwaway demo. Picking a real
  future migration target is separate, later, out-of-scope work.
- **`gte_core.a`/`gte_editor.a` never become `SHARED` libraries.** Locked
  Design Decision #2 makes this unnecessary — reversing it later is possible
  but is a separate, much larger future decision, not assumed or half-built
  here.
- **No renaming of `gte_core`/`gte_editor`/`Core`/`EditorHost`/
  `GreatTamanaEditor`.**
- **No real CI pipeline for this repository** — every new probe this
  campaign adds is a manually-invocable local CMake project, exactly like
  `tools/ci/gte_core_standalone_probe`/`tools/ci/gte_core_player_link_probe`
  already are, not a GitHub Actions workflow.
