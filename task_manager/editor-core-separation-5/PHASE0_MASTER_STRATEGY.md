# editor-core-separation-5 — PHASE0 MASTER STRATEGY

**Branch:** `feature/editor-core-separation` (stay on it — do not create a new branch).

**Project root:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

**This folder:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\task_manager\editor-core-separation-5`

This document is the ORCHESTRATOR for this whole campaign. Every child phase file
(`PHASE1_*.md` .. `PHASE5_*.md`) in this same folder must be read together with this
file before starting work on that phase. Every phase produces its own
`PHASEn_COMPLETION_REPORT.md` in this same folder when done, and ends with its own
`git_add` + `git_commit`.

**Use `ask_questions` whenever a real design ambiguity comes up that this master
strategy or the relevant phase file does not already resolve — never silently guess.
This applies transitively: if a phase (or any task it delegates) itself ever hands
off further work, that further work must also be told to use `ask_questions` for its
own genuine ambiguities.** (This master strategy's own planning pass was written
without live `ask_questions` access — see "Decisions made without `ask_questions`,
and why" below for every place a real judgment call was made instead of asking; an
implementer is free to challenge any of those calls via `ask_questions` if it looks
wrong once real code is in front of them.)

---

## Step 1: The Goal (Where are we going?)

Implement, for real, the proposal sitting at
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
PLUGIN_AUTHORING_ERGONOMICS_PROPOSAL_2026-09-24.md` ("Plugin Authoring Ergonomics —
Proposal (not yet implemented)"), in the actual repository. **Read that file in
full before starting ANY phase of this campaign — it is the primary source of truth
for the exact shape of the two new header-only files and the exact before/after
`.cpp` shapes this campaign produces.**

Concretely: today, writing a single-capability plugin (`demo_render_feature`,
`demo_editor_panel`) means hand-writing a second glue class
(`Demo*PluginModule : public IPluginModule`) that only ever does the same three
things (string-compare in `QueryCapability()`, cast, fill in a
`GtePluginModuleInfo` via three easy-to-get-wrong `std::strncpy(..., sizeof(x) -
1)` calls), plus hand-writing an `extern "C" { ... }` block of three
`__declspec(dllexport)` functions, verbatim, in every single plugin file. The goal
of this campaign is to make this **zero-ABI-change, purely additive**: two new
header-only files under `plugins/gte_plugin_abi/` that a plugin author can choose
to use, then migrate all four of this repo's own existing demo plugins onto them
as living proof it actually removes the boilerplate without changing any observable
behavior — same fingerprint, same three exports, same `GtePluginModuleInfo`
content, same capability strings, same render pass names, same panel name. Every
existing interface (`IPluginModule`, `IRenderFeatureModule_v1`,
`IEditorPanelModule_v1`, `IPluginRenderPassBuilder`, `IPluginPanelDrawContext`),
every fingerprint field, and the three fixed `extern "C"` export names stay
byte-for-byte identical — this campaign never touches `GtePluginAbiFingerprint.h`,
`IPluginModule.h`, `IRenderFeatureModule.h`, `IEditorPanelModule.h`,
`IPluginRenderPassBuilder.h`, `IPluginPanelDrawContext.h`, `PluginExports.h`, or
`PluginHost.cpp`/`PluginHost.h` at all.

**Non-goals (explicitly out of scope for this campaign):**

- No new plugin capability interface, no new render-graph pass type, no new
  editor-panel widget.
- No change to `PluginHost`'s load/scan/fingerprint-check logic, or to
  `EditorPanelRegistry`/`Core::LoadPlugins()`'s multi-render-feature-warning logic
  (`editor-core-separation-4` campaign) — those are done, working, and untouched.
- No hot reload, no cross-process sandboxing, no per-project plugin manifest/UI,
  no cross-compiler third-party plugin SDK — unchanged, permanent non-goals
  carried over from every prior `editor-core-separation-*` campaign.
- No actual switch of this repository's active `CMAKE_CXX_COMPILER` to a
  shared-runtime-capable toolchain — unrelated, deliberately deferred elsewhere.
- **This campaign does not add a fifth demo plugin implementing 2+ capabilities
  from one module.** `SingleCapabilityPluginModule<T>` is opt-in sugar for the
  common "one plugin, one capability" case; a plugin needing 2+ capabilities from
  one module still hand-writes its own `IPluginModule`, exactly like today — this
  campaign proves the common case is easier, it does not need to prove the
  uncommon case still works (it was never touched).

## Step 2: The Situation (Where are we now?)

The plugin ABI (`plugins/gte_plugin_abi/`) and `PluginHost` shipped by
`editor-core-separation-3` and hardened by `editor-core-separation-4` both work
correctly today. Four demo plugins exist and all load successfully:

| Plugin | Capability | Source file | Current line count (code, not counting this header comment) |
|---|---|---|---|
| `demo_hello_world` | none (`QueryCapability` always returns `nullptr`) | `plugins/demo_hello_world/HelloWorldPlugin.cpp` | ~57 |
| `demo_render_feature` | `IRenderFeatureModule_v1` | `plugins/demo_render_feature/RenderFeaturePlugin.cpp` | ~67 |
| `demo_render_feature_second` | `IRenderFeatureModule_v1` | `plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` | ~75 |
| `demo_editor_panel` | `IEditorPanelModule_v1` | `plugins/demo_editor_panel/EditorPanelPlugin.cpp` | ~69 |

Every one of these four files repeats the exact same shape: an `#include <cstring>`,
a hand-written `Demo*PluginModule : public IPluginModule` class doing a manual
`std::strcmp` dispatch and three manual `std::strncpy(..., sizeof(field) - 1)`
calls, and a verbatim `extern "C" { ... }` block of the same three
`__declspec(dllexport)` function bodies (only the `new gte::Demo*PluginModule()`
line differs). None of this is a bug — `editor-core-separation-3`/`-4`'s own
re-analyses never flagged it as one — it is pure ergonomics/boilerplate debt,
exactly as the proposal document frames it.

`plugins/gte_plugin_abi/` today has 9 header files + `PublicSurface.md` +
`CMakeLists.txt` (see that folder's own listing) — no `PluginExportsMacro.h`, no
`SingleCapabilityPluginModule.h` exist yet. The proposal document's own two
code listings are the exact starting point for Phase 1, **with one real
correction this campaign's own investigation found and Phase 1 must apply**: the
proposal's own sketch of `PluginExportsMacro.h` writes
`#include "GtePluginAbiFingerprintGenerated.h"` (a bare relative include). This is
**wrong** — every existing plugin `.cpp` in this repo (and
`HelloWorldPlugin.cpp`'s own explicit comment,
`plugins/demo_hello_world/HelloWorldPlugin.cpp` lines 11-18) already documents why:
that header is a CMake `configure_file()` OUTPUT living under
`<build-dir>/plugins/gte_plugin_abi/generated/gte_plugin_abi/
GtePluginAbiFingerprintGenerated.h`, resolved correctly only by relying on
`gte_plugin_abi`'s own `INTERFACE` include directory
(`target_include_directories(gte_plugin_abi INTERFACE ... "${CMAKE_CURRENT_BINARY_DIR}/generated")`),
i.e. the correct spelling is `#include "gte_plugin_abi/
GtePluginAbiFingerprintGenerated.h"` — exactly what every existing demo plugin
`.cpp` already writes. Phase 1 must use the CORRECT spelling, not the proposal
document's own literal (untested) sketch.

Four consumers currently depend on these demo plugins' exact observable
behavior — this campaign must not change any of them (only "the same output,
produced by less code"):

1. `tools/ci/gte_plugin_abi_handshake_probe/main.cpp` — loads
   `demo_hello_world.dll` directly via `LoadLibraryW`/`GetProcAddress`, prints
   `info.name`/`info.version`/`info.description` — must print byte-identical text
   after this campaign as before.
2. `tools/ci/gte_plugin_isolation_probe/main.cpp` — loads every `.dll` in
   `plugins/` via the real `PluginHost`, asserts `LoadedModuleCount() == 4` and
   exactly 2 modules answer `QueryCapability(kIRenderFeatureModule_v1_Name) !=
   nullptr` — must still assert true after this campaign.
3. `src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp`'s own comment references
   the literal pass name string `"DemoRenderFeaturePlugin_Clear"` appearing in the
   Editor's "Render Graph" panel — this exact string must not change.
4. The live `GreatTamanaEditor.exe`'s own startup log lines
   (`"Loaded plugin 'DemoEditorPanelPlugin' v1.0.0 from ..."`, etc., quoted
   verbatim in several `editor-core-separation-3`/`-4` completion reports) and its
   `list_tabs`/panel-registry behavior (`"Demo Plugin Panel"` panel name) — must
   read identically after this campaign.

## Step 3: The Plan (How do we get there?)

One phase builds the two new header files; three phases migrate the four demo
plugins onto them (grouped by capability shape, smallest risk first); one final
phase updates the two convention docs, runs the one full regression pass this
whole campaign is allowed, and closes out.

### Phase map

- **`PHASE1_PLUGIN_ABI_AUTHORING_SUGAR_FOUNDATION.md`** — adds
  `plugins/gte_plugin_abi/PluginExportsMacro.h` (the `extern "C"` hider, two
  macro flavors) and `plugins/gte_plugin_abi/SingleCapabilityPluginModule.h`
  (`MakeModuleInfo()`, `SingleCapabilityPluginModule<T>`, **plus this campaign's
  own confirmed follow-up** `ZeroCapabilityPluginModule` — see "Decisions made
  without `ask_questions`" below for why this extra class is in scope). Updates
  `plugins/gte_plugin_abi/PublicSurface.md`. Touches **zero** existing demo
  plugin — pure addition, verified by a standalone compile check, not by
  rebuilding any plugin `.dll`.
- **`PHASE2_RENDER_FEATURE_DEMO_PLUGINS_MIGRATION.md`** — migrates
  `plugins/demo_render_feature/RenderFeaturePlugin.cpp` and
  `plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` onto Phase 1's two
  new headers, exactly mirroring the proposal document's own before/after
  listing for `demo_render_feature` (and the same shape, names swapped, for
  `demo_render_feature_second`). Verified via `tools/ci/gte_plugin_isolation_probe`
  (fast, already has its own dedicated inner build folder — no full rebuild
  needed).
- **`PHASE3_EDITOR_PANEL_DEMO_PLUGIN_MIGRATION.md`** — migrates
  `plugins/demo_editor_panel/EditorPanelPlugin.cpp` onto the same two headers,
  exactly mirroring the proposal document's own before/after listing. Verified
  via a live, running `GreatTamanaEditor.exe` (`GET /list_tabs`, `GET
  /get_logs`).
- **`PHASE4_HELLO_WORLD_ZERO_CAPABILITY_SYMMETRY_MIGRATION.md`** — migrates
  `plugins/demo_hello_world/HelloWorldPlugin.cpp` onto
  `ZeroCapabilityPluginModule` + `GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE`, for
  full symmetry across all four demo plugins (the proposal document's own
  "What about `demo_hello_world`..." section flags this as a natural, small
  follow-up). Verified via `tools/ci/gte_plugin_abi_handshake_probe` (its own
  dedicated inner build folder) AND `tools/ci/gte_plugin_isolation_probe`.
- **`PHASE5_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`** — updates
  `docs/conventions/plugin-architecture.md` and `plugins/gte_plugin_abi/
  PublicSurface.md` (if Phase 1 left anything for this phase to finish),
  double-checks `AGENTS.md`'s "Plugin Architecture" section still reads true, runs
  the ONE full clean build + full `ctest` pass + live HTTP smoke test this whole
  campaign is allowed to run, confirms every one of the "four consumers" listed in
  Step 2 above still behaves identically, and writes
  `CAMPAIGN_COMPLETION_REPORT.md`.

### Decisions made without `ask_questions` (this planning pass had no live access to it — an implementer may still challenge any of these via `ask_questions` if it looks wrong once real code is in front of them)

1. **`ZeroCapabilityPluginModule` is IN SCOPE for this campaign, added inside
   `SingleCapabilityPluginModule.h`** (not a separate third file — the proposal
   only asked for two files, and this class is small enough that a third file
   would be over-engineering for one four-line class). Reasoning: the proposal
   document itself flags this exact gap ("What about `demo_hello_world` (zero
   capabilities)? ... a matching `ZeroCapabilityPluginModule` ... could be added
   the same way — a small follow-up, not required for this proposal to be useful
   on its own") and this campaign's own explicit goal is "make ALL FOUR of this
   repo's demo plugins prove the ergonomics win, not three of four" — leaving
   `demo_hello_world` on the old hand-written shape while the other three move to
   the new one would be an inconsistent, confusing end state for anyone reading
   `plugins/` afterward.
2. **The corrected `#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"`
   spelling** (not the proposal document's own literal
   `#include "GtePluginAbiFingerprintGenerated.h"` sketch) — see Step 2 above for
   the exact evidence. This is not a design choice, it is a straightforward bug
   in the proposal's own illustrative code that would fail to compile as
   literally written.
3. **`demo_hello_world` moves from the heap-allocated `GTE_DEFINE_PLUGIN_EXPORTS`
   flavor (today: `new`/`delete` per load/unload) to the static-instance
   `GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE` flavor**, matching the other three
   migrated demo plugins and matching the proposal document's own stated
   preference ("PREFERRED for the common case: it means this plugin never once
   calls `new`/`delete` for its own module object at all"). `demo_hello_world` has
   no per-instance construction logic today (a value-less `IPluginModule` that
   always returns `nullptr` from `QueryCapability`), so nothing is lost by this
   switch, and it makes all four demo plugins follow one single, consistent
   pattern.
4. **Phase grouping is by capability shape (render-feature pair, then editor
   panel, then hello-world), not by file-size** — the two render-feature plugins
   are near-byte-identical to each other, so migrating both together in one
   phase is the smallest reasonable unit; `demo_editor_panel` and
   `demo_hello_world` each get their own phase because they exercise genuinely
   different verification paths (a live running Editor vs. the two standalone
   CI probes).

### Campaign-wide rules every phase must follow ("Workflow Rule 1" – "Workflow Rule 10" below — later phase files may cite these exact numbers)

1. **No full build, no full regression test, for Phases 1–4.** Only a fast,
   targeted, incremental build/compile check (see each phase's own
   "Verification" section for the exact command) — this machine's full clean
   build and full `ctest` pass are slow and reserved for Phase 5 alone.
2. **Reuse the existing dedicated probe build folders instead of reconfiguring
   from scratch.** This repository already has `build-plugin-abi-handshake-probe/`
   and `build-plugin-isolation-probe/` sitting at the repo root, each wrapping
   its own nested `GTE_CORE_STANDALONE_PROBE_ONLY=ON` inner build
   (`gte_plugin_abi_inner_build/`, `gte_plugin_isolation_inner_build/`
   respectively) that already successfully configured and built once before.
   Both nested inner builds configure `-S` pointing at this SAME repository
   root, so any change under `plugins/`/`src/` is picked up by re-running
   `cmake --build <that inner build dir>` — a genuine Ninja incremental build,
   not a reconfigure, not a clean rebuild. Phases 2 and 4 use this instead of
   touching the main `build/` tree's own probe targets, to keep each phase's own
   verification fast and narrowly scoped. If either folder is missing/stale
   when a phase runs, re-create it exactly as its own `tools/ci/*/CMakeLists.txt`
   documents (the two `-S .. -B .. -G Ninja -DGTE_CORE_STANDALONE_PROBE_ONLY=ON
   ...` / `--build .. --target ..` command pairs, run directly rather than via
   the wrapper project, is the fastest path) — do not treat a missing folder as
   a blocker, just recreate it.
3. **Use the engine's own internal logging + `GET /get_logs` for anything
   inside `GreatTamanaEditor.exe`'s own run** (`AGENTS.md`'s "Logging"
   convention) — never `printf`/`std::cout`/`OutputDebugString` for anything new
   added under `src/`. The two standalone `tools/ci/*` probes are the one
   pre-existing exception (they already use `std::printf`/`std::fprintf`,
   matching established precedent) — this campaign does not add any new logging
   anywhere (it changes zero runtime logic), so this rule mostly just means:
   never introduce a new logging mechanism while migrating a plugin `.cpp`.
4. **Every phase writes its own `PHASEn_COMPLETION_REPORT.md`** in this same
   folder once its own verification passes, documenting exactly what changed,
   any real deviation from this phase's own plan, and the exact verification
   evidence gathered (command run + its real output/exit code, or the exact
   HTTP response body for a live check).
5. **`git_add` + `git_commit` at the end of every phase** — one commit per
   phase, message referencing the phase number and its one-line summary.
6. **If a phase hits a genuine design ambiguity not already resolved by this
   master strategy or its own phase file, use `ask_questions` before guessing.**
   This applies transitively to anything a phase itself delegates further.
7. **Implementation phases (1–5) must NOT call `delegate_task` themselves.**
   Only the double-check/orchestration step of this campaign (run separately,
   after all 5 phase files are scaffolded and reviewed) is allowed to use
   `delegate_task` to hand off each phase's real implementation. The one
   exception: Phase 5's own mandatory final full regression pass may invoke
   `delegate_task` ONLY if that regression pass surfaces a real, newly-broken,
   unexplained test failure that needs a dedicated fix — never for any other
   reason, and never by any phase other than Phase 5.
8. **Every phase's code must follow `AGENTS.md`'s existing coding guidelines**
   (Clean Architecture, RAII, `namespace gte`) and this repo's own established
   plugin-architecture conventions (`docs/conventions/plugin-architecture.md`,
   `plugins/gte_plugin_abi/PublicSurface.md`) — never invent a new pattern where
   the proposal document (or an existing demo plugin) already shows the exact
   shape to copy.
9. **Never change any observable behavior.** Every `GtePluginModuleInfo` string
   (`name`/`version`/`description`), every capability query string, every render
   pass name string, every panel name string, must read byte-identical before and
   after each phase's migration — this campaign is a pure internal refactor of
   HOW those exact same values get produced, never a change to WHAT they are.
   Diff each migrated `.cpp`'s literal string constants against the ORIGINAL
   file (read it first, before editing) to confirm this by hand before moving on.
10. **Run `git_status` at the very START of every phase** — confirm the branch
    still reads `feature/editor-core-separation` and the working tree is either
    clean or contains only the exact diff the immediately-prior phase already
    committed (never someone else's half-finished edit) — **and run it AGAIN
    immediately before that phase's own final commit**, to confirm the about-
    to-be-staged diff touches ONLY the files this phase's own plan says it may
    touch, and that any scratch/throwaway file created for a compile check
    (see e.g. Phase 1's own compile-check step) does not show up as an
    untracked leftover. This mirrors `editor-core-separation-3`/`-4`'s own
    established per-phase precedent (see e.g.
    `task_manager/editor-core-separation-3/PHASE1_COMPLETION_REPORT.md`'s own
    "confirmed via `git_status` before starting") and is the single cheapest
    guard against ever committing more, or less, than intended.

### What this campaign explicitly does NOT do (Non-Goals, restated from Step 1)

- No new plugin capability interface, no architecture change, no `PluginHost`
  change, no hot reload, no cross-process sandboxing, no toolchain switch, no
  fifth "multi-capability" demo plugin.

### Reference commands

- Main build tree (existing, already configured): `cmake --build build`
  (working directory `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`,
  Ninja/MinGW) — use this whenever a phase's own verification needs the real
  `GreatTamanaEditor.exe`/`build/plugins/*.dll` (Phases 3 and 5).
- Handshake probe's own dedicated inner build:
  `cmake --build build-plugin-abi-handshake-probe\gte_plugin_abi_inner_build`
  then run `build-plugin-abi-handshake-probe\gte_plugin_abi_inner_build\
  gte_plugin_abi_handshake_probe.exe` (Phase 4).
- Isolation probe's own dedicated inner build:
  `cmake --build build-plugin-isolation-probe\gte_plugin_isolation_inner_build`
  then run `build-plugin-isolation-probe\gte_plugin_isolation_inner_build\
  gte_plugin_isolation_probe.exe` (Phases 2 and 4).
- Full regression test (Phase 5 ONLY): `cd /d
  C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
  --output-on-failure`.
- Run the live Editor for HTTP-driven smoke checks:
  `run_app_background` on `build\GreatTamanaEditor.exe`, then `gte_send_request`
  against `http://127.0.0.1:8080` (default port) — `GET /get_logs?limit=50`,
  `GET /list_tabs`, `GET /get_swapchain`, `GET /get_game_view`, `POST
  /clear_logs`. Always `stop_app_background` the PID when finished.

### Every child phase file in this folder

1. `PHASE1_PLUGIN_ABI_AUTHORING_SUGAR_FOUNDATION.md`
2. `PHASE2_RENDER_FEATURE_DEMO_PLUGINS_MIGRATION.md`
3. `PHASE3_EDITOR_PANEL_DEMO_PLUGIN_MIGRATION.md`
4. `PHASE4_HELLO_WORLD_ZERO_CAPABILITY_SYMMETRY_MIGRATION.md`
5. `PHASE5_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`

Read this master file FIRST, then the source proposal document
(`PLUGIN_AUTHORING_ERGONOMICS_PROPOSAL_2026-09-24.md`), then the one phase file
you are working on, then (if it exists yet) the previous phase's own
`PHASEn_COMPLETION_REPORT.md` for continuity clues, before writing any code.
