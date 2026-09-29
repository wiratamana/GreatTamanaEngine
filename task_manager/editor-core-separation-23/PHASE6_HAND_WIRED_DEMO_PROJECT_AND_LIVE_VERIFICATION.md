# PHASE6 — Hand-wired demo Project Assembly render feature and full live HTTP verification

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first).
Campaign folder: `task_manager/editor-core-separation-23/`
Design doc citation: `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`,
Step 8's "Live, against a real running `GreatTamanaEditor.exe`" section (items
1-7) — re-read it in full before starting.
Previous phase report to read first: `PHASE5_COMPLETION_REPORT.md`.

## Step 1: The Goal (Where are we going?)

The single most important live proof in this whole campaign: a hand-written,
zero-shader, trivially-visual Project Assembly render feature, registered
through `Core::RegisterProjectRenderFeature()` (PHASE3), is genuinely,
pixel-confirmably visible in a real Editor Game View, survives a real
hot-reload cycle with no duplicate/dangling entry, and survives
`kMaxConcurrentProjectRenderFeatures + 4` register/unregister rename cycles
with BOUNDED GPU descriptor-set consumption. This phase writes real code (a
demo project's own `.cpp`) — it is not "verification only."

## Step 2: The Situation (Where are we now?)

`Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` is an EXISTING, already-
compiling, already-loadable Project Assembly `_Game.dll` used as the live
test rig by MULTIPLE prior campaigns
(`editor-core-separation-11`/`-13`/`-15`) — it already calls
`core.RegisterProjectRenderPassProvider("ProjectAssemblyProbe.FillTexture", ...)`
today (its own `RegisterProbeGame()` function, exported via
`GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterProbeGame)`). **Reuse this SAME
project rather than creating a brand-new one** — it already compiles, links,
and loads correctly against `GreatTamanaEditor`, which removes an entire
class of unrelated setup risk this phase does not need to re-solve. If, once
you start, you find a genuine reason a fresh, separate project is actually
required (e.g. this file's own existing content would need destructive
changes that risk breaking OTHER campaigns' own regression expectations
against it), `ask_questions` before deciding either way — do not silently
create a second demo project without confirming this reasoning first.

`GTE_DEFINE_PROJECT_EXPORTS_GAME` (`Projects/ProjectAssemblyProbe/Libraries/ProjectAssemblyExports.h`)
is this project's own ABI-export macro — re-read it before editing
`HelloGame.cpp`, to confirm exactly how `RegisterProbeGame()` is invoked and
whether anything about adding a SECOND registration call inside it needs
special care (ordering relative to the existing
`RegisterProjectRenderPassProvider()` call, for instance).

The relevant network endpoints, all already implemented and confirmed
present in `src/Network/NetworkServer.cpp`:
  - `POST /project_assembly/create_project` — NOT needed this phase (reusing
    an existing project).
  - `POST /project_assembly/debug/compile_only?name=<X>` (confirmed, real
    route, `NetworkServer.cpp` — NOT `GET`) / the compile-triggering debug
    capability (`IHotReloadDebugCapability::TriggerCompileOnly()`) — use this
    for a `compile_only` check before a full reload/relaunch.
  - `POST /project_assembly/hot_reload?name=<X>` — triggers a genuine unload +
    recompile + reload cycle for an already-loaded project (line ~1465,
    `NetworkServer.cpp`).
  - `GET /project_assembly/hot_reload/status` — poll this after triggering a
    reload; do not assume it completed instantly.
  - `GET /project_assembly/debug/ledger?name=<X>` (confirmed the real query
    parameter is `name`, not `projectName` — same `ParseProjectNameQuery()`
    helper every other `/project_assembly/*` route in `NetworkServer.cpp`
    shares; `BuildLedgerEntryResponseJson()` itself lives in
    `src/Network/NetworkRoutes.cpp`) — confirms PHASE4's `render_feature_names`
    list (snake_case — see PHASE4's own note on this exact JSON key).
  - `GET /render_graph` / `GET /render_graph/passes` — the live
    `RenderFeatureDebugEntry` snapshot (`DebugSnapshot()`, PHASE2).
  - `GET /get_game_view` (via `gte_send_request`) — the actual, rendered
    Game View frame — the ONE screenshot that matters most in this whole
    campaign.
  - `GET /get_logs?category=<X>&since_id=<N>` — the engine's own internal
    logging system; use this for every diagnostic need in this phase, never
    `printf`/`std::cout`.

## Step 3: The Plan (detailed strategy)

### 3.1 — Add the demo render feature to `HelloGame.cpp`

Inside `RegisterProbeGame(gte::Core& core)`, AFTER the existing
`core.RegisterProjectRenderPassProvider(...)` call, add a new call to
`core.RegisterProjectRenderFeature()` (PHASE3), using the design doc's own
Step 8.2 zero-shader technique — a bare clear-color declaration through the
raster entry point, empty `execute` body, zero hand-written Vulkan commands,
tagged `RenderPassEvent::AfterEverything` per PHASE5's own rule:

```cpp
core.RegisterProjectRenderFeature(
    "ProjectAssemblyProbe.ScreenTint",
    gte::RenderFeatureStage::PostComposite,
    gte::RenderFeatureBlendMode::AlphaOver,
    /*priority=*/0,
    [](gte::rg::RenderGraphBuilder& builder, gte::rg::TextureHandle privateTarget, VkExtent2D /*extent*/) {
        builder.AddRenderPass(
            "ProjectAssemblyProbe.ScreenTint.Clear", gte::rg::PassKind::Graphics,
            [privateTarget](gte::rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(privateTarget, std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.15f });
            },
            [](gte::rg::PassContext&) {},
            gte::rg::RenderPassDrawKind::DrawQuad, gte::rg::RenderPassEvent::AfterEverything);
    });
```

Before assuming the exact `AddRenderPass()` overload signature above is
correct, RE-CONFIRM it against `RenderGraphBuilder.h`'s own real, current
declaration (`search_in_dir` for `AddRenderPass(` in that header) — confirmed,
as of this phase file's own writing: the code sketch above uses the SHORTER
convenience overload, `AddRenderPass(name, kind, setup, execute, drawKind,
renderPassEvent, tags = 0)`, which DEFAULTS `ViewScope::Shared`/
`RenderPassCategory::General` internally (there is a SEPARATE, longer, 9-
parameter overload that takes `ViewScope`/`RenderPassCategory` EXPLICITLY as
its own 3rd/4th arguments instead — do not confuse the two; the sketch above
must keep using the shorter, defaulting one). Re-count the sketch's own
arguments against whichever overload is actually current before compiling —
do not guess. Also confirm `WriteColorAttachment()`'s own real signature
accepts a clear-color argument shaped this way (`std::array<float, 4>` vs.
some other type) — adjust to match reality, do not silently assume the design
doc's own sketch compiles byte-for-byte.

`#include <array>` if not already present in `HelloGame.cpp`.

Add a small `GTE_LOG_INFO` line confirming this registration succeeded/failed
(log the `bool` return value) — mirrors this file's own existing
"always log something observable" convention (its `RegisterProbeGame()`
opening line).

### 3.2 — Compile-only check first

Use the compile-only debug path (`IHotReloadDebugCapability::TriggerCompileOnly()`,
its corresponding HTTP route, or a direct `cmake --build build` targeting
just this project's own CMake target if that is simpler and faster) to
confirm the new `.cpp` compiles cleanly against a build that already contains
PHASE1-5's changes, BEFORE launching the full Editor. Fix any compile error
here first.

### 3.3 — Live verification sequence

Using `run_app_background` (launch `build\GreatTamanaEditor.exe`),
`gte_send_request`, and `stop_app_background` (always close it when done —
never leave a stray instance running):

1. Launch the Editor. Confirm `ProjectAssemblyProbe` is loaded (its own
   existing `GET`/log evidence from prior campaigns — e.g. the existing
   "GTE_RegisterProject called" log line — still fires).
2. `GET /render_graph` (or `/render_graph/passes`) — confirm
   `"ProjectAssemblyProbe.ScreenTint"` appears as a real entry (via
   `RenderFeatureDebugEntry`/`DebugSnapshot()`), stage `PostComposite`,
   blend mode `AlphaOver`, enabled, and — per PHASE2's own new
   `isProjectFeature` field (3.5 of that phase) — the JSON entry's own
   `"is_project_feature"` reads `true` and `"is_v3"` reads `false`. Also
   confirm, via `GET /activate_tab?name=Render Graph` (bringing that tab to
   the front first — a screenshot of whatever tab merely HAPPENS to be
   active is not a reliable check) followed by `GET /get_swapchain`, that
   the new `"[Project]"` tag renders next to this row exactly like `"[v3]"`
   renders next to a real `_v3` plugin row.
3. `GET /get_game_view` — capture a screenshot. Confirm a visible, subtle red
   tint is present across the whole Game View (a 0.15-alpha red overlay) —
   this is the single most important check in this entire campaign. Compare
   against a baseline captured with the feature unregistered/disabled if
   available, to make the difference unambiguous rather than relying on eyeballing alone.
4. Trigger `POST /project_assembly/hot_reload` for `ProjectAssemblyProbe`
   (no source change needed — a reload with unchanged source is still a
   genuine unload+recompile+reload cycle). Poll
   `GET /project_assembly/hot_reload/status` until it reports a completed
   outcome (`"Success"` — if `"CriticalFailure"`/`"RolledBack"`, fetch
   `GET /get_logs` and diagnose before proceeding, do not paper over it).
5. Re-check `GET /render_graph`/`GET /project_assembly/debug/ledger` —
   confirm `"ProjectAssemblyProbe.ScreenTint"` appears EXACTLY ONCE, both in
   the compositor's own snapshot AND in the ledger's `renderFeatureNames`
   list — never duplicated, never silently dropped. Re-check
   `GET /get_game_view` still shows the tint.
6. Two-features-blend-correctly proof: temporarily add a SECOND, differently-
   named/prioritized project render feature (e.g. a second tint, a different
   color/priority) to the same `RegisterProbeGame()` — this can be a small,
   throwaway addition kept only long enough to prove this one item, or (if
   simpler and equally valid) kept permanently alongside the first if it adds
   genuine, low-cost regression value; `ask_questions` if unsure which. Confirm
   via `GET /get_game_view` both effects visibly blend, in the declared
   priority order.
7. **Bounded-slot-reuse proof** (the design doc's Step 8, live item 7):
   repeat, against the SAME running Editor process, at least
   `kMaxConcurrentProjectRenderFeatures + 4` times (i.e. at least 20 times):
   unregister the PREVIOUS project render feature name, register a NEW,
   DIFFERENTLY-named one (mirrors a developer renaming their effect during
   real iteration) — since this requires new code running EACH iteration,
   the practical way to drive this without recompiling 20 times is to add a
   SEPARATE, temporary HTTP-reachable trigger (e.g. reuse
   `SetProbeHotReloadMarkerValueForTesting`'s own existing
   `EngineCommandBridge` pattern, `src/Application/EngineCommandDispatch.cpp`,
   to add ONE new, narrowly-scoped, main-thread-marshalled debug command that
   unregisters-then-registers-under-a-new-generated-name — mirroring that
   existing method's own "deliberately narrow, testing-only, cannot be
   repurposed" contract exactly) OR by looping
   `POST /project_assembly/hot_reload` with the demo `.cpp`'s own registered
   name edited differently before each trigger (slower, but requires zero
   new engine code) — `ask_questions` about which approach is preferred
   before building a new, permanent-feeling debug command for a one-time
   proof; a temporary, well-labeled, REMOVED-before-this-phase-ends helper
   may be the better trade-off. Confirm this never fails once the previous
   name is genuinely unregistered first, and confirm (via a debug build's
   own Vulkan validation layers, if enabled, or a lightweight, temporary
   instrumentation counter on `GpuResourceFactory`'s own allocation call
   site, removed afterward) that total compute descriptor sets consumed by
   this whole exercise stays bounded at
   `kMaxConcurrentProjectRenderFeatures x 2 (views) x 2 (private + blend)`,
   never growing further regardless of how many additional rename cycles ran.
8. `gte_plugin_abi`'s own existing `IRenderFeatureModule_v2`/`_v3` demo
   plugins (`DemoRenderFeaturePlugin_Clear`/`DemoRenderFeatureSecondPlugin_Clear`,
   confirmed to exist from `PHASE0_MASTER_STRATEGY.md` of
   `editor-core-separation-22` — re-check they still exist/still load) are
   confirmed, live, completely unaffected: still present in `GET /render_graph`,
   still visually correct in `GET /get_game_view`, same as before this
   campaign's changes.

### 3.4 — Ambiguity checkpoints

  - If reusing `ProjectAssemblyProbe` risks breaking another campaign's own
    existing regression expectations against that exact `.cpp` file (e.g. a
    test elsewhere asserts on its EXACT current render-pass count/shape),
    `ask_questions` before modifying it.
  - If the bounded-slot-reuse proof (3.3 item 7) genuinely cannot be driven
    without a new, permanent-feeling debug command, `ask_questions` about
    whether a smaller, explicitly temporary/removed-after-use command is
    acceptable, or whether a Tier-1 test (PHASE2's own slot-pool tests
    already partially cover the NON-live half of this exact claim) is
    sufficient supplementary evidence alongside a smaller live proof (e.g. 4-5
    real cycles instead of 20, if that is what is actually practical against
    a real running Editor).

### 3.5 — End of phase

1. Incremental build (`cmake --build build`) succeeds.
2. Every live verification item in 3.3 completed, with real evidence
   (screenshot bytes/sizes, JSON snippets, log excerpts) captured into the
   completion report — not merely "looked fine."
3. `stop_app_background` called — confirm no stray `GreatTamanaEditor.exe`
   process is left running.
4. Write `PHASE6_COMPLETION_REPORT.md`: the exact `HelloGame.cpp` diff, every
   live verification item's real evidence, and any temporary
   debug-command/instrumentation added-then-removed for the bounded-slot-
   reuse proof, explicitly confirmed removed before this report was written.
5. `git_add` + `git_commit` covering `HelloGame.cpp` and the report (and any
   OTHER permanent code change made during this phase, if the second-feature
   addition from 3.3 item 6 was kept permanently per that item's own
   resolution).
