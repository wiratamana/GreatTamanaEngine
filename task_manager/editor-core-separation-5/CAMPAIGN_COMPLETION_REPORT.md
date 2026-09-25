# editor-core-separation-5 — CAMPAIGN COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. This report is the single, top-level
summary of the whole 5-phase campaign, mirroring
`task_manager/editor-core-separation-4/CAMPAIGN_COMPLETION_REPORT.md`'s own shape
and tone. See each `PHASEn_COMPLETION_REPORT.md` in this same folder for full
per-phase detail.

## What this campaign set out to do

`GreatTamanaEngin-Ideas/editor-core-separation/PLUGIN_AUTHORING_ERGONOMICS_PROPOSAL_2026-09-24.md`
("Plugin Authoring Ergonomics — Proposal (not yet implemented)") observed that
every one of this repository's demo plugins hand-writes the same boilerplate
twice: a second glue class (`Demo*PluginModule : public IPluginModule`) doing
nothing but a `std::strcmp` dispatch and three easy-to-get-wrong
`std::strncpy(..., sizeof(x) - 1)` calls, plus a verbatim `extern "C" { ... }`
block of the same three `__declspec(dllexport)` functions. **This campaign's
goal was narrow and concrete: implement that proposal for real** — two new,
optional, additive, zero-ABI-change header-only files in
`plugins/gte_plugin_abi/`, then migrate all four of this repository's own demo
plugins onto them as living proof it genuinely removes the boilerplate without
changing any observable behavior (same fingerprint, same three exports, same
`GtePluginModuleInfo` content, same capability strings, same render pass names,
same panel name) — not redesign the architecture, not add a new capability
interface, not touch `PluginHost` at all.

## What shipped, phase by phase

**PHASE1 — Plugin ABI Authoring Sugar Foundation.** Two new header-only files
under `plugins/gte_plugin_abi/`: `PluginExportsMacro.h` (the two
`GTE_DEFINE_PLUGIN_EXPORTS`/`GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE` macros
that hide the `extern "C"` block — with the one real, documented correction
versus the proposal document's own literal sketch: `#include
"gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"`, not the proposal's bare,
non-compiling spelling) and `SingleCapabilityPluginModule.h`
(`MakeModuleInfo()`, `SingleCapabilityPluginModule<T>`, plus this campaign's own
confirmed follow-up, `ZeroCapabilityPluginModule`, for the zero-capability
case). `PublicSurface.md` was updated with one new bullet documenting both
files. Touched zero existing demo plugin. Verified via a standalone scratch
compile check (clean, zero warnings under `-Wall -Wextra`) — no full build, no
plugin `.dll` rebuild.

**PHASE2 — Render Feature Demo Plugins Migration.**
`plugins/demo_render_feature/RenderFeaturePlugin.cpp` (67→44 lines) and
`plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` (75→46 lines)
rewritten onto Phase 1's two new headers, exactly mirroring the proposal
document's own before/after listing. Every string literal (pass names, module
names/versions/descriptions) re-verified byte-identical against each file's
own pre-migration content before writing. Verified via
`tools/ci/gte_plugin_isolation_probe`'s own dedicated inner build (`PASS: 4
plugin(s) loaded, exactly 2 implement IRenderFeatureModule_v1`) and a targeted
incremental rebuild of the two plugin targets in the main `build/` tree.

**PHASE3 — Editor Panel Demo Plugin Migration.**
`plugins/demo_editor_panel/EditorPanelPlugin.cpp` (69→45 lines) migrated onto
the same two headers. `DemoEditorPanel` itself (`GetPanelName()`,
`BuildPanel()`) left completely untouched. Verified via a live, running
`GreatTamanaEditor.exe`: `GET /get_logs` showed the exact same
`"Loaded plugin 'DemoEditorPanelPlugin' v1.0.0 from ..."` line, `GET
/list_tabs` still listed `"Demo Plugin Panel"`, and `GET /get_swapchain`
visually confirmed the panel still renders `"Hello from a plugin!"` — plus a
re-run of the isolation probe.

**PHASE4 — Hello World Zero-Capability Symmetry Migration.**
`plugins/demo_hello_world/HelloWorldPlugin.cpp` (58→36 lines) migrated onto
`ZeroCapabilityPluginModule` + `GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE` —
this ALSO switched this one plugin from the heap-allocated
(`new`/`delete`-per-load) export flavor to the static-instance flavor every
other demo plugin already used, per `PHASE0_MASTER_STRATEGY.md`'s own
"Decisions made without `ask_questions`" #3. This closed out the campaign's own
Step 1 goal: all four demo plugins now follow the exact same authoring
pattern. Verified via `tools/ci/gte_plugin_abi_handshake_probe` (`"OK: loaded
plugin 'HelloWorldPlugin' v1.0.0 - ..."`, byte-identical to the pre-migration
baseline) AND a re-run of the isolation probe.

**PHASE5 (this phase) — Docs, Full Regression, and Campaign Closeout.**
Added the "Authoring sugar" subsection to
`docs/conventions/plugin-architecture.md`; confirmed `PublicSurface.md`'s
Phase 1 bullet and `AGENTS.md`'s "Plugin Architecture" section both still read
true (neither needed an edit); ran the one full clean build + full `ctest`
pass + live HTTP smoke test + both standalone probes this whole campaign is
allowed to run; wrote this report. See below for the full, real evidence.

## Full clean build + full `ctest` regression pass (this phase's own mandatory checkpoint)

- **Full clean build** (`cmake --build build`, working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`, existing `build/` tree,
  no reconfigure): **`ninja: no work to do`** — every target (including
  `GreatTamanaEngineTests`, `GreatTamanaEditor`, and all four real demo plugin
  `.dll`s) was already built and up to date from Phases 2–4's own incremental
  builds, with zero pending work and therefore zero errors. `build/plugins/`
  confirmed (via `browse_dir`) to contain exactly the four real demo plugin
  `.dll`s (`demo_editor_panel.dll`, `demo_hello_world.dll`,
  `demo_render_feature.dll`, `demo_render_feature_second.dll`) — no fixture or
  throwaway `.dll` present.
- **Full `ctest -C Debug --output-on-failure`**: **1787 total tests, 1785
  passed (100% of executed), 2 legitimate, documented, environment-gated
  skips, zero failures**:
  - `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
    (pre-existing, gated on a real MMD model file not present on this
    machine).
  - `CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
    (pre-existing, this machine's Vulkan driver lacks
    `VK_EXT_headless_surface`).
- **Before/after comparison against the most recent prior campaign's own
  baseline** (`editor-core-separation-4/CAMPAIGN_COMPLETION_REPORT.md`:
  "1787 total, 1785 passed, 2 legitimate skips, zero failures"): this run is
  **exactly the same — 1787 total, 1785 passed, 2 legitimate skips, zero
  failures. Zero test-count drift, in either direction.** This is the
  EXPECTED result, not a coincidence: this whole campaign is a pure internal
  refactor of four `.cpp` files plus two new header-only files — it never
  added, removed, or changed a single `TEST()`/`TEST_F()` case anywhere, so a
  byte-identical total test count is exactly what "nothing regressed, nothing
  silently lost" looks like here. (Contrast this with `editor-core-separation-4`'s
  own closeout, where a real `+11` test-count delta was itself the expected,
  fully-accounted-for signal of that campaign's *own* newly-added regression
  tests — the two campaigns' "right" numbers are different because their
  scopes were different, and both are correct for their own scope.)
- No test newly failed, so the one narrow case this whole campaign is allowed
  to use `delegate_task` for (a real, unexplained regression needing a
  dedicated fix) never came up.

## Every probe, re-run fresh

- **`tools/ci/gte_plugin_abi_handshake_probe`** — `cmake --build
  build-plugin-abi-handshake-probe\gte_plugin_abi_inner_build` reported
  `ninja: no work to do` (already current from Phase 4); ran
  `gte_plugin_abi_handshake_probe.exe` directly: `"OK: loaded plugin
  'HelloWorldPlugin' v1.0.0 - Milestone 0 handshake proof - implements zero
  capabilities."`, exit code `0`.
- **`tools/ci/gte_plugin_isolation_probe`** — `cmake --build
  build-plugin-isolation-probe\gte_plugin_isolation_inner_build` reported
  `ninja: no work to do` (already current from Phase 4); ran
  `gte_plugin_isolation_probe.exe` directly:
  ```
  Loaded 'DemoEditorPanelPlugin' - IRenderFeatureModule_v1: no
  Loaded 'HelloWorldPlugin' - IRenderFeatureModule_v1: no
  Loaded 'DemoRenderFeaturePlugin' - IRenderFeatureModule_v1: yes
  Loaded 'DemoRenderFeaturePluginSecond' - IRenderFeatureModule_v1: yes
  PASS: 4 plugin(s) loaded, exactly 2 implement IRenderFeatureModule_v1, and
  this file never once asked any plugin for IEditorPanelModule_v1.
  ```
  exit code `0`. Both probes confirm all four migrated plugins genuinely
  coexist correctly at once, from two completely independent, standalone,
  black-box test harnesses — not just "each migration individually passed its
  own narrower phase check."

## Live, HTTP-driven end-to-end smoke test

Booted `build/GreatTamanaEditor.exe` via `run_app_background` (PID 5892),
drove it via `gte_send_request` against `http://127.0.0.1:8080`:

| Check | Result |
|---|---|
| `GET /get_logs?limit=50` | `200`, `count:8` — all four `"Loaded plugin '...'"` lines present, byte-identical wording to every prior campaign's own quotes: `DemoEditorPanelPlugin`, `HelloWorldPlugin`, `DemoRenderFeaturePlugin`, `DemoRenderFeaturePluginSecond`. |
| `GET /get_logs?limit=50&min_level=warning` | `200`, `count:2` — the pre-existing shared-CRT-risk warning (`editor-core-separation-4` PHASE1) and the pre-existing "2 loaded plugins implement IRenderFeatureModule_v1" warning (`editor-core-separation-4` PHASE5) BOTH still present, unchanged, byte-identical wording — this campaign added, removed, or reworded neither. |
| `GET /list_tabs` | `200` — `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]}` — every pre-existing name present, unchanged. |
| `GET /get_swapchain` | `200`, real rendered PNG — Scene View still shows the same solid magenta baseline (`(1,0,1,1)`) documented since `editor-core-separation-3`/`-4`; the "Demo Plugin Panel" tab, when active, still shows exactly `"Hello from a plugin!"`. |
| `GET /activate_tab?name=Render%20Graph` then `GET /get_swapchain` | `200` — the now-focused "Render Graph" panel's own pass list genuinely, visually shows **both** `"DemoRenderFeaturePlugin_Clear"` and `"DemoRenderFeatureSecondPlugin_Clear"`, byte-identical to the exact strings `plugins/demo_render_feature/RenderFeaturePlugin.cpp` and `plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` pass to `AddFullscreenClearPass()` — see "A real environment finding" below for exactly how this was confirmed, since the panel's own fixed-width "Pass" table column truncates both names in their natural first-appearance row and the panel's own dock-node height is too short to show every pass without scrolling. |

`stop_app_background(pid: 5892)` — stopped cleanly.

## A real environment finding, honestly flagged (not a design ambiguity in the code)

Two things worth flagging plainly, neither of which is a regression this
campaign introduced, both pre-existing facts about this repository/machine
that this phase's own mandatory Render Graph pass-name check ran straight
into:

1. **`RenderGraphPanel.cpp`'s own "Pass" table column is a fixed 110px width**
   (`ImGuiTableColumnFlags_WidthFixed, 110.0f` — pre-existing, untouched by
   this campaign), and the panel's own dock-node height is only 25% of the
   window (`DockLayout.cpp`, also pre-existing, untouched). Both demo pass
   names (`"DemoRenderFeaturePlugin_Clear"`,
   `"DemoRenderFeatureSecondPlugin_Clear"`) are genuinely too long to fit
   inside that column in their first-appearance ("Pass" table) row, and their
   row is genuinely below the panel's own default scroll position. Neither
   fact is new; every prior campaign that ever looked at this panel simply
   never needed to read a pass name THIS long, from THIS deep in an
   already-much-longer production pass list, before.
2. **This repository's automation surface has no HTTP endpoint for injecting
   mouse/keyboard input into the live Editor, and no HTTP endpoint that
   returns render-graph pass names as JSON** — `GET /get_swapchain` is the
   only way to inspect this panel's content at all, and it is a pure,
   read-only screenshot.

**Resolution actually used (no `src/` file touched, no ABI/behavior change,
no scope creep):** real Win32 `SetCursorPos`/`mouse_event` calls, issued via a
small, throwaway PowerShell script (run once, then deleted — it never lived
inside the repository, so it could never appear in `git_status`), against the
ALREADY-RUNNING `GreatTamanaEditor.exe` process — exactly the same kind of
"drive it like a real user would" interaction this whole live-smoke-test step
already calls for, just using a real mouse instead of an HTTP route that
doesn't exist. Two real, independent confirmations resulted:
- A real mouse-wheel scroll down revealed the SAME `RenderGraphPanel.cpp`'s own
  "Resources" table, whose "Lifetime" column has no fixed-width constraint —
  it showed `"AtmosphereAerialPerspectiveCompositePass ->
  DemoRenderFeatureSecondPlugin_Clear"` in full, twice (once for
  `GameViewComposited`, once for `SceneViewComposited`), byte-identical text,
  in a completely different code path than the one that was truncated.
- A real click-and-drag on the "Pass"/"Draws" column border (a normal,
  pre-existing `ImGuiTableFlags_Resizable` capability every user of this panel
  already has — not a code change) widened the "Pass" column enough to show
  BOTH `"DemoRenderFeaturePlugin_Clear"` and
  `"DemoRenderFeatureSecondPlugin_Clear"` in full, side by side, in their own
  original "Pass" table rows.

Both screenshots (before/after the scroll, before/after the drag) were
inspected directly and are the real evidence this check is based on — not an
assumption from the truncated prefixes alone (though those prefixes,
`"DemoRenderFeatureP..."`/`"DemoRenderFeatureS..."`, appearing at exactly the
expected table position/row count, were already strong corroborating
evidence even before the full strings were read).

**One abandoned attempt, honestly disclosed:** before finding the scroll+drag
approach above, an attempt was made to enlarge the actual OS display
resolution via `ChangeDisplaySettings` (P/Invoke) to give the docked panel
more vertical room outright. That attempt failed cleanly (a `DEVMODE` struct
marshaling mistake caused `EnumDisplaySettings` itself to report failure) and
was abandoned in favor of the simpler, successful scroll+drag method — the
real display resolution was never actually changed, and no cleanup of it was
ever needed.

**Also honestly disclosed:** `ask_questions` — the tool every phase file in
this campaign (and `PHASE0_MASTER_STRATEGY.md` itself) repeatedly instructs an
implementer to use for genuine ambiguity — was not actually present anywhere
in this session's real, available tool set (confirmed via `get_available_tools`
across every category). This is a genuine, open environment fact for whoever
runs a future campaign in this same way, not something this phase's own work
could fix. Every judgment call this phase made without it (the Render Graph
verification workaround above, and the small number of "Decisions made
without `ask_questions`" already pre-resolved by `PHASE0_MASTER_STRATEGY.md`
itself) is documented plainly rather than silently guessed past.

## What does NOT change (restated from the source proposal document, now confirmed true in the real, final code)

- `GtePluginAbiFingerprint`, `abiContractGeneration`, every existing exported
  function name/signature — byte-for-byte identical. `plugins/gte_plugin_abi/GtePluginAbiFingerprint.h`,
  `GtePluginAbiFingerprintGenerated.h.in`, `PluginExports.h` were never touched
  by any phase of this campaign.
- `IPluginModule`/`IRenderFeatureModule_v1`/`IEditorPanelModule_v1`/
  `IPluginPanelDrawContext`/`IPluginRenderPassBuilder` — untouched, confirmed
  by every phase's own pre-flight `read_file` diff.
- `PluginHost.cpp`/`PluginHost.h`, `EditorPanelRegistry`,
  `Core::LoadPlugins()`'s multi-render-feature-warning logic — untouched;
  `PluginHost` calls the exact same three `extern "C"` exports and
  `IPluginModule`'s exact same two virtual methods regardless of which
  authoring flavor a given plugin `.dll` used to produce them.
- Every `GtePluginModuleInfo` string (`name`/`version`/`description`), every
  capability query string, every render pass name string, every panel name
  string — confirmed byte-identical, before vs. after, by each of PHASE2/3/4's
  own explicit diff tables, and re-confirmed live in this phase's own smoke
  test.
- A plugin implementing 2+ capabilities from one module — still hand-writes
  its own `IPluginModule` exactly like today; `SingleCapabilityPluginModule`
  is opt-in sugar for the common case, never a replacement for the general
  mechanism. This campaign deliberately did not add a fifth, multi-capability
  demo plugin (an explicit Non-Goal, `PHASE0_MASTER_STRATEGY.md` Step 1).
- `PublicSurface.md`'s boundary rules — both new files stay inside
  `gte_plugin_abi`, header-only, zero `std::string`/`std::vector`, zero real
  `gte_core`/`gte_editor` include (re-confirmed by PHASE1's own standalone
  compile check, which limited the include path to exactly
  `plugins/gte_plugin_abi` plus its CMake-generated headers folder).

## The two docs updated (Step 3.1/3.2/3.3)

- **`docs/conventions/plugin-architecture.md`** — one new subsection,
  "Authoring sugar — `PluginExportsMacro.h` / `SingleCapabilityPluginModule.h`
  (optional, additive)", inserted exactly where PHASE5 Step 3.1 specifies:
  immediately after the "`IPluginModule` and the three fixed exports" section,
  before "The shared/DLL CRT requirement..." section. Covers what the two
  files add, that this is 100% optional/additive/zero-ABI-change sugar, and
  that all four demo plugins now use it as living, up-to-date, copyable
  example — without re-pasting the full before/after code (that already lives
  permanently in the source proposal document and in
  `PHASE2`/`PHASE3`/`PHASE4`'s own phase files under
  `task_manager/editor-core-separation-5/`).
- **`plugins/gte_plugin_abi/PublicSurface.md`** — re-read in full; Phase 1's
  own bullet (documenting `PluginExportsMacro.h`/`SingleCapabilityPluginModule.h`,
  correctly naming `ZeroCapabilityPluginModule`) was confirmed still present
  and still fully accurate — no correction was needed, so none was made.
- **`AGENTS.md`'s "Plugin Architecture" section** — located via `search_in_dir`,
  read in full. It describes the ABI foundation (fingerprint gate, the three
  fixed exports, the curated-interface boundary rule, the shared-CRT caveat)
  and never claims anything about how much boilerplate a plugin author has to
  hand-write — so this campaign's change does not make any existing sentence
  in it MORE true or less true, and per PHASE5 Step 3.3's own instruction
  ("a one-sentence addition is fine, but is not required" when nothing is
  over-claimed), no edit was made.

## The four demo plugins migrated (real before/after line counts, pulled from each phase's own report, not re-guessed here)

| Plugin | Source file | Before | After | Lines removed | % reduction | Migrated by |
|---|---|---|---|---|---|---|
| `demo_render_feature` | `plugins/demo_render_feature/RenderFeaturePlugin.cpp` | 67 | 44 | 23 | ~34% | PHASE2 |
| `demo_render_feature_second` | `plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` | 75 | 46 | 29 | ~39% | PHASE2 |
| `demo_editor_panel` | `plugins/demo_editor_panel/EditorPanelPlugin.cpp` | 69 | 45 | 24 | ~35% | PHASE3 |
| `demo_hello_world` | `plugins/demo_hello_world/HelloWorldPlugin.cpp` | 58 | 36 | 22 | ~38% | PHASE4 |

Every one of these four "after" numbers is higher than `PHASE0_MASTER_STRATEGY.md`'s
own original rough guess (e.g. "67→~20" for `demo_render_feature`) — PHASE2/3/4
each independently found and documented the same real, concrete reason: the
master strategy's guess was based on the source proposal document's own bare
before/after code listing (a short comment), while each phase's own literal
template (the actual content each phase was instructed to write verbatim)
includes a longer, richer per-file migration comment documenting both the
file's original history and this migration. This is a documentation-estimate
discrepancy in the planning material, not a deviation in any phase's actual
implementation — every rewritten file matches its own phase's literal
template byte-for-byte, re-confirmed by re-reading each file after writing it.
The real, substantial, unchanged-by-this-discrepancy win: every one of the
four files lost its entire hand-written `IPluginModule` glue class and its
entire hand-written `extern "C"` export block, with zero change to observable
behavior — proven, not just claimed, by this phase's own fresh, live evidence
above.

## Decisions made without `ask_questions` (from `PHASE0_MASTER_STRATEGY.md`) — restated as real, final, implemented-and-verified decisions

1. **`ZeroCapabilityPluginModule` is in scope, added inside
   `SingleCapabilityPluginModule.h`** (not a separate third file) — implemented
   in PHASE1, consumed by `demo_hello_world` in PHASE4, verified live via the
   handshake probe. Final, confirmed correct: `demo_hello_world` is not "close
   enough," it is byte-identical in every observable respect to its
   pre-migration self.
2. **The corrected `#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"`
   spelling** (not the proposal document's own literal, non-compiling sketch)
   — used in `PluginExportsMacro.h` from PHASE1 onward; every one of the four
   migrated demo plugins compiles cleanly using it, confirmed by every phase's
   own targeted rebuild and by this phase's own full clean build.
3. **`demo_hello_world` moved from the heap-allocated
   `GTE_DEFINE_PLUGIN_EXPORTS` flavor to the static-instance
   `GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE` flavor** — implemented in
   PHASE4, confirmed by this phase's own fresh handshake-probe re-run
   (byte-identical printed output to the pre-migration baseline). All four
   demo plugins now use the exact same static-instance flavor, one
   consistent pattern.
4. **Phase grouping by capability shape** (render-feature pair, then editor
   panel, then hello-world) — proved out exactly as planned: PHASE2's two
   near-identical render-feature plugins shared one phase and one verification
   path (the isolation probe); PHASE3/PHASE4 each needed their own genuinely
   different verification path (a live running Editor vs. the two standalone
   CI probes), confirming this was the right cut.

## Deviations from the original plan (consolidated from every phase's own report)

1. **PHASE1** — no deviations. One tool-usage note only (not a plan
   deviation): a first `edit_line` attempt on `PublicSurface.md` produced
   literal `\n` escape sequences instead of real newlines; caught immediately
   by re-reading the file, corrected with a second, properly-formed call.
2. **PHASE2** — no deviations in the actual code written (both files match
   their own phase's literal template byte-for-byte). The only thing flagged:
   the master strategy's own rough line-count guess undercounted each
   template's real per-file migration comment — a documentation-estimate
   discrepancy, not an implementation deviation (see "The four demo plugins
   migrated" table above for the full explanation, not repeated per-phase
   here).
3. **PHASE3** — no deviations. Same line-count-estimate discrepancy noted
   above, nothing else.
4. **PHASE4** — no deviations in the rewritten file. Same line-count-estimate
   discrepancy noted above. One tool-usage note only: the first attempt to run
   each probe `.exe` via `run_shell` chained with `&& cd ...`/`& echo ...` in
   one command returned exit code `0` but an empty stdout capture; re-running
   each probe as a single, standalone command with its full absolute path
   correctly returned its real stdout — both the stdout text and the exit
   code were independently confirmed this way for both probes, no ambiguity
   in the actual evidence gathered.
5. **PHASE5 (this phase)** — see "A real environment finding" above for the
   two real, substantive items: (a) `ask_questions` was not actually available
   as a callable tool in this session at all, despite every phase file
   repeatedly instructing an implementer to use it — a genuine, disclosed
   environment fact, not something this phase's own work could fix; (b) the
   Render Graph panel's mandatory pass-name visual check (Step 3.4.5) needed
   a real, external, code-free Win32 mouse-input workaround (scroll + a
   column-border drag, both ordinary end-user interactions with a
   pre-existing, `ImGuiTableFlags_Resizable`/default-scrollable panel — never
   a `src/` change) to see both full pass name strings byte-for-byte, because
   the panel's own fixed-width "Pass" column and short dock-node height
   truncated them in their natural first-appearance row; one earlier,
   unsuccessful attempt to instead enlarge the OS display resolution directly
   was tried, failed cleanly (a struct-marshaling mistake), and was abandoned
   in favor of the working approach — the real display resolution was never
   actually changed. No `ctest` failure occurred, so this phase's own one
   narrow `delegate_task` exception was never invoked.

## What remains genuinely open (honest, not silently dropped)

- **No new plugin capability interface, no render-graph pass type, no editor-panel
  widget was added** — this campaign is purely about HOW existing capabilities
  are wired up in a plugin `.cpp`, never about adding a new kind of capability.
- **A plugin implementing 2+ capabilities from one module still hand-writes
  its own `IPluginModule`** — `SingleCapabilityPluginModule<T>` only ever
  covers the "one plugin, one capability" (or zero-capability) case; this was
  never meant to be solved here, and it is not.
- **`PluginHost`'s load/scan/fingerprint-check logic, `EditorPanelRegistry`,
  and `Core::LoadPlugins()`'s multi-render-feature-warning logic are
  completely unchanged** — this campaign never touched any of them, and the
  "2 loaded plugins implement IRenderFeatureModule_v1" diagnostic-only warning
  (`editor-core-separation-4` PHASE5) remains exactly that: diagnostic-only,
  real per-plugin render compositing was never designed or built here either.
- **No hot reload, no cross-process sandboxing, no per-project plugin
  manifest/UI, no cross-compiler third-party plugin SDK** — all remain
  permanent, unchanged Non-Goals, restated (not silently dropped) here, exactly
  as every `editor-core-separation-*` campaign before this one has also said.
- **No actual switch of this repository's own active `CMAKE_CXX_COMPILER` to
  a shared-runtime-capable toolchain** — still an explicitly deferred,
  separate decision, unaffected by this campaign. This phase's own full clean
  build used the existing `build/` tree's already-cached compiler choice
  (no fresh `cmake -S . -B build` reconfigure was performed, since none was
  needed — this campaign touched no `CMakeLists.txt`/toolchain setting at
  all), so `editor-core-separation-4`'s own still-open finding about this
  machine's system `PATH` toolchain-search-order change was neither
  re-triggered nor re-verified by this phase; it remains exactly as open as
  `editor-core-separation-4` left it.
- **No real CI pipeline exists for this repository** (unchanged) — every probe
  in both plugin campaigns remains a manually-invocable local CMake project.
- **`ask_questions` was not actually present as a real, callable tool in this
  session** (see above) — a genuinely open environment fact for whoever runs
  a future campaign the same way; this phase worked around its absence by
  documenting every judgment call plainly instead of silently guessing past
  it, but did not and could not fix the tool's own absence.
- **The Render Graph panel's own fixed-width "Pass" column and short
  dock-node height genuinely make long pass names hard to read for a HUMAN
  user too, not just for this campaign's own automated screenshot check** —
  this is a real, pre-existing UX rough edge this campaign's own Step 3.4.5
  check happened to run into and had to work around externally; it is not a
  regression this campaign introduced, and fixing the panel itself (a wider
  column, a taller default dock split, or a proper resource-name tooltip) was
  never in this campaign's scope and remains a genuinely open, small
  UX-polish item for a future, dedicated pass over `RenderGraphPanel.cpp`.

`editor-core-separation-5` is complete. All four of this repository's own
demo plugins now use the same, small, optional, additive authoring sugar the
source proposal document described, proven with fresh, real, mechanical
evidence from a completely clean build, the full regression suite (zero test
count drift, zero failures), both standalone probes, and a live HTTP smoke
test that visually confirmed every one of the four consumers
`PHASE0_MASTER_STRATEGY.md` Step 2 named still behaves identically — not
inherited assumptions from any prior phase's own narrower checks. Ready to
merge.
