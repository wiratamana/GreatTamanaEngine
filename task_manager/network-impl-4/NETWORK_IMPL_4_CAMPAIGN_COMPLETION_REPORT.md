# `network-impl-4` — Campaign Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md`. This document is the short,
campaign-level summary cross-referencing all six phases' own detailed
completion reports (mirroring the convention already established by
`task_manager/network-impl-1/NETWORK_IMPL_1_CAMPAIGN_COMPLETION_REPORT.md`
and its sibling `network-impl-2`/`network-impl-3` campaigns).

## Goal recap

Give the engine's embedded HTTP server a GENERIC way to pull ANY render-graph
texture out as a PNG by name (`GET /get_texture`), plus a discoverability
companion (`GET /list_textures`) — closing the "only two hardcoded capture
endpoints" gap `network-impl-2` left open, and specifically anticipating a
future need to debug off-screen intermediate passes (e.g. atmosphere-
scattering LUTs) that never otherwise appear on screen. See
`PHASE0_MASTER_STRATEGY.md`'s own Step 1/Locked Design Decisions for the full
acceptance criteria.

## Phase-by-phase summary

| Phase | Deliverable | Report |
|---|---|---|
| **0** | Readiness check — confirmed the live shape of `RenderGraph`/`Renderer`/`FrameCaptureBridge`/`NetworkRoutes.h` this whole campaign depends on before any code was written. | `PHASE0_READINESS_CHECK_REPORT.md` |
| **1** | `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h/.cpp` — a new, pure, Tier-1-testable name→snapshot table (`DebugTextureSnapshot`: name/regime/target/hasDepth/colorState/depthState/lastUpdatedFrameCounter), `Upsert()`/`ApplyColorStateOverride()`/`FindByName()`/`ListAll()`. | `PHASE1_COMPLETION_REPORT.md` |
| **2** | Wires the registry into `RenderGraph` itself — a new private member, populated automatically at the end of every `ExecuteCompiledGraph()` call (both `ExecuteTimingMode` regimes), a shared `m_debugTextureFrameCounter`, and four new public accessors (`DebugTextureSnapshotFor()`/`ListDebugTextures()`/`NotifyDebugTextureStateOverride()`/`CurrentDebugTextureFrameCounter()`). | `PHASE2_COMPLETION_REPORT.md` |
| **3** | `Renderer::CaptureRenderTexturePixels()` refactored into a shared, PUBLIC `Renderer::CaptureImagePixels()` primitive (zero behavior change for its existing caller) usable against ANY `VkImage`/format/extent/`ResourceState`, plus `Renderer::WaitForGpuIdle()`, plus `gte::Encoding::ConvertDepthToGrayscaleRgba8()` (`src/Encoding/DepthVisualization.h/.cpp`). **ALL FOUR** graph-external manual-finalize call sites (`"GameView"`/`"SceneView"`/`"Swapchain"`/`"BlurredSceneOutput"`) wired to `RenderGraph::NotifyDebugTextureStateOverride()`. | `PHASE3_COMPLETION_REPORT.md` |
| **4** | `FrameCaptureBridge` gains `FrameCaptureKind::NamedTexture` + a dynamic `(textureName, channel)` request payload + its own `Slot`; `Application::Run()` gains the per-frame wiring servicing it via the registry/readback/encode primitives Phases 2/3 built. | `PHASE4_COMPLETION_REPORT.md` |
| **5** | `GET /get_texture` + `GET /list_textures` routes (`NetworkRoutes.h/.cpp`, `NetworkServer.cpp`) — `nlohmann::json`-built response shapes (including `frames_since_update`), query-parameter parsing (`texture_name`/`channel`), and the full failure-status mapping (400/409/503/504). | `PHASE5_COMPLETION_REPORT.md` |
| **6** | This phase — confirmed/filled in every earlier phase's own test coverage, updated `AGENTS.md`/`README.md`, ran the full build + `ctest` regression suite, did the manual end-to-end HTTP verification, and wrote this report. | This document |

## What the engine has now

- **Any render-graph texture can be captured as a PNG by name over HTTP.**
  `GET /get_texture?texture_name=<name>[&channel=color|depth][&format=png|base64|json]`
  captures whatever texture was registered under that exact name — every
  texture any pass declares via `RenderGraphBuilder::CreateTexture()`/
  `ImportTexture()` becomes capturable automatically, with zero opt-in
  required from that pass's own author. `GET /list_textures` enumerates every
  texture registered so far this session (`name`/`regime`/`format`/`width`/
  `height`/`has_depth`/`frames_since_update`).
- **`RenderGraphDebugTextureRegistry`** is the mechanism — a small, pure,
  Tier-1-tested name→snapshot table `RenderGraph` keeps passively up to date
  every frame, with zero per-pass opt-in cost.
- **One deliberate, narrow, bounded exception to this engine's usual "zero
  added GPU stall" networking rule**: `GET /get_texture` (and ONLY that
  endpoint) calls `Renderer::WaitForGpuIdle()` once per request — acceptable
  because this endpoint is rare, human/LLM-triggered debugging traffic,
  never part of any per-frame path. Documented explicitly in `AGENTS.md` as a
  warning against ever reusing this pattern elsewhere.
- **`RenderGraph::NotifyDebugTextureStateOverride()`** — the correction hook
  needed because a graph-external manual Vulkan barrier (a texture handed off
  for external sampling/presentation outside the graph's own compiled barrier
  plan) is otherwise invisible to the registry's own state tracking. All FOUR
  real call sites (`"GameView"`/`"SceneView"`/`"Swapchain"`/
  `"BlurredSceneOutput"`) are wired.
- **`GET /list_textures`'s `frames_since_update` plumbing** — resolved via a
  third, small, plain-data struct, `gte::PublishedTextureListEntry`
  (`FrameCaptureBridge.h`), deliberately kept SEPARATE from
  `gte::Network::TextureListEntryView` (`NetworkRoutes.h`) — see "Deviations"
  below for why this was the one design choice this campaign's own documents
  left open across phases, and how it was actually resolved.
- **Full automated regression coverage** for every new Tier-1 module: the
  registry (7 tests), depth-visualization conversion (7 tests), the
  `FrameCaptureBridge` `NamedTexture`-kind extension (now 9 tests — see
  "Deviations" below), and the new `NetworkRoutes.h` JSON builders/parser (13
  tests) — all passing, alongside every pre-existing test in the repository.

## Deviations from the strategy documents

**One real gap was found and closed during this (Phase 6) session — not a
design deviation, but a genuinely missing piece of test coverage Phase 4's own
completion report explicitly deferred here:**

- Phase 4's completion report states verbatim: *"the NEW `NamedTexture`-kind
  test cases (independent slot behavior, `RequestedTextureName()`/
  `RequestedTextureChannel()` correctness, the three-way `SlotFor()`
  distinctness check) are explicitly assigned to Phase 6."* Re-checking
  `tests/Application/FrameCaptureBridgeTests.cpp` at the start of this phase
  confirmed those cases had NOT yet been added (only Phase 5's own
  `/list_textures`-support tests — `PublishTextureList()`/
  `GetPublishedTextureList()` — were present). This phase added the three
  missing cases:
  - `NamedTextureRequestFulfillRoundTripCarriesNameChannelAndFramesSinceUpdate`
    — a full request/fulfill round trip, confirming
    `RequestedTextureName()`/`RequestedTextureChannel()` are correct while
    the request is pending, and that `framesSinceUpdate` round-trips through
    `CapturedPngImage`.
  - `NamedTextureRequestFailReturnsTargetNotAvailableQuickly` — mirroring the
    existing `Swapchain`/`GameView` fail-path test.
  - `SecondConcurrentNamedTextureRequestForDifferentNameReturnsAlreadyPendingAndDoesNotOverwriteFirstName`
    — the regression test for the "set name/channel before marking
    `requested`" ordering guarantee `FrameCaptureBridge.h` documents.
  - `AllThreeCaptureKindSlotsAreMutuallyDistinct` — the regression test for
    the `SlotFor()` ternary→switch rewrite (Phase 4's own fix), proving
    `Swapchain`/`GameView`/`NamedTexture` never share state even when all
    three are pending concurrently.

  All four new cases pass, alongside the 10 pre-existing `FrameCaptureBridgeTest`
  cases (14 total in that suite now, +9 from Phase 5's own additions = **the
  full Phase 6 checklist item is now closed**). No other gaps were found —
  every other test file/CMake registration this phase's own Step 3.1 checklist
  calls for (`RenderGraphDebugTextureRegistryTests.cpp`,
  `DepthVisualizationTests.cpp`, `NetworkRoutesTests.cpp`'s
  `ParseGetTextureQuery`/`BuildTextureCaptureJsonBody`/
  `BuildListTexturesResponseJson` suites, including the double-quote-escaping
  regression case) was already present and correctly wired into both
  `CMakeLists.txt` (root, for production `.cpp` files) and
  `tests/CMakeLists.txt` (for test `.cpp` files) by the time this phase began.

**`frames_since_update`/`/list_textures` "Option 1 vs Option 2" — already
resolved by Phase 5's own second-iteration audit, confirmed (not re-decided)
here:** the shape actually shipped is the three-struct split — `Application::Run()`
resolves each `rg::DebugTextureSnapshot` into a `gte::PublishedTextureListEntry`
(already-resolved plain scalars, kept in the `gte` namespace so
`FrameCaptureBridge.h` stays Vulkan/Renderer/RenderGraph-free), and
`NetworkServer.cpp` — the one place that legitimately depends on both
`FrameCaptureBridge.h` and `NetworkRoutes.h` — copies it 1:1 into a fresh
`gte::Network::TextureListEntryView` right before calling
`BuildListTexturesResponseJson()`. This keeps both of the engine's existing
"a struct must never cross this exact layer boundary" rules intact
simultaneously. `AGENTS.md`'s new "Named Texture Capture" sub-section
documents this split explicitly so a future contributor doesn't "simplify" it
back into one shared type.

No other deviations were found in Phases 1–5's own work — every phase's own
completion report already confirmed zero deviation from its own strategy
document, and re-reading them fresh during this phase (per the task's own
instruction to re-read every earlier phase's own "### Tests" section,
including second-iteration-audit additions) did not surface anything new
beyond the one gap above.

## Documentation updates (this phase)

- **`AGENTS.md`** — added a new "Named Texture Capture (`GET /get_texture`)"
  sub-section directly inside the existing "Networking" section (not a
  replacement), documenting: `RenderGraphDebugTextureRegistry` as the
  automatic-registration mechanism (and that buffers are NEVER visible this
  way); the pass-name-vs-texture-name distinction this campaign's own history
  got wrong once already; `Renderer::WaitForGpuIdle()`'s narrow, bounded
  exception to the "zero added GPU stall" rule; all FOUR
  `NotifyDebugTextureStateOverride()` call sites by name; the
  `frames_since_update` counter's own regime-dependent freshness caveat; the
  `PublishedTextureListEntry`/`TextureListEntryView` two-struct split and why
  it must stay two structs; and the two new endpoints' exact contract
  (including the full 400/409/503/504 status mapping, cross-checked directly
  against the live `NetworkServer.cpp` route handlers before writing it down).
- **`README.md`** — added one new "Status" bullet describing the feature in
  the same style/level of detail as the existing `network-impl-2`/
  `network-impl-3` bullets, cross-referencing this campaign's own
  `PHASE0_MASTER_STRATEGY.md` and this completion report.

## Full build + regression (this phase)

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

- **Build**: clean, zero errors/warnings (default `GTE_ENABLE_NETWORK=ON`
  configuration — the only configuration this phase's brief required).
- **`ctest`**: **100% tests passed, 1159/1159** (1 pre-existing, unrelated
  machine-gated smoke test skipped — `PmxLoaderRealModelSmokeTest`, gated on
  a real MMD model file's presence on this specific machine, entirely
  unrelated to this campaign). This is every test in the repository,
  including every pre-existing test from `network-impl-1`/`network-impl-2`/
  `network-impl-3` and every other prior campaign (Render Graph, GPU
  Skinning, verlet-integration, job_system, scene-serialization, etc.) — no
  regressions anywhere.

## Manual end-to-end verification (this phase)

Performed against a real, running `GreatTamanaEngine.exe` (Editor build,
launched via `run_app_background`, driven via `gte_send_request` and a
`powershell Invoke-WebRequest` fallback for one raw-JSON-field check),
mapping onto `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done" checklist:

1. `GET /get_texture?texture_name=GameView` → `200`, valid PNG (33895 bytes).
   The "Game" panel was not the active dock tab this session (default layout
   starts on "Scene"), so `GET /get_game_view` itself returned `409` at the
   same moment — this is EXPECTED, pre-existing behavior (see `README.md`'s
   own "Visibility-driven rendering" section: only the active Scene/Game tab
   renders), not a regression; the `GameView` texture itself was still
   present in the registry from earlier in the session and remained
   capturable via `/get_texture`, exactly as the stale-freshness design
   intends.
2. `GET /get_texture?texture_name=GameView&channel=depth` → `200`, a valid
   (solid-white, since the scene was empty with nothing but a far-plane
   clear) grayscale PNG.
3. `GET /get_texture?texture_name=GameView&format=json` → `200`, JSON
   envelope confirmed via a direct field-by-field check (`width=1264
   height=660 format=png frames_since_update=1682 data_base64_len=45196`) —
   `frames_since_update` is present, numeric, and visibly increases across
   requests (1682 → 2355 a few requests later) since the Game view stayed
   hidden/un-rendered the whole session, exactly matching the documented
   regime-dependent freshness caveat.
4. `GET /get_texture?texture_name=GameView&channel=bogus` → `400`,
   `{"error":"invalid channel - must be \"color\" or \"depth\"", ...}`.
5. `GET /get_texture` (no `texture_name`) → `400`,
   `{"error":"missing or empty required query parameter: texture_name", ...}`.
6. `GET /get_texture?texture_name=ThisNameNeverExists` → `504` (confirmed
   with a 15-second tool timeout — resolved well within it, never hung, never
   crashed the engine).
7. `GET /get_texture?texture_name=SceneView&channel=depth` → `200` (the
   "Scene" panel WAS the active/visible tab this session, so this returned a
   real depth capture rather than exercising the "hidden view" timeout path —
   still a valid, useful confirmation that a second, independently-tracked
   view's depth channel works correctly).
8. **`GET /get_texture?texture_name=Swapchain` → `200`, a valid PNG showing
   the actual presented Editor window content** (dock panels, menu bar,
   chrome) — confirmed visually identical in shape to what `GET
   /get_swapchain` independently returned moments later. This is the exact
   regression check `PHASE0_MASTER_STRATEGY.md` calls out by name for Locked
   Design Decision 7's corrected, four-call-site scope — passed.
9. **`"BlurredSceneOutput"` best-effort check: SKIPPED.** This texture is
   only ever registered while the Editor's "Show Compute Blur (debug)"
   checkbox is on, which has no HTTP-only way to toggle — this verification
   session had no UI-automation tooling available (only HTTP request tools),
   so this one check was not performed. Recorded here explicitly, per the
   Phase 6 strategy document's own instruction, rather than silently omitted.
10. `GET /list_textures` → `200`, confirmed the literal names `"GameView"`,
    `"SceneView"`, and `"Swapchain"` (never `"Present"`) all present, each
    with a real, recognizable `format` string (`"B8G8R8A8_UNORM"` for all
    three observed this session — never the `"VkFormat(...)"` numeric
    fallback), plausible `width`/`height`/`regime`/`has_depth` values, and
    each entry's own `frames_since_update` behaving exactly as documented
    (`0` for the two continuously-rendering targets this session — `Scene`
    view and the pipelined `Swapchain` — vs. a large, growing number for the
    hidden `GameView`).
11. Confirmed the engine kept running/updating throughout this whole
    verification session (repeated successful requests, `GameView`'s
    `frames_since_update` visibly advancing between calls, no hang/crash) —
    the per-request `WaitForGpuIdle()` hitch was not independently measured
    with a frame-timing tool in this pass, but no user-visible
    unresponsiveness or multi-second stall was observed across roughly a
    dozen consecutive requests.
12. **Regression check — every endpoint from prior campaigns still works
    correctly**: `GET /http_hello_world` → `200 hello world`; `GET
    /get_swapchain` → `200`, valid PNG; `GET /get_game_view` → `409` (correct,
    expected behavior for a hidden Game view — not a regression, see item 1);
    `POST /instantiate_primitive` (spawned `"Phase6VerificationCube"`) →
    `200`, `{"success":true,...}`; `POST /delete_entity` (same entity) →
    `200`, `{"success":true,...}`. All exactly as documented by
    `network-impl-1`/`network-impl-2`/`network-impl-3`.

The engine process was cleanly stopped via `stop_app_background` once
verification was complete.

## Follow-up work worth flagging for the future

- **Atmosphere-scattering LUTs (or any future off-screen compute/graphics
  pass)**: once that feature is actually implemented, its LUT pass(es) should
  show up in `GET /list_textures` — and be capturable via `GET /get_texture`
  — with ZERO extra code, purely because `RenderGraphDebugTextureRegistry`'s
  registration is fully automatic for anything declared via
  `RenderGraphBuilder::CreateTexture()`/`ImportTexture()`. Worth a quick
  confirmation check the first time such a pass actually lands, as the
  natural real-world proof of this mechanism's generality (deliberately NOT
  prototyped in this campaign itself — see `PHASE0_MASTER_STRATEGY.md`'s own
  Non-Goals and Phase 6's own "what this phase deliberately does not do").
- **The `"BlurredSceneOutput"` manual verification (Step 3.5, item 9) was
  skipped this session** (see above) for lack of UI-automation tooling in
  this particular verification pass — a future session with actual mouse/
  keyboard control over the Editor window (or a future headless/CLI toggle
  for "Show Compute Blur (debug)") should perform it at least once to fully
  close out that specific call site's own regression coverage.
- **`Renderer::WaitForGpuIdle()`'s per-request stall was not independently,
  numerically measured** (e.g. via the Profiler panel's own frame-time graph)
  during this session's manual verification — a future pass with visual/UI
  access to the Editor's "Profiler" panel could capture a concrete
  millisecond number for the record, though nothing in this campaign's own
  Definition of Done requires a specific bound on it beyond "a single,
  bounded hitch, not a sustained stall/hang", which was qualitatively
  confirmed.

## Campaign status: COMPLETE

Every item in `PHASE0_MASTER_STRATEGY.md`'s "Definition of Done" checklist is
satisfied:

- `cmake --build build` succeeds (default `GTE_ENABLE_NETWORK=ON`).
- `GET /get_texture?texture_name=GameView` returns a valid PNG.
- `GET /get_texture?texture_name=GameView&channel=depth` returns a valid
  grayscale PNG.
- `GET /get_texture?texture_name=Swapchain` returns a valid, visually
  plausible PNG (the named regression check for Locked Design Decision 7).
- `GET /get_texture?texture_name=DoesNotExist` returns `504`, never hangs,
  never crashes.
- `GET /list_textures` returns a JSON array with the literal names
  `"GameView"`/`"SceneView"`/`"Swapchain"`, correct regime/extent/format/
  hasDepth metadata for each.
- `ctest` passes, including every new test file/case this campaign adds
  (1159/1159, 1 unrelated pre-existing skip).
- `AGENTS.md` has an updated "Networking" section documenting
  `/get_texture`/`/list_textures` and `RenderGraphDebugTextureRegistry`;
  `README.md`'s "Status" section has a new bullet.

`network-impl-4` is closed out — any future networking work (a `POST`-based
mutation of a texture, an Editor "Network" panel, exposing this beyond
loopback, etc.) is new, follow-on work requiring its own fresh strategy
document, per this campaign's own explicitly stated non-goals.
