# PHASE6 — Automated Tests, Documentation, and Full Regression Pass

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phases 1-5 all complete and
individually manually verified. This is the campaign's closing phase — it
adds the one real end-to-end automated test the campaign has been building
towards, finalizes documentation, and performs the full build+test
regression pass `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done"
requires.

## Step 1: The Goal (Where are we going?)

Leave the repository in a state where:
- Every Tier-1 module this campaign added (Phase 1's encoding utilities,
  Phase 2's `FrameCaptureBridge`, Phase 3's format-negotiation helpers) has
  its own passing test file, already added in its own phase — this phase
  does not need to re-add those, only confirm they're still green.
- A NEW, real, Tier-2, end-to-end test exists that starts a real
  `NetworkServer` wired to a real capture-serving loop and hits both
  `/get_game_view` and `/get_swapchain` over a real socket — the strongest
  possible regression proof this campaign's entire cross-thread plumbing
  actually works, not just its individual pieces in isolation.
- `AGENTS.md` and `README.md` are fully up to date.
- A full `cmake --build build` + `ctest -C Debug --output-on-failure` pass
  is clean.

## Step 2: The Situation (Where are we now?)

- `tests/Network/NetworkServerTests.cpp` already establishes the pattern a
  real, live, socket-level HTTP test in this repo follows (start a real
  `NetworkServer` on an ephemeral port, poll until it's accepting
  connections, use `httplib::Client` to issue real requests) — this phase's
  new test extends that exact pattern, the only new wrinkle being that a
  capture request needs SOMETHING to actually service it from "the main
  thread" side (there is no real `Application`/window/GPU available inside
  a plain GoogleTest binary) — see 3.1 below for how this is bridged without
  needing a live Vulkan device at all.
- Every other Tier-2 (GPU-dependent) class in this engine has "no automated
  test coverage yet" as an explicitly ACCEPTED state (`AGENTS.md`,
  "Testability & Regression Safety": *"GPU-dependent ('Tier 2') code...
  has no automated test coverage yet... this must never be treated as a
  blocker"*) — this campaign does NOT need to add a headless-Vulkan test
  fixture to reach "done"; Phase 4/5's own Renderer/FramePresenter/
  SwapchainCaptureService changes stay in that same accepted, manually-
  verified-only bucket. The NEW end-to-end test this phase adds tests the
  `FrameCaptureBridge`+`NetworkServer` wiring with a FAKE, GPU-free capture
  producer standing in for `Application::Run()`'s real per-frame logic —
  this is both achievable AND the most valuable thing to actually automate
  (it's the part with real, non-obvious cross-thread timing behavior; the
  GPU pixel-copying itself is comparatively low-risk, ordinary Vulkan
  plumbing already covered by this campaign's extensive manual verification
  in Phases 3-5).

## Step 3: The Plan

### 3.1 — End-to-end capture test

New file, `tests/Network/CaptureEndpointsEndToEndTests.cpp`:

- Construct a real `FrameCaptureBridge` and a real `NetworkServer(&bridge)`,
  `Start(0)` (ephemeral port), same `WaitUntilAcceptingConnections()`-style
  helper `NetworkServerTests.cpp` already has (reuse it — move it to a
  small shared test helper header, e.g. `tests/Network/NetworkTestHelpers.h`,
  if it's not already reusable as-is, rather than copy-pasting it a second
  time).
- Spawn ONE dedicated `std::thread` standing in for "the main thread" —
  a tight loop that calls `bridge.IsCaptureRequested(kind)` for BOTH kinds
  every ~5ms and, whenever true, calls
  `bridge.FulfillPendingRequest(kind, <a small, hand-built, valid 2x2 PNG -
  see Phase 1's own PngEncoderTests.cpp for how to build one inline>)` —
  this is the GPU-free stand-in for `Application::Run()`'s real per-frame
  logic described above. Join/stop this thread cleanly at the end of each
  test (a `std::atomic<bool> stop` flag it checks each loop iteration).
- `TEST(CaptureEndpoints, GetGameViewReturnsRawPngByDefault)`: real
  `httplib::Client::Get("/get_game_view")`, assert `res->status == 200`,
  `res->get_header_value("Content-Type") == "image/png"`, and that
  `res->body` starts with the PNG magic bytes (`\x89PNG\r\n\x1a\n`).
- `TEST(CaptureEndpoints, GetGameViewFormatBase64ReturnsJson)`: same but
  `Get("/get_game_view?format=base64")`, assert `Content-Type ==
  "application/json"`, parse out `data_base64` (a simple substring
  extraction is fine given this campaign's own fixed, known JSON shape — no
  JSON library needed for the TEST either), base64-decode it (write a tiny
  test-only decoder, or reuse `Encoding::EncodeBase64`'s own known-vector
  test cases in reverse if that's easier), and assert the decoded bytes are
  byte-for-byte identical to the RAW-PNG response's body from the previous
  test's own hand-built 2x2 PNG.
- `TEST(CaptureEndpoints, GetSwapchainWorksTheSameWay)`: mirrors the two
  tests above for `/get_swapchain`.
- `TEST(CaptureEndpoints, SecondConcurrentRequestOfSameKindGets503)`: from
  the main test thread, start a request that the fake "main thread" stand-in
  is deliberately made to NOT fulfill immediately (e.g. gate it behind a
  second, test-controlled flag) — fire a second, concurrent request for the
  SAME kind from a second `std::thread`, assert it returns `503` promptly
  (well before any timeout).
- `TEST(CaptureEndpoints, TimeoutSurfacesAs504)`: a request for a kind the
  fake "main thread" stand-in never services at all, with a SHORT bridge
  timeout for this one test only (confirm `FrameCaptureBridge::
  RequestCaptureAndWait()`'s `timeoutMilliseconds` parameter is reachable
  end-to-end from the HTTP layer for test purposes — if it's currently
  hardcoded inside the route lambda in `NetworkServer.cpp` rather than
  configurable, that's fine for production, but this specific test needs
  ITS OWN direct `FrameCaptureBridge::RequestCaptureAndWait(kind, 100)` call
  from a plain test thread instead of going through HTTP at all, to keep the
  test itself fast — asserting the BRIDGE'S OWN timeout behavior, which the
  HTTP route already forwards verbatim, is just as strong a regression proof
  without needing a slow, real 3-second HTTP round-trip in the test suite).

### 3.2 — `AGENTS.md` final review

Confirm the "Networking" section (updated incrementally in Phase 2) reads
coherently end to end once every phase has landed — in particular, add one
more bullet (or fold into the existing Phase-2-added one) documenting the
concrete endpoints themselves:

> **`GET /get_swapchain`/`GET /get_game_view`** (`network-impl-2` campaign)
> are this engine's first engine-state-touching endpoints, built entirely
> on top of `FrameCaptureBridge` (see above) — `src/Encoding/` (Base64/
> pixel-conversion/PNG-encode, Tier 1), `Renderer::CaptureRenderTexturePixels()`
> (synchronous Game-view readback) and `SwapchainCaptureService`
> (`src/Renderer/SwapchainCaptureService.h/.cpp`, a pipelined, frame-in-
> flight-aware swapchain readback mirroring `GpuTimingService`'s own
> Present-timing pattern with zero added GPU stall) are the two capture
> mechanisms feeding it. A future THIRD capture kind (e.g. a Scene-view
> endpoint) should reuse `Renderer::CaptureRenderTexturePixels()` directly
> (it already works for ANY `RenderTexture`, not just the Game view) rather
> than duplicating it.

### 3.3 — `README.md` "Status" update

Add a new bullet, matching the file's existing per-feature bullet style
(see the existing Networking bullet added by `network-impl-1` as the
template to copy), describing both new endpoints, the response-format
query parameter, and linking back to `task_manager/network-impl-2/
PHASE0_MASTER_STRATEGY.md`.

### 3.4 — Full regression pass

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Both must be clean. Additionally, per `AGENTS.md`'s own "Networking"
precedent (`network-impl-1`'s own Definition of Done required BOTH
`GTE_ENABLE_NETWORK=ON` and `=OFF` to build), confirm a
`-DGTE_ENABLE_NETWORK=OFF` configure ALSO still builds cleanly — this
campaign added no new CMake option of its own (Locked Design Decision #6),
so this is really a confirmation that nothing accidentally introduced a
hard, unconditional dependency on `Network::NetworkServer` existing (e.g. a
stray, un-guarded include or a `FrameCaptureBridge` member that somehow
requires it) — `Application`'s own `#if GTE_ENABLE_NETWORK`-gated
`m_networkServer.Start(8080)` call is the ONLY thing that should differ
between the two configurations; `m_captureBridge` itself must stay
unconditional/always-constructed in `Application` either way (it has zero
dependency on `GTE_ENABLE_NETWORK`, matching `src/Encoding/`'s own
"always compiles" convention from Phase 1) since Phase 3/5's `Application::
Run()` capture-checking code calls `m_captureBridge.IsCaptureRequested(...)`
unconditionally every frame regardless of that switch — the only thing
`GTE_ENABLE_NETWORK=OFF` actually removes is the HTTP server that could ever
SET a pending request in the first place, so those calls simply always see
"nothing requested" and cost one cheap mutex lock per frame per kind, same
as an Editor build with the Editor's own panels doing similar per-frame
housekeeping checks elsewhere.

### 3.5 — Final manual acceptance pass

Re-run, once more, end to end, exactly as described in Phase 3/5's own
manual verification steps, but now against the FULLY landed campaign in one
sitting:

```
curl http://127.0.0.1:8080/get_game_view -o game_view.png
curl http://127.0.0.1:8080/get_swapchain -o swapchain.png
curl "http://127.0.0.1:8080/get_swapchain?format=base64" -o swapchain.json
```

Open both `.png` files in an image viewer; confirm `swapchain.json`'s
`data_base64`, decoded, matches `swapchain.png` byte-for-byte; confirm the
engine window kept rendering at full frame rate throughout, exactly as
`PHASE0_MASTER_STRATEGY.md`'s own Goal requires.

### What NOT to do in this phase

- Do not add a headless-Vulkan (`VK_EXT_headless_surface`) test fixture to
  try to get Phase 3/4/5's GPU-touching code under automated test — this
  remains an explicitly accepted, documented gap per `AGENTS.md`'s own
  "Testability & Regression Safety" section, exactly like every other Tier-2
  class in this engine.
- Do not weaken `FrameCaptureBridge`'s 3-second default production timeout
  to make the NEW end-to-end test faster — use a per-call override
  (`RequestCaptureAndWait(kind, shortTimeoutForTestsOnly)`) instead, exactly
  as `PROFILER_STRATEGY_v2.md`-style precedents in this engine already do
  for other "keep production defaults sane, override per-test" needs (see
  `AGENTS.md`'s own `FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting()`
  precedent for the same philosophy applied elsewhere).
