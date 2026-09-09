# PHASE5 — Completion Report: `GET /get_swapchain` Endpoint

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document executed:
`PHASE5_GET_SWAPCHAIN_ENDPOINT_AND_FORMAT_NEGOTIATION_REUSE.md`.

## Summary

Implemented exactly what the phase document specified: `Application::Run()`
wiring for Phase 4's `SwapchainCaptureService` (via `Renderer::
RequestSwapchainCapture()`/`TakeLastCompletedSwapchainCapture()`), and the
second and final route, `GET /get_swapchain`, reusing Phase 3's
`ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()` completely
unchanged. This is the campaign's SECOND fully working, end-to-end HTTP
endpoint, and — unlike Phase 3's own Game-view endpoint, whose success path
could not be manually exercised in that session (no visible "Game" tab) —
this phase's manual verification confirms the actual PNG success path
end-to-end, live, against a real running engine, closing the one open gap
Phase 3/4's own completion reports flagged.

## What was done

1. **`Application::Run()` wiring** (`src/Application/Application.cpp`), split
   into the exact two insertion points the phase document's own Step 3.1
   specifies:
   - A new, unconditional check placed directly in `Run()`'s own top-level
     body (NOT nested inside the `if (gameTarget != nullptr || sceneTarget !=
     nullptr)` block), immediately before the "Call 2 of 2: the PIPELINED
     swapchain-present regime" block: `if
     (m_captureBridge.IsCaptureRequested(FrameCaptureKind::Swapchain)) {
     m_renderer.RequestSwapchainCapture(); }` — runs every single frame, in
     every build configuration, matching the phase document's own
     "deliberately safe from Phase 3's own placement gotcha" note.
   - The success-path fulfillment, placed immediately after the existing
     `try { presentStats = m_renderer.PresentViaRenderGraph(...); } catch
     (...) { ... }` block (i.e. right after `PresentViaRenderGraph()` returns,
     whether or not it actually recorded/returned a value this frame) and
     before the pre-existing `if (presentStats.has_value())` GPU-stats block:
     `if (std::optional<CapturedSwapchainPixels> raw =
     m_renderer.TakeLastCompletedSwapchainCapture())` — BGRA→RGBA conversion
     via the existing `IsBgraFormat()`/`Encoding::ConvertBgraToRgbaInPlace()`
     (reused verbatim, no duplicate helper added), PNG encode via
     `Encoding::EncodeRgba8ToPng()`, then
     `m_captureBridge.FulfillPendingRequest(FrameCaptureKind::Swapchain, ...)`
     — mirroring Phase 3's own Game-view success-path shape exactly.
   - No "target not available" fast-fail branch was added for this endpoint
     (per the phase document's own Step 3.1 reasoning) — a minimized window
     simply lets a pending request time out via `FrameCaptureBridge`'s
     existing fixed 3-second timeout, exactly as `PHASE0_MASTER_STRATEGY.md`'s
     own Locked Design Decision #4 anticipates.
2. **`NetworkServer.cpp` route wiring** (`src/Network/NetworkServer.cpp`) —
   extracted the phase document's own suggested `RegisterCaptureRoute(httplib::Server&,
   const char* path, FrameCaptureKind kind, FrameCaptureBridge* captureBridge)`
   helper (since Phase 3's `/get_game_view` lambda and this phase's
   `/get_swapchain` lambda are byte-for-byte identical apart from the path
   and `FrameCaptureKind` value — exactly the "two real, live call sites now
   exist" trigger the phase document's own Step 3.2 calls for). `RegisterRoutes()`
   now calls this helper twice:
   `RegisterCaptureRoute(server, "/get_game_view", FrameCaptureKind::GameView, captureBridge);`
   and
   `RegisterCaptureRoute(server, "/get_swapchain", FrameCaptureKind::Swapchain, captureBridge);`
   — no new HTTP status code beyond the existing `503`/`504`/`409` mapping,
   and `ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()` are reused
   completely untouched.
3. **No new test file added** — per the phase document's own Step 3.4, this
   phase's route wiring is Tier 2 (needs a live `httplib::Server` at minimum,
   ultimately a live `Renderer`/window for a true end-to-end check), and
   `RegisterCaptureRoute()` exposes no new pure logic beyond what Phase 3's
   `tests/Network/NetworkRoutesTests.cpp` already covers
   (`ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()` themselves are
   unchanged). Covered instead by this phase's own live manual verification
   below, mirroring the "Phase 6 owns the dedicated end-to-end automated
   test" plan.

## Deviations from the phase document

- **None of substance.** Every step (3.1 through 3.4) was implemented as
  literally specified, including the exact placement/ordering the phase
  document's own code blocks and prose call for.
- One minor, expected clarification during implementation: the phase
  document's own Step 3.1 code snippet showed the pre-Present check as a
  bare `if` block with no surrounding comment; I added a short doc comment
  above it (mirroring the density of commentary already present throughout
  `Application.cpp`) purely for local readability/consistency with the rest
  of the file — no behavioral difference from the phase document's own
  snippet.

## Compile/test verification performed

- `cmake --build build --target gte_core` — succeeded (2 objects recompiled:
  `Application.cpp`, `NetworkServer.cpp`).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded.
- Ran the directly-relevant tests:
  `tests\GreatTamanaEngineTests.exe --gtest_filter=*NetworkRoutes*:*NetworkServer*:*FrameCaptureBridge*`
  — **24/24 passed** (identical suite/count to Phase 3's own run, confirming
  no regression from the `RegisterCaptureRoute()` extraction).
- Built the full `GreatTamanaEngine.exe` (`cmake --build build --target
  GreatTamanaEngine`) and ran it as a live manual smoke test — **this is the
  genuine end-to-end proof both Phase 4's and this phase's own completion
  reports flagged as still outstanding**:
  - `GET /get_swapchain` → **HTTP 200**, returning a byte-for-byte valid PNG
    (`89 50 4E 47 0D 0A 1A 0A ...` signature confirmed, `IHDR`/`IDAT`/`IEND`
    chunks present) — opened/viewed directly and confirmed it shows the
    ACTUAL live Editor UI (dock layout: "Hierarchy"/"Scene"+"Game" tabs/
    "Inspector" top row, "Memory"/"Profiler"/"Render Graph"/"Jobs"/"Project"
    tabbed bottom row, with the Project panel showing the real
    `Furina.gta`/`ChatanyaraKuushanku_bassui260717a.vmd`-imported
    `Furina_Textures` folder contents) — NOT a black/garbage/torn image, and
    NOT a stale frame (matches the actual on-screen window contents at
    capture time). This is direct, empirical proof that:
    1. Phase 4's `SwapchainCaptureService`'s barrier sequence (including the
       host-read-visibility buffer barrier) is correct.
    2. The frame-in-flight-indexed request/collect handshake this phase
       wires into `Application::Run()` is correct.
    3. The whole chain (Vulkan readback → BGRA→RGBA conversion → PNG encode →
       `FrameCaptureBridge` → HTTP route → response) works end-to-end for a
       genuinely different (harder, pipelined) capture path than Phase 3's
       already-synchronous one.
  - `GET /get_swapchain?format=base64` → **HTTP 200**, JSON body
    `{"width":1280,"height":720,"format":"png","data_base64":"..."}` —
    base64-decoded via a small PowerShell script and compared via SHA-256
    hash against the raw-bytes response: **identical hash on both**,
    confirming Phase 3's response-format-negotiation logic is reused
    correctly and produces byte-for-byte matching output for this endpoint
    too.
  - `GET /http_hello_world` still returned `200`/`"hello world"` both BEFORE
    and immediately AFTER the two capture requests above — the pre-existing
    route, and the engine's main frame loop generally, are completely
    unaffected; the window kept rendering/updating throughout (confirmed
    visually in the captured screenshot itself, which shows a live,
    correctly-laid-out Editor frame, not a frozen/corrupted one).
  - Process was cleanly terminated afterward (`stop_app_background`).
- Per this campaign's workflow rules, a full `ctest`/full rebuild was
  deliberately NOT run — only the targeted `gte_core`/`GreatTamanaEngineTests`
  builds, the directly-relevant tests, and the full live manual smoke test
  above, as instructed for a non-final phase.

## What the next phase (PHASE6) should know

- **Both endpoints (`/get_game_view` and `/get_swapchain`) are now fully
  proven end-to-end, live, with real PNG output verified** — Phase 6's own
  planned real, end-to-end `NetworkServerTests.cpp`-style automated test
  hitting both routes over a real socket has a known-working reference
  behavior to assert against (this phase's own manual `curl` output, plus
  the specific screenshot content described above, can serve as a sanity
  check for what "success" should look like).
- **One thing Phase 6 should specifically re-confirm with an actual
  automated test**: this phase's manual verification exercised
  `/get_swapchain`'s success path with the Editor's default dock layout
  (Scene/Game tabbed, Scene active) — i.e. this endpoint's success path does
  NOT depend on "Game" being the active tab the way `/get_game_view`'s does
  (the swapchain is captured regardless of which Editor tab is focused,
  since it's the literal presented window contents). Phase 6's own
  `/get_game_view` automated test may still need to account for the same
  "Game tab must be active" caveat Phase 3's completion report already
  flagged as unverified there — this phase does not change or close that
  gap, since it is specific to `/get_game_view`, not `/get_swapchain`.
- `RegisterCaptureRoute()` (`src/Network/NetworkServer.cpp`) is now the
  single, shared implementation behind both routes — a future third
  engine-state-touching endpoint (if one is ever added) that also fits this
  exact "request via `FrameCaptureBridge`, negotiate response format" shape
  should extend `FrameCaptureKind` and call this same helper a third time,
  rather than hand-rolling a new lambda.
- Nothing under `src/Renderer/`, `src/Application/FrameCaptureBridge.h/.cpp`,
  or `src/Encoding/` was touched in this phase — Phase 6 starts from a clean
  slate for its own automated-test/documentation work, exactly as
  `PHASE0_MASTER_STRATEGY.md`'s Phase Map anticipated.
