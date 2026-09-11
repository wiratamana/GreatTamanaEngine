# PHASE0_MASTER_STRATEGY — `network-impl-7`: HTTP `activate_tab` / `list_tabs`

Orchestrator document for this campaign. Every child phase document below
MUST be read in order; each one assumes everything the previous phase
already built exists and compiles.

## Step 1: The Goal (Where are we going?)

Give an external LLM/AI agent (or any HTTP client) the ability to bring a
specific named Editor panel/tab to the front over the engine's existing
embedded loopback HTTP server — e.g. `GET /activate_tab?name=Profiler` makes
the "Profiler" tab the active/focused tab, exactly as if a human had clicked
that tab in the Editor's docked UI. A companion `GET /list_tabs` endpoint
reports which tab names currently exist, so a caller never has to guess or
hardcode the engine's internal panel-naming convention.

This is a small, additive feature: no existing endpoint's behavior changes,
no existing file's public contract changes except by strictly ADDING new
members and one new virtual method to an existing interface (`IEditorLayer`,
implemented by both existing implementations).

## Step 2: The Situation / The Problem (Where are we now?)

- The engine already has a working embedded HTTP server
  (`src/Network/NetworkServer.h/.cpp`, `src/Network/NetworkRoutes.h/.cpp`),
  loopback-only, auto-started by `Application`, gated by `GTE_ENABLE_NETWORK`
  (see `AGENTS.md`, "Networking"). It already has several precedents for
  "a network route handler needs the MAIN thread to do something and hand a
  result back":
  - `FrameCaptureBridge` (`src/Application/FrameCaptureBridge.h/.cpp`) — a
    READ-ONLY bridge: a route handler asks for pixels, the main thread
    produces them.
  - `EngineCommandBridge` (`src/Application/EngineCommandBridge.h/.cpp`) — an
    ECS-MUTATING bridge: a route handler asks to spawn/delete/transform an
    entity, the main thread does it via `Game::`.
  - Both are single-purpose, reviewed, narrow bridges. AGENTS.md's own rule
    (see "Networking" section, `FrameCaptureBridge` bullet) is explicit: **a
    future endpoint needing DIFFERENT engine data must build its own new,
    similarly narrow bridge rather than repurpose an existing one for an
    unrelated purpose.**
- There is currently **no** bridge for "make the EDITOR UI itself do
  something" (as opposed to "read pixels" or "mutate the ECS world"). Which
  tab is currently focused/active is Dear ImGui docking state, owned
  entirely by `ImGuiEditorLayer` (`src/Editor/ImGuiEditorLayer.cpp`) and only
  ever touched from the main thread, inside `Application::Run()`'s frame
  loop, between `IEditorLayer::NewFrame()` and `IEditorLayer::BuildUI()`. A
  network route handler runs on `NetworkServer`'s own dedicated background
  thread and must NEVER touch ImGui/`IEditorLayer` directly (see AGENTS.md,
  "Networking": "a route handler must be a pure function of its own request
  data only ... must NEVER touch ... IEditorLayer/ImGui/any other engine
  subsystem, directly or indirectly, full stop").
- The engine's default dock layout (`src/Editor/DockLayout.cpp`,
  `BuildDefaultDockLayout()`) already has a single, canonical, hand-written
  list of every panel name that exists: `"Hierarchy"`, `"Inspector"`,
  `"Scene"`, `"Game"`, `"Memory"`, `"Profiler"`, `"Render Graph"`, `"Jobs"`,
  `"Atmosphere"`, and (only when `GTE_ENABLE_PROJECT_PANEL` is ON)
  `"Project"`. Today this list (`kAllPanelNames`) is a private, anonymous-
  namespace array local to `DockLayout.cpp` — nothing outside that file can
  see it, and nothing outside `src/Editor/` (compiled only when
  `GTE_ENABLE_EDITOR` is ON) can even try.
- Per the locked design decisions from this campaign's own requirements
  discussion (see below), `activate_tab` is restricted to this SAME known,
  fixed panel catalog (never an arbitrary/unexpected ImGui window name), is
  a `GET` endpoint, and ships with a discovery companion, `GET /list_tabs`.

### Locked Design Decisions (from the requirements discussion for this campaign)

1. **Output location**: every strategy `.md` file for this campaign lives in
   `task_manager/network-impl-7/` (this folder), the next free slot in the
   existing `network-impl-N` campaign series — NOT
   `task_manager/atmosphere-scattering-3/` (already fully used by a finished,
   unrelated campaign).
2. **HTTP method**: `GET /activate_tab?name=<PanelName>` (not `POST`) — this
   changes Editor UI focus state only, never gameplay/ECS data, and a `GET`
   is simplest to trigger ad hoc (a browser address bar, `curl`, an LLM tool
   call with no body to construct).
3. **Discoverability**: ship a companion `GET /list_tabs` endpoint (mirrors
   the existing `GET /list_textures` precedent) so a caller can enumerate
   valid `name` values instead of guessing/hardcoding them.
4. **Name space**: `activate_tab`/`list_tabs` only ever know about the
   engine's own fixed, compiled-in panel catalog (see above) — never an
   arbitrary ImGui window name. An unrecognized name is a clean, actionable
   `404`, never a lookup into arbitrary/internal ImGui state.
5. **Cross-thread bridge**: build a brand-new, small, dedicated bridge,
   `EditorUiCommandBridge` (mirrors `EngineCommandBridge`'s exact shape) —
   do NOT add a new `EngineCommandKind` to the existing ECS-mutating bridge,
   per AGENTS.md's own explicit rule against repurposing an existing bridge
   for an unrelated kind of request.

## Step 3: The Plan (child phases)

| Phase | File | What it builds |
|---|---|---|
| 1 | `PHASE1_EDITOR_UI_COMMAND_BRIDGE.md` | The new cross-thread bridge (`src/Application/EditorUiCommandBridge.h/.cpp`) + the shared panel-name catalog (`src/Editor/EditorPanelCatalog.h`) + Tier-1 tests for both. Zero ImGui/Vulkan/httplib dependency — pure data + synchronization primitives only. |
| 2 | `PHASE2_IMGUI_TAB_ACTIVATION_ENGINE.md` | The actual tab-focusing mechanism: a new `IEditorLayer::ActivateTab()` virtual method, its `NullEditorLayer`/`ImGuiEditorLayer` implementations, and a new `DockLayout.h` helper (`FindAndFocusEditorWindow()`) that is the ONE place `imgui_internal.h`'s `FindWindowByName()` gets called from for this feature. |
| 3 | `PHASE3_APPLICATION_WIRING_AND_FRAME_LOOP_INTEGRATION.md` | Wires the Phase 1 bridge into `Application` (new member, passed into `NetworkServer`'s constructor) and drains it once per frame, at the correct point in `Application::Run()`, calling into Phase 2's `IEditorLayer::ActivateTab()`. |
| 4 | `PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md` | The actual HTTP surface: `NetworkRoutes.h/.cpp` parsing/response-building functions (pure, Tier-1-tested) + `NetworkServer.cpp` route registration for `GET /activate_tab` and `GET /list_tabs`. |
| 5 | `PHASE5_TESTING_DOCS_AND_REGRESSION_SAFETY.md` | End-to-end tests (mirrors `EngineCommandEndpointsEndToEndTests.cpp`), `AGENTS.md`/`README.md` documentation updates, full clean build + full `ctest` regression pass, and the campaign completion report. |

### Why this exact ordering

Each phase is buildable and independently compile-checkable in isolation,
bottom-up, exactly mirroring how `network-impl-3`/`network-impl-5` were
staged:

1. Phase 1 first, because it has ZERO dependency on anything Editor/ImGui/
   httplib-related — pure data types + a mutex/condition_variable, so it
   compiles and its own tests pass before anything else in this campaign
   exists.
2. Phase 2 next, because it's the one place genuinely new/risky ImGui
   docking behavior (`SetWindowFocus()` interacting with `DockBuilder`
   tabs) gets introduced — isolating it into its own phase, compiled and
   manually smoke-tested (`run_app_background` + `gte_send_request`) BEFORE
   it is wired to the network at all, makes it trivial to tell "the ImGui
   mechanism itself is wrong" apart from "the network plumbing is wrong" if
   something misbehaves later.
3. Phase 3 wires Phases 1+2 together inside `Application`, with no HTTP
   surface yet — this is the layer most likely to have a subtle ordering
   bug (see Phase 3's own "why this exact frame position" reasoning), and is
   easiest to reason about/test in isolation before an HTTP layer is added
   on top.
4. Phase 4 adds the actual HTTP endpoints, now that everything they call
   into already compiles and has already been manually verified end-to-end
   inside the running engine.
5. Phase 5 is verification, documentation, and campaign close-out — no new
   production code, only tests + docs + the final full build/regression
   pass (already-written code IS allowed to be adjusted here if a genuine
   bug surfaces, per this campaign's own workflow rule below).

### Cross-cutting rules every phase must follow

- **Every new class/function/type lives in the `gte` namespace** (or
  `gte::Network` for anything under `src/Network/`), per `AGENTS.md`,
  "Coding Guidelines".
- **RAII discipline**: the new bridge follows `EngineCommandBridge`'s exact
  RAII/locking shape — no manual cleanup, no leaked state.
- **A route handler stays a pure function of its own request data plus the
  ONE bridge it is allowed to touch** — `activate_tab`'s route handler may
  call `EditorUiCommandBridge::SubmitAndWait()` and nothing else engine-side,
  exactly mirroring `EngineCommandBridge`'s own established precedent (see
  AGENTS.md, "Networking").
- **Every Tier-1-testable piece of new logic ships with a matching test in
  the SAME phase that introduces it** — never "add tests later" (see
  AGENTS.md, "Testability & Regression Safety").
- **Fast compile check only, per phase** (`cmake --build build`) — no full
  `ctest` run required until Phase 5's own final step, which explicitly asks
  for one.
- **After each phase**: write a `PHASEn_COMPLETION_REPORT.md` into this same
  folder (`task_manager/network-impl-7/`) documenting what was actually
  done, and `git add`/`git commit` the change + the report together.
- **Always read the previous phase's own completion report before starting
  the next phase** — it may record a deviation from this plan (e.g. an
  exact API turned out to need one extra parameter) that the next phase
  must build against instead of this document's own original wording.
- **This feature needs NO new CMake option/switch.** It is gated entirely by
  the two switches that already exist and already apply to everything it
  touches: `GTE_ENABLE_NETWORK` (no server, no routes at all — same as every
  other endpoint) and `GTE_ENABLE_EDITOR` (no real tabs to activate — the
  `NullEditorLayer` path degrades this feature to "not available", exactly
  like every other Editor-touching capability already does in a release
  build). Do not invent a third switch.

## Endpoint contract (locked — every phase must build toward this exact shape)

**`GET /activate_tab?name=<PanelName>`**
- `200` — the named tab was found and focused this frame:
  `{"success":true,"activated_tab":"<PanelName>"}`
- `400` — missing/empty `name` query parameter:
  `{"success":false,"error":"missing or empty required query parameter: name"}`
- `404` — `name` is not one of the engine's known panel names (see
  `EditorPanelCatalog.h`, Phase 1):
  `{"success":false,"error":"unknown tab name '<name>' - see GET /list_tabs for the currently known names"}`
- `409` — `name` IS a known panel name, but no live window with that name
  exists yet this session. TWO distinct causes produce this exact same
  status/body, and both matter (a later phase's implementer must not treat
  either as an edge case to special-case away): (a) an extremely narrow race
  in a normal `GTE_ENABLE_EDITOR=ON` build — requested before the Editor has
  rendered its very first frame; or (b) PERMANENTLY, on every single call, in
  a `GTE_ENABLE_EDITOR=OFF` build — `NullEditorLayer::ActivateTab()` always
  reports `tabExists == false` (see Phase 2), since a release build has no
  Editor UI/tabs to activate at all. Case (b) is the actual, correct behavior
  of a `GTE_ENABLE_EDITOR=OFF` build (see Phase 5's own verification step for
  this exact case) — it is NEVER a 503 in that configuration, since
  `Application` still owns a real, non-null `EditorUiCommandBridge`
  unconditionally either way (see Phase 3); only `NullEditorLayer`'s own
  answer is permanently negative:
  `{"success":false,"error":"panel '<name>' has no live window yet this session - try again after the Editor has rendered at least one frame"}`
- `503` — the `EditorUiCommandBridge*` pointer itself is `nullptr` (only
  reachable when something constructs `NetworkServer` without one — e.g. a
  unit test; in real production `Application` always owns a real, non-null
  bridge, UNCONDITIONALLY, regardless of `GTE_ENABLE_EDITOR` — see Phase 3.
  Do NOT conflate this with `GTE_ENABLE_EDITOR=OFF`, which is the 409 case
  (b) above, not this one):
  `{"success":false,"error":"editor UI command bridge not available"}`
- `504` — the main thread did not drain/fulfill the request within the
  bridge's timeout:
  `{"success":false,"error":"editor UI command timed out"}`

**`GET /list_tabs`**
- Always `200`: `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere"]}`
  (plus `"Project"` at the end when `GTE_ENABLE_PROJECT_PANEL` is ON). This
  endpoint needs NO bridge round-trip at all — the panel catalog is a
  compile-time-fixed list, so this is answerable as a pure function of
  build configuration alone, with zero main-thread interaction (see Phase 4).

## Definition of Done for the whole campaign

- `cmake --build build` succeeds with `GTE_ENABLE_EDITOR=ON` (default),
  `GTE_ENABLE_EDITOR=OFF`, and `GTE_ENABLE_NETWORK=OFF` — three separate
  clean configure+build passes, per Phase 5.
- Full `ctest` run passes with zero regressions.
- A live, running engine instance answers `GET /list_tabs` correctly, and
  `GET /activate_tab?name=Profiler` visibly brings the "Profiler" tab to the
  front in a screenshot taken via `GET /get_swapchain` immediately
  afterward (Phase 5's own manual verification step).
- `AGENTS.md` ("Networking" section) and `README.md` ("Status") both gained
  a short, accurate entry for this feature, following every other
  campaign's existing documentation convention exactly.
