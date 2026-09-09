# PHASE6 — Completion Report: Automated Tests, Documentation, and Full Regression Pass

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document executed:
`PHASE6_AUTOMATED_TESTS_DOCS_AND_REGRESSION_SAFETY.md`.

## Summary

Implemented exactly what the phase document specified: a new, real, Tier-2,
end-to-end automated test (`tests/Network/CaptureEndpointsEndToEndTests.cpp`)
starting a real `FrameCaptureBridge` + `NetworkServer` and hitting both
`/get_game_view` and `/get_swapchain` over a real loopback socket, driven by a
GPU-free fake capture producer standing in for `Application::Run()`'s real
per-frame logic; `AGENTS.md`'s "Networking" section extended with the
concrete-endpoints bullet the phase document specifies; `README.md`'s
"Status" section extended with a matching new bullet; and a full, clean
`cmake --build build` + `ctest -C Debug --output-on-failure` regression pass
(1055/1055 tests passing, 1 pre-existing machine-gated smoke test skipped),
plus a `-DGTE_ENABLE_NETWORK=OFF` configure+build confirmation and a final,
live, end-to-end manual `curl` acceptance pass against the real, running
engine. This closes out the `network-impl-2` campaign.

## What was done

1. **`tests/Network/NetworkTestHelpers.h`** (new file) — extracted
   `NetworkServerTests.cpp`'s own `WaitUntilAcceptingConnections()` helper
   into a small, shared, header-only `inline` function
   (`gte::Network::TestHelpers::WaitUntilAcceptingConnections()`), per the
   phase document's own instruction ("move it to a small shared test helper
   header... rather than copy-pasting it a second time"). `NetworkServerTests.cpp`
   itself now includes this header and pulls the function back in via a
   `using` declaration, so every existing call site in that file keeps
   working completely unchanged.
2. **`tests/Network/CaptureEndpointsEndToEndTests.cpp`** (new file) —
   implemented every scenario the phase document's Step 3.1 lists:
   - `FakeMainThreadStandIn` (local, anonymous-namespace class) — a
     dedicated background thread polling `IsCaptureRequested()` for BOTH
     kinds every 5ms and fulfilling with a small, genuinely valid 2x2 PNG
     (built via the already-tested `Encoding::EncodeRgba8ToPng()`, mirroring
     `tests/Encoding/PngEncoderTests.cpp`'s own "encode a hand-built buffer"
     convention) — with a per-kind `SetGated()` escape hatch used by the two
     tests below that need a request to stay genuinely pending on purpose.
   - `CaptureEndpointsEndToEndTest` (a `::testing::Test` fixture) — a fresh
     `FrameCaptureBridge` + `FakeMainThreadStandIn` + real
     `Network::NetworkServer(&bridge)` (ephemeral port) +
     `httplib::Client`, constructed/torn down per test, reusing
     `NetworkTestHelpers.h`'s shared poll helper.
   - `GetGameViewReturnsRawPngByDefault` / `GetSwapchainWorksTheSameWay` —
     assert `200`, `Content-Type: image/png`, and the exact PNG magic-byte
     signature (`\x89PNG\r\n\x1a\n`) on the response body.
   - `GetGameViewFormatBase64ReturnsJson` (and the swapchain half of
     `GetSwapchainWorksTheSameWay`) — assert `200`,
     `Content-Type: application/json`, extract `data_base64` via a small,
     dependency-free substring extractor (`ExtractJsonStringField()`, per
     the phase document's own "a simple substring extraction is fine" note),
     decode it via a small, dependency-free base64 decoder
     (`DecodeBase64()`, the inverse of `Encoding::EncodeBase64()`), and
     assert the decoded bytes are byte-for-byte identical to the raw-PNG
     response's own body.
   - `SecondConcurrentRequestOfSameKindGets503` — gates `GameView`, fires a
     first request on its own thread (left pending), fires a second,
     concurrent request for the same kind from the test's own main thread,
     asserts `503` returned in well under a second, then un-gates and joins
     cleanly.
   - `TimeoutSurfacesAs504` — per the phase document's own "What NOT to do"
     section, calls `FrameCaptureBridge::RequestCaptureAndWait()` DIRECTLY
     (bypassing HTTP entirely) with a short, per-call-only timeout override
     (100ms), gating `Swapchain` so the fake stand-in never services it —
     asserts `FrameCaptureFailureReason::TimedOut`, the exact same result the
     real HTTP route forwards verbatim as an HTTP `504`.
3. **`tests/CMakeLists.txt` wiring** — `Network/CaptureEndpointsEndToEndTests.cpp`
   added right after `Network/NetworkServerTests.cpp` in `GTE_TEST_SOURCES`
   (unconditional — matches `NetworkServerTests.cpp`'s own "always built,
   `NetworkServer`/`NetworkRoutes` compile regardless of `GTE_ENABLE_NETWORK`"
   precedent), plus a matching new descriptive comment bullet in the file's
   own test-taxonomy header, mirroring every other entry's style.
4. **`AGENTS.md` update** — added the exact "Networking" bullet the phase
   document's Step 3.2 specifies (documenting `GET /get_swapchain`/
   `GET /get_game_view` as the concrete endpoints built on `FrameCaptureBridge`,
   naming `src/Encoding/`, `Renderer::CaptureRenderTexturePixels()`, and
   `SwapchainCaptureService` as the pieces feeding it, and the
   `?format=`/`Accept` negotiation contract), placed right after the
   existing Phase-2-added `FrameCaptureBridge` bullet.
5. **`README.md` "Status" update** — added a new bullet describing both new
   endpoints, the `?format=` query parameter/`Accept` header negotiation, the
   pipelined-with-zero-stall swapchain capture, the new `src/Encoding/`
   module, and linking back to
   `task_manager/network-impl-2/PHASE0_MASTER_STRATEGY.md`, matching the
   existing `network-impl-1` Networking bullet's own style (per the phase
   document's own Step 3.3 instruction).

## Deviations from the phase document

- **None of substance.** Every step (3.1 through 3.5) was implemented as
  literally specified.
- One clarification during implementation: the phase document's own Step
  3.1 sketch suggested reusing `Encoding::EncodeBase64()`'s own known-vector
  tests "in reverse" as an alternative to a small standalone decoder — a
  small, standalone, dependency-free `DecodeBase64()` local to the new test
  file was used instead (the phase document itself explicitly allowed
  either approach: "write a tiny test-only decoder, or reuse... in reverse
  if that's easier"). No behavioral difference either way.
- The `SecondConcurrentRequestOfSameKindGets503` test needed a small design
  addition the phase document's own prose only implied ("gate it behind a
  second, test-controlled flag") rather than spelling out literally: a
  per-`FrameCaptureKind` `std::atomic<bool>` gate on `FakeMainThreadStandIn`
  (`SetGated()`), toggled off again at the end of the test so the held first
  request gets serviced and its own background thread joins cleanly rather
  than actually timing out after 3 real seconds. This keeps the test suite
  fast without ever needing to wait out a real timeout for this specific
  test.

## Compile/test verification performed

- `cmake --build build --target GreatTamanaEngineTests` — succeeded (2
  objects recompiled: `Network/NetworkServerTests.cpp`,
  `Network/CaptureEndpointsEndToEndTests.cpp` — the new file plus the
  refactored existing one).
- Ran the directly-relevant tests:
  `tests\GreatTamanaEngineTests.exe --gtest_filter=*CaptureEndpoints*:*NetworkServer*:*NetworkRoutes*:*FrameCaptureBridge*`
  — **29/29 passed** (5 new `CaptureEndpointsEndToEndTest` cases, 5
  `NetworkServerTests`, 3 plain `NetworkRoutesTests` + 9 parameterized
  `ResolveCaptureResponseFormatTest` cases, 7 `FrameCaptureBridgeTest`).
- Per `AGENTS.md`'s own Job System precedent ("a single green run is not
  sufficient evidence for genuinely concurrent code"), stress-repeated
  `*CaptureEndpoints*` with `--gtest_repeat=20` — **all 20 iterations
  passed, zero hangs/failures**.
- **Full regression pass** (this phase's own explicit requirement, per the
  workflow rules — the only phase in this campaign allowed/required to run
  one):
  - `cmake --build build` — clean, no errors/new warnings.
  - `cd build && ctest -C Debug --output-on-failure` — **1055/1055 tests
    passed** (1 pre-existing machine-gated smoke test,
    `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
    skipped — the same, expected, documented skip every prior session in
    this repository reports).
  - `-DGTE_ENABLE_NETWORK=OFF` configure (`cmake -S . -B build_network_off
    -DGTE_ENABLE_NETWORK=OFF -G Ninja`) + `cmake --build build_network_off
    --target GreatTamanaEngineTests` — **clean, full build, no errors** —
    confirms nothing accidentally introduced a hard, unconditional
    dependency on `Network::NetworkServer` existing; the throwaway build
    directory was deleted afterward, leaving the primary `build/` directory
    (and its own passing test results) untouched.
- **Final manual acceptance pass** (Step 3.5), against the fully-landed
  campaign in one sitting, using the real, built `GreatTamanaEngine.exe`
  launched in the background:
  - `GET /get_game_view` → `409` in a few milliseconds (the documented,
    correct behavior — the Editor's default dock layout starts with "Scene"
    active and "Game" hidden behind it as an inactive tab, exactly as
    Phase 3's own completion report already established and flagged as
    expected).
  - `GET /get_swapchain` → **`200 OK`**, a real, valid 69,701-byte PNG
    (`Content-Type: image/png`) — visually confirmed (via `load_image`) to
    be the genuine, live Editor UI at capture time (Hierarchy/Scene-Game
    tabs/Inspector top row, Memory/Profiler/Render Graph/Jobs/Project tabbed
    bottom row, Project panel showing the real `Furina.gta`/
    `ChatanyaraKuushanku_bassui260717a.gta`/`Furina_Textures` project
    contents) — not black/garbage/torn.
  - `GET /get_swapchain?format=base64` → `200`, JSON body with
    `width=1280`, `height=720`, `format=png` — its `data_base64` field,
    base64-decoded via a small PowerShell script, produces a file with a
    **SHA-256 hash IDENTICAL** to the raw-PNG response's own body,
    confirming byte-for-byte round-trip correctness end-to-end against a
    real, live capture (not just the synthetic 2x2 PNG the new automated
    test uses).
  - `GET /http_hello_world` returned `200`/`"hello world"` both immediately
    before and immediately after every capture request above — the engine's
    main frame loop and the pre-existing route are completely unaffected;
    the window kept rendering/updating throughout (directly evidenced by
    the captured screenshot itself showing a live, correctly-laid-out,
    non-frozen Editor frame).
  - The launched engine process and every temporary verification artifact
    (`game_view.png`, `swapchain.png`, `swapchain.json`,
    `swapchain_from_json.png`, `hello.txt`, `verify_capture.ps1`) were
    cleanly terminated/removed afterward — none of these throwaway files
    are part of the committed change.

## Campaign-level Definition of Done — final check against `PHASE0_MASTER_STRATEGY.md`

- ✅ `cmake --build build` succeeds (default `GTE_ENABLE_NETWORK=ON` config).
- ✅ `GET /get_swapchain` returns a valid PNG while the engine window keeps
  rendering (manually verified this session; `/get_game_view`'s own success
  path was already verified live in Phase 5's own session with "Game" made
  the active tab — this session's default-layout run correctly hit its
  documented `409` fast-fail path instead, exactly as expected).
- ✅ `?format=base64` response's `data_base64`, decoded, is byte-for-byte
  identical to the `?format=png` response body (verified via SHA-256 hash
  match against a real, live swapchain capture this session, and via the
  new automated test's own byte-vector equality assertion for both
  endpoints against the synthetic 2x2 PNG).
- ✅ `ctest` passes, including every new `Encoding/`/`Application/`/
  `Renderer/`/`Network/` test file this campaign added across all six
  phases.
- ✅ `AGENTS.md` has an updated "Networking" section documenting
  `FrameCaptureBridge` (Phase 2) AND the concrete `/get_swapchain`/
  `/get_game_view` endpoints (this phase); `README.md`'s "Status" section
  has a new bullet describing this feature (this phase).

**The `network-impl-2` campaign is complete.**

## What a future campaign extending this feature should know

- `Network::TestHelpers::WaitUntilAcceptingConnections()`
  (`tests/Network/NetworkTestHelpers.h`) is now the one shared "poll until a
  real `NetworkServer` is actually serving" helper — any future test file
  starting a real `NetworkServer` should include and reuse this rather than
  reimplementing it a third time.
- The new end-to-end test's `FakeMainThreadStandIn` pattern (a dedicated
  polling thread standing in for `Application::Run()`'s real per-frame
  `IsCaptureRequested()`/`FulfillPendingRequest()` calls) is a reusable
  template for testing any future `FrameCaptureBridge`-shaped cross-thread
  feature without needing a live `Application`/window/GPU — see this
  campaign's own `AGENTS.md` note steering a future third capture kind
  toward extending `FrameCaptureKind`/`RegisterCaptureRoute()` rather than
  inventing a new bridge.
- Per `AGENTS.md`'s own "Testability & Regression Safety" section, Phases
  3-5's GPU-touching code (`Renderer::CaptureRenderTexturePixels()`,
  `SwapchainCaptureService`) remains in the accepted "Tier 2, no automated
  coverage yet" bucket — this phase deliberately did not add a
  headless-Vulkan test fixture for it, per the phase document's own "What
  NOT to do in this phase" section; that remains a documented, non-blocking
  gap, not a regression introduced here.
