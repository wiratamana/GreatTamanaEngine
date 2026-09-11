# PHASE5 — End-to-End Tests, Documentation, Full Build & Regression Pass

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first.
**Also read:** `PHASE1_COMPLETION_REPORT.md` through
`PHASE4_COMPLETION_REPORT.md` before starting — this phase closes out the
whole campaign and must reconcile against whatever the real, as-built state
turned out to be (deviations happen; trust the completion reports over this
document's own assumed wording wherever they disagree).

## Step 1: The Goal

Prove the ENTIRE feature end-to-end (a real cross-thread request, not just
each phase's own isolated unit tests), bring the project's own
documentation (`AGENTS.md`, `README.md`) up to date following every
existing campaign's own documentation convention, and run this campaign's
one and only FULL clean build + full `ctest` regression pass across every
build configuration this feature can affect.

## Step 2: The Situation / The Problem

- Phases 1-4 each verified their OWN slice in isolation (a bridge with no
  network caller yet, an ImGui mechanism with a temporary hardcoded call
  site, wiring with nothing to drive it, routes with only nullptr-bridge
  coverage). Nothing yet proves the FULL chain — a real HTTP request
  reaching a real `EditorUiCommandBridge`, drained by something standing in
  for `Application::Run()`'s own new frame-loop step, calling into
  something standing in for `IEditorLayer::ActivateTab()` — end to end, the
  same way `tests/Network/EngineCommandEndpointsEndToEndTests.cpp` already
  proves the `/instantiate_primitive`/`/delete_entity` chain end to end
  without needing a live Vulkan device/window (it fakes the "main thread"
  side with a plain, direct function call driven by a background
  `std::thread` standing in for the network thread — read that file in
  full before starting, it is the exact template for this phase's new
  test file).
- Every existing campaign's own `README.md`/`AGENTS.md` entries follow a
  consistent shape (see `AGENTS.md`'s "Networking" section's own bullet
  list, and `README.md`'s "Status" section's own reverse-chronological
  bullet list) — this campaign's entry must match that shape, not invent a
  new documentation style.
- This is also the one phase explicitly permitted to go back and FIX a
  genuine bug found during this final verification pass (per PHASE0's own
  workflow note) — if the manual/automated end-to-end pass surfaces a real
  defect in an earlier phase's code, fix it here, note the fix in this
  phase's own completion report, and re-run the affected phase's own
  verification steps before continuing.

## Step 3: The Plan

### 3.1 — New end-to-end test file:
`tests/Network/ActivateTabEndpointEndToEndTests.cpp`

Mirror `tests/Network/EngineCommandEndpointsEndToEndTests.cpp`'s own
structure exactly: a real `httplib::Client` (or this codebase's own
existing lightweight HTTP-request test helper — check
`tests/Network/NetworkTestHelpers.h` first and reuse whatever helper
already exists there rather than inventing a new one) issuing a real HTTP
request against a `NetworkServer` bound to an OS-assigned ephemeral port
(`Start(0)`, then `BoundPort()` — the existing test-only pattern), backed
by a REAL `EditorUiCommandBridge`, with a background `std::thread`
standing in for `Application::Run()`'s own new drain step — i.e. that
thread loops calling `TryPeekPendingCommandRequest()` (with a short sleep/
yield between attempts) and, once it sees a request, calls
`FulfillCommand()` with a HAND-CONSTRUCTED `ActivateTabOutcome` (there is
no real ImGui context in this test process at all, so this test does NOT
exercise `IEditorLayer::ActivateTab()` itself — that remains Phase 2's own
manual-only verification, per this codebase's established "Tier 2, no
automated coverage yet" acceptance for anything genuinely ImGui/GPU-window-
dependent).

Cover, at minimum:
- `ActivateTabSucceedsWhenMainThreadReportsTabExists` — the fake main-thread
  loop fulfills with `{ success: true, tabExists: true }`; assert the real
  HTTP response is `200` with the exact expected JSON body
  (`{"success":true,"activated_tab":"Profiler"}`).
- `ActivateTabReturns409WhenMainThreadReportsTabDoesNotExistYet` — fulfills
  with `{ success: false, tabExists: false }`; assert `409`.
- `ActivateTabReturns404ForAnUnknownName` — request `name=NotARealTab`; no
  background "main thread" fulfillment is ever needed for this case (the
  route handler must reject it BEFORE ever touching the bridge — assert
  this by never spinning up the fake main-thread loop at all for this one
  test, and still observing a fast `404`, proving the pre-validation in
  `ParseActivateTabQuery()` really does short-circuit before any bridge
  round-trip).
- `ActivateTabReturns400ForMissingName` — request `/activate_tab` with no
  `name` query parameter at all.
- `ActivateTabReturns503WhenBridgeIsNull` — construct the `NetworkServer`
  with `uiCommandBridge = nullptr`; assert `503` for a KNOWN name.
- `ActivateTabReturns404NotServiceUnavailableForUnknownNameEvenWithNullBridge`
  — the ordering regression test Phase 4 Section 3.3 flagged: same nullptr
  bridge, but `name=NotARealTab` — assert `404`, NOT `503`.
- `ActivateTabReturns504WhenNeverFulfilled` — a short bridge timeout
  (submit with a small `timeoutMilliseconds`, or rely on the route's
  default and a deliberately slow/absent fake main thread — pick whichever
  matches this test file's own realistic runtime budget, mirroring how
  `EngineCommandEndpointsEndToEndTests.cpp` already handles its own
  timeout test without making the whole suite slow).
- `ListTabsReturnsEveryKnownPanelName` — a plain `GET /list_tabs` (no bridge
  needed at all, per its own design) — assert `200` and that the JSON
  `"tabs"` array matches `gte::kKnownEditorPanelNames` exactly.

Register this new file in `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list,
alongside the existing `Network/*EndToEndTests.cpp` entries.

### 3.2 — `AGENTS.md` update ("Networking" section)

Add a new bullet, placed directly after the existing `"Named Texture
Capture"` subsection's own content (or wherever the "Networking" section's
existing chronological bullet list naturally continues — match the exact
placement convention already used for every prior campaign's own addition
to this section), following the exact prose style/level of detail already
established there. At minimum, it must document:
- The new `EditorUiCommandBridge` and WHY it is a separate bridge from
  `EngineCommandBridge`/`FrameCaptureBridge` (cite the existing rule this
  campaign followed, from the `FrameCaptureBridge` bullet already in this
  section).
- `GET /activate_tab`'s full contract (query parameter, every status code
  and what triggers it) and `GET /list_tabs`'s contract.
- The exact frame-loop position `Application::Run()` drains this new bridge
  at (`NewFrame()` -> drain -> ... -> `BuildUI()`), and WHY (the one-frame-
  lag reasoning from Phase 3).
- `EditorPanelCatalog.h`'s existence and its role as the single shared
  source of truth for panel names (`DockLayout.cpp`'s own default-layout
  logic AND this campaign's two new endpoints both read from it now).

### 3.3 — `README.md` update ("Status" section)

Add one new bullet at the END of the existing reverse-chronological
"Status" list (after the `atmosphere-scattering-4` entry, which is
currently last), in the exact same style/voice as every other entry there
(bold lead sentence, then supporting detail, referencing the exact new
files/endpoints by name) — summarizing this feature for a reader who has
not read any of this campaign's own `task_manager/network-impl-7/*.md`
files, mirroring how e.g. the `network-impl-6`/`atmosphere-scattering-3`
entries already summarize THEIR own campaigns concisely.

### 3.4 — Campaign completion report

Write `CAMPAIGN_COMPLETION_REPORT.md` in `task_manager/network-impl-7/`
(the folder-level summary every prior campaign in this repository ends
with — see e.g. `task_manager/network-impl-6/NETWORK_IMPL_6_CAMPAIGN_COMPLETION_REPORT.md`
or `task_manager/atmosphere-scattering-3/CAMPAIGN_COMPLETION_REPORT.md` for
the expected shape/level of detail), tying together all five
`PHASEn_COMPLETION_REPORT.md` writeups into one final summary: what was
built, what was verified, what (if anything) was deliberately left out of
scope, and the final test-count delta (e.g. "Full suite: N tests, 1
pre-existing machine-gated smoke test skipped, zero regressions" — copy the
exact phrasing convention already used by every prior campaign's own final
report).

### 3.5 — Full build + full regression pass (the one place in this whole
campaign a FULL build/test run is required, per PHASE0's own workflow rule)

Run all three of the following, in order, each a CLEAN configure (delete or
reuse `build/` per this project's own normal workflow — see `BUILDING.md`)
followed by a full build and a full `ctest` run:

1. Default configuration (`GTE_ENABLE_EDITOR=ON`, `GTE_ENABLE_NETWORK=ON`,
   `GTE_ENABLE_PROJECT_PANEL=ON` — every switch at its default). Confirm
   the full suite passes with zero regressions, and note the new total
   test count in this phase's own completion report.
2. `-DGTE_ENABLE_EDITOR=OFF`. Confirm this still builds cleanly (this is
   the configuration that proves `NullEditorLayer::ActivateTab()`,
   `EditorUiCommandBridge` itself, and `NetworkRoutes.cpp`'s new pure
   functions all compile with ZERO ImGui linked at all) and that
   `GET /activate_tab`/`GET /list_tabs` still respond sensibly (every known
   panel name still round-trips through `IsKnownEditorPanelName()`
   correctly even with no Editor at all — `GET /list_tabs` is unaffected by
   this switch entirely, since its data is compile-time-fixed and has no
   dependency on `GTE_ENABLE_EDITOR`; `GET /activate_tab` for a known name
   now always gets a `409`-shaped outcome from `NullEditorLayer`'s
   `tabExists = false`, never a crash).
3. `-DGTE_ENABLE_PROJECT_PANEL=OFF`. Confirm `GET /list_tabs`'s response no
   longer includes `"Project"`, and `GET /activate_tab?name=Project`
   correctly now returns `404` (since `IsKnownEditorPanelName("Project")`
   is `false` in this configuration) rather than `409`/`200`.

### 3.6 — Live runtime smoke test (manual, the campaign's final acceptance
check — matches PHASE0's own "Definition of Done" bullet)

1. `run_app_background` the default-configuration build.
2. `gte_send_request` (or `run_shell`/`curl`) `GET /list_tabs` — confirm
   the full expected panel list.
3. `gte_send_request` `GET /activate_tab?name=Profiler` — confirm `200`.
4. Immediately `gte_send_request` `GET /get_swapchain` (or use `load_image`
   against a saved screenshot) and visually confirm the "Profiler" tab is
   now the frontmost/active tab among the bottom-docked group.
5. `gte_send_request` `GET /activate_tab?name=Memory` — confirm `200`, then
   re-capture `/get_swapchain` and confirm "Memory" is now frontmost
   instead (proving repeated activation genuinely switches the active tab
   each time, not just the first call).
6. `gte_send_request` `GET /activate_tab?name=NotARealTab` — confirm `404`.
7. `stop_app_background` the process.

## Step 4: Verification for this phase (= verification for the whole campaign)

- Every item in Section 3.5 (three separate clean configure+build+`ctest`
  passes) is green.
- Section 3.6's manual runtime smoke test passes exactly as described.
- `AGENTS.md`/`README.md` updates are accurate and match this codebase's
  existing documentation voice/format precisely (re-read a neighboring
  entry immediately before writing this campaign's own, so the tone
  matches).
- `CAMPAIGN_COMPLETION_REPORT.md` is written and accurate.
- `git add`/`git commit` everything from this phase (tests, docs, the
  completion report, and the campaign report) together, as the final
  commit of this campaign.
