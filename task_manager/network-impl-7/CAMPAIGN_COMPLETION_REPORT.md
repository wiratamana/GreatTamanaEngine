# CAMPAIGN_COMPLETION_REPORT — network-impl-7 ("`GET /activate_tab` / `GET /list_tabs`")

Parent: `PHASE0_MASTER_STRATEGY.md`. This report closes out the whole
five-phase `network-impl-7` campaign, tying together `PHASE1_COMPLETION_
REPORT.md` through `PHASE5_COMPLETION_REPORT.md`.

## 1. The original problem

The engine's embedded loopback HTTP server already let an external HTTP/LLM-
agent caller read pixels back (`GET /get_swapchain`/`/get_game_view`/
`/get_texture`) and mutate the ECS world (`POST /instantiate_primitive`/
`/delete_entity`/`/set_entity_trs`/`/instantiate_light`), but had no way at
all to control the EDITOR UI itself — specifically, which docked tab
("Hierarchy", "Profiler", "Memory", ...) is currently the active/focused one.
An agent driving the engine remotely (e.g. to inspect the "Profiler" panel's
live data via a screenshot) had no way to bring that tab to the front first;
it could only ever screenshot whatever tab a human had last clicked.

## 2. What was built

A new, additive, five-phase feature giving an external caller exactly that
capability, plus a companion discovery endpoint:

- **`GET /activate_tab?name=<PanelName>`** — makes the named Editor tab the
  active/focused tab this same frame, exactly as if a human had clicked it.
  Returns `200` on success, `400` for a missing/empty `name`, `404` for an
  unrecognized panel name, `409` when the name is known but has no live
  window yet this session (a narrow just-started-Editor race, or
  PERMANENTLY in a `GTE_ENABLE_EDITOR=OFF` build), `503` when the bridge
  itself is unavailable (test-only), and `504` on a bridge timeout.
- **`GET /list_tabs`** — always `200`, reporting every currently-known panel
  name, needing no cross-thread round-trip at all (the catalog is
  compile-time-fixed).
- **`EditorUiCommandBridge`** (`src/Application/EditorUiCommandBridge.h/.cpp`)
  — a brand-new, narrow, single-purpose cross-thread bridge, structurally
  mirroring `EngineCommandBridge`, built specifically because this campaign's
  own request kind (an Editor-UI command) is a genuinely different class of
  thing from either `FrameCaptureBridge`'s read-only pixel requests or
  `EngineCommandBridge`'s ECS mutations — never repurposing either existing
  bridge, per `AGENTS.md`'s own explicit rule.
- **`src/Editor/EditorPanelCatalog.h`** — a new, shared, compile-time-fixed
  list of every known panel name (`kKnownEditorPanelNames`/
  `IsKnownEditorPanelName()`), deliberately living outside any
  `GTE_ENABLE_EDITOR`-gated compilation unit (a header with no `.cpp` is
  never gated), so both the new HTTP endpoints AND `DockLayout.cpp`'s own
  default-layout logic now read from the exact same source of truth instead
  of two independently-hand-maintained lists.
- **`IEditorLayer::ActivateTab()`** — a new virtual method, implemented for
  real in `ImGuiEditorLayer` (via a new `DockLayout.h` helper,
  `FindAndFocusEditorWindow()`, the one place `imgui_internal.h`'s
  `FindWindowByName()` is called for this feature) and as a permanent,
  honest `tabExists = false` no-op in `NullEditorLayer` (a
  `GTE_ENABLE_EDITOR=OFF` build has no Editor UI/tabs to activate at all).
- **`Application::Run()` frame-loop wiring** — drains at most one pending
  `EditorUiCommandRequest` per frame, immediately after
  `m_editorLayer->NewFrame()` and before `BuildUI()`, so a tab-activation
  request is visible in that same frame's own rendering rather than lagging
  one frame behind.

## 3. What each phase did

- **Phase 1 — Editor UI Command Bridge + Shared Panel Catalog**
  (`PHASE1_COMPLETION_REPORT.md`): built `EditorUiCommandBridge.h/.cpp` (a
  structural copy of `EngineCommandBridge`, zero ImGui/Vulkan/httplib
  dependency) and `EditorPanelCatalog.h`, both registered in `gte_core`'s
  main, unconditional source list, with 11 new Tier-1 tests
  (`EditorUiCommandBridgeTests.cpp`, `EditorPanelCatalogTests.cpp`).
  Compiled but unused by any production call site yet, exactly as planned.
- **Phase 2 — ImGui Tab Activation Engine**
  (`PHASE2_COMPLETION_REPORT.md`): added `IEditorLayer::ActivateTab()` and
  its two implementations, plus `DockLayout.h`'s
  `FindAndFocusEditorWindow()` — the genuinely new/risky ImGui docking
  behavior in this whole campaign, isolated into its own phase and manually
  smoke-tested end-to-end via a temporary hardcoded call site before any
  network plumbing existed, so an ImGui-mechanism bug could never be
  confused with a network-plumbing bug later.
- **Phase 3 — Application Wiring + Frame-Loop Integration**
  (`PHASE3_COMPLETION_REPORT.md`): made `Application` own a real, non-null
  `EditorUiCommandBridge` unconditionally, threaded a third bridge pointer
  through `NetworkServer`'s constructor/`RegisterRoutes()`, and inserted the
  per-frame drain block at the one correct point in `Application::Run()` —
  immediately after `NewFrame()`, before `BuildUI()` — with no HTTP route
  yet to exercise it, verified by code inspection plus a no-crash smoke test.
- **Phase 4 — HTTP Endpoints** (`PHASE4_COMPLETION_REPORT.md`): added
  `NetworkRoutes.h/.cpp`'s parsing/response-building functions and
  `NetworkServer.cpp`'s two route registrations, 10 new Tier-1 tests, and a
  full manual end-to-end smoke test against a real running engine —
  `GET /activate_tab?name=Profiler` followed by `GET /get_swapchain`
  visually confirmed the "Profiler" tab genuinely became the frontmost tab.
- **Phase 5 — Testing, Docs, and Regression Safety**
  (`PHASE5_COMPLETION_REPORT.md`, this session, run as part of the
  `doc-refactor-1` campaign's own Phase 1): confirmed
  `tests/Network/ActivateTabEndpointEndToEndTests.cpp` was already written
  and registered (a superset of the originally-planned case list, split
  across three suites), added the missing `AGENTS.md`/`README.md`
  documentation bullets, and wrote this campaign completion report. Sections
  3.5 (full 3-configuration build+regression pass) and 3.6 (live runtime
  smoke test) were deliberately deferred to
  `task_manager/doc-refactor-1/PHASE5_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`,
  so the combined effort pays for one full regression pass instead of two —
  see that file's own eventual addendum to this report for their outcome.

## 4. The final result

`GET /activate_tab?name=<PanelName>` and `GET /list_tabs` are fully live,
reachable endpoints on a running engine, verified both by automated Tier-1/
Tier-2-style tests (37 tests passing when filtered to
`*ActivateTab*:*ListTabs*:*Network*`, confirmed this session) and by a real,
running-engine manual smoke test (Phase 4) that visually confirmed the
tab-activation side effect via a swapchain screenshot. `AGENTS.md`
("Networking") and `README.md` ("Status") both now carry an accurate,
house-style-matching entry for this feature. No existing endpoint's behavior
changed; the only public-interface change was the strictly additive
`IEditorLayer::ActivateTab()` virtual method, implemented by both existing
layers.

## 5. Final build/test pass result

Deliberately **deferred** to `task_manager/doc-refactor-1/PHASE5_FULL_
REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`, per this campaign's own Phase 5
report — that phase will run the one full, 3-configuration clean build +
full `ctest` regression pass covering BOTH this feature and the `doc-refactor-1`
documentation split, and will append a short addendum directly below this
line once it does, confirming the final test count and a zero-regression
result for this feature specifically.

<!-- doc-refactor-1 Phase 5 addendum will be appended below this line. -->

## 6. Campaign disposition

All five phases of `network-impl-7` are functionally complete: the feature
itself, its tests, its ImGui mechanism, its frame-loop wiring, its HTTP
surface, and its documentation are all done and verified in isolation.
Only the campaign's own final full-regression/live-smoke-test obligations
(Sections 3.5/3.6 of `PHASE5_TESTING_DOCS_AND_REGRESSION_SAFETY.md`) remain
open, by deliberate design, pending `doc-refactor-1`'s own Phase 5 — this is
not a gap in this campaign's own work, it is a locked, cross-campaign
sequencing decision made explicitly to avoid running the full regression
suite twice in the same overall effort. This campaign will be considered
fully closed once that addendum is appended above.

## Addendum — Sections 3.5/3.6 Discharge Confirmation (via doc-refactor-1)

`doc-refactor-1`'s own Phase 5 (`task_manager/doc-refactor-1/PHASE5_FULL_
REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`) ran the full 3-configuration clean
build + `ctest` regression pass this campaign's own Section 3.5 deferred,
plus the manual live smoke test Section 3.6 deferred. Results: default
configuration (`build/`) — 1326 tests, 100% passed (1 machine-gated
`PmxLoaderRealModelSmokeTest` skip, zero failures); `-DGTE_ENABLE_EDITOR=OFF`
(`build-editor-off/`) — 1140 tests, 100% passed after this same Phase 5
fixed one genuine pre-existing test bug it surfaced (a dangling
`const Transform&` held across a second `Registry::AddComponent<Transform>()`
call in `GameEntityCommandsTests.cpp`'s
`InstantiateLightDefaultCallMatchesCreateDirectionalLightEntityRotation` —
unrelated to `GTE_ENABLE_EDITOR` itself, see that phase's own completion
report for the full root-cause writeup); `-DGTE_ENABLE_PROJECT_PANEL=OFF`
(`build-project-panel-off/`) — 1255 tests, 100% passed. The manual live
smoke test passed in full across both the default and
`-DGTE_ENABLE_PROJECT_PANEL=OFF` binaries: `GET /list_tabs` and
`GET /activate_tab?name=Profiler`/`Memory`/`NotARealTab` behaved exactly as
specified in the default configuration (each activation visually confirmed
via `GET /get_swapchain`), and `-DGTE_ENABLE_PROJECT_PANEL=OFF` correctly
omitted `"Project"` from `/list_tabs` and returned `404` (not `409`/`200`)
for `GET /activate_tab?name=Project`. See
`task_manager/doc-refactor-1/PHASE5_COMPLETION_REPORT.md` and
`task_manager/doc-refactor-1/CAMPAIGN_COMPLETION_REPORT.md` for the full
verification detail. This campaign (`network-impl-7`) is now fully closed.
