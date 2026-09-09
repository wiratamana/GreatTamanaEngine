# PHASE5 — `GET /get_swapchain` Endpoint

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 2
(`FrameCaptureBridge`), Phase 3 (`ResolveCaptureResponseFormat()`/
`BuildCaptureJsonBody()`/the `/get_game_view` route to mirror), Phase 4
(`Renderer::RequestSwapchainCapture()`/`TakeLastCompletedSwapchainCapture()`).
This is a deliberately SMALL, mostly-mechanical phase — all the hard
correctness work already happened in Phase 4; this phase is "plumb it
through to HTTP", following Phase 3's own already-proven pattern almost
line-for-line.

## Step 1: The Goal (Where are we going?)

Wire Phase 4's `SwapchainCaptureService` into `Application::Run()` and
`FrameCaptureBridge`, and add the second and final route,
`GET /get_swapchain`, reusing Phase 3's response-format-negotiation helpers
completely unchanged.

## Step 2: The Situation (Where are we now?)

- Phase 4 left `Renderer::RequestSwapchainCapture()` and
  `Renderer::TakeLastCompletedSwapchainCapture()` fully implemented and
  manually verified to correctly produce raw pixel data, two frames after a
  request, with no HTTP/bridge wiring on top yet.
- Phase 3 already established the exact per-frame `Application::Run()`
  pattern this phase mirrors: check `IsCaptureRequested(kind)`, ask the
  Renderer to do the work, convert BGRA→RGBA if needed, encode to PNG, call
  `FulfillPendingRequest()`. The ONLY structural difference here is that the
  "ask the Renderer to do the work" step is now split across TWO calls in
  TWO different places in the frame (`RequestSwapchainCapture()` must be
  called BEFORE `PresentViaRenderGraph()` runs this frame, so Phase 4's
  `RecordCaptureIfRequested()` sees `m_captureRequested == true` in time;
  `TakeLastCompletedSwapchainCapture()` is checked AFTER
  `PresentViaRenderGraph()` returns, since that's when a capture recorded
  potentially several frames ago might finally have arrived) — rather than
  Phase 3's single, same-call-site "do it all synchronously right here"
  shape.
- `Application::Run()`'s existing Present block already exists at a fixed,
  known location (~lines 399-452 as currently written, inside the
  `{ GTE_PROFILE_SCOPE("Renderer::PresentViaRenderGraph"); ... }` block) —
  re-read the live file before editing.

## Step 3: The Plan

### 3.1 — `Application::Run()` wiring

Immediately BEFORE the existing
`presentStats = m_renderer.PresentViaRenderGraph(...)` call:

```cpp
if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::Swapchain)) {
    m_renderer.RequestSwapchainCapture();
}
```

(A plain, unconditional check every frame — cheap, matches
`IsCaptureRequested()`'s own "cheap, side-effect-free read" doc comment.
`RequestSwapchainCapture()` itself is idempotent/safe to call every frame
while a request is pending, per Phase 4's own `RequestCapture()` doc
comment — no harm in calling it more than once before it's actually
consumed.)

**Cross-phase consistency note (deliberately safe from Phase 3's own
placement gotcha):** unlike the Game-view wiring (`PHASE3`'s own Step 3.2,
"IMPORTANT — a placement gotcha"), this `IsCaptureRequested(Swapchain)` check
is placed directly in `Run()`'s own top-level body, NOT nested inside any
`if (gameTarget != nullptr || sceneTarget != nullptr)`-style conditional — it
runs unconditionally every single frame, in every build configuration
(Editor or release), regardless of whether a Game/Scene view exists this
frame. The matching `TakeLastCompletedSwapchainCapture()` check below is
equally unconditional. There is therefore no equivalent "this branch can
never be reached in a release build" risk here — confirm this remains true
if `Run()`'s surrounding structure is ever refactored.

Immediately AFTER `m_renderer.PresentViaRenderGraph(...)` returns (whether
it returned a value or not — a capture can complete even on a call where
`PresentViaRenderGraph()` itself returns `std::nullopt`, e.g. because THIS
frame's own new Present pass was skipped for an unrelated reason like a
just-recreated swapchain, since the completed-capture check happens near
the TOP of `FramePresenter::PresentViaRenderGraph()`, before any such early
return — confirm this ordering directly against Phase 4's actual
implementation before assuming it, since an early-return path added
carelessly in `FramePresenter::PresentViaRenderGraph()` could accidentally
skip the `TryTakeCompletedCapture()` call too; if Phase 4's real
implementation DOES have such a gap, note it here and prefer moving the
`TryTakeCompletedCapture()` call earlier in that function rather than
weakening this phase's own wiring to compensate):

```cpp
if (std::optional<CapturedSwapchainPixels> raw = m_renderer.TakeLastCompletedSwapchainCapture()) {
    if (IsBgraFormat(raw->format)) {
        Encoding::ConvertBgraToRgbaInPlace(raw->pixels.data(), raw->width, raw->height);
    }
    std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw->pixels.data(), raw->width, raw->height);
    m_captureBridge.FulfillPendingRequest(FrameCaptureKind::Swapchain,
        CapturedPngImage{ std::move(png), raw->width, raw->height });
}
```

(`IsBgraFormat()` — reuse the exact same small helper Phase 3 added, do not
duplicate it; hoist it to file-scope in `Application.cpp` if Phase 3 left it
as a lambda local to the Game-view block.)

Unlike the Game-view path (Phase 3), there is **no** "target not available"
fast-fail branch needed here for a minimized window — a minimized window
already makes `PresentViaRenderGraph()` return `std::nullopt` every frame
with nothing recorded at all (see `FramePresenter::PresentViaRenderGraph()`'s
own early `if (m_pendingWidth <= 0 ...) return false;` guard) — a pending
swapchain-capture request during that time simply never gets recorded
(Phase 4's `RecordCaptureIfRequested()` is never even called that frame) and
naturally times out via `FrameCaptureBridge`'s existing fixed timeout,
exactly like `PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decision #4
already anticipates ("What should happen if the main thread doesn't produce
a frame in time... Block with a fixed timeout"). Do not add a redundant
`FailPendingRequest(TargetNotAvailable)` call for this specific case — a
minimized window is expected to eventually be un-minimized and is not a
structurally-permanent "this can never succeed" condition the way a hidden
Game-view panel's absence is treated in Phase 3 (a Game view CAN be made
visible again too, in principle, but this phase deliberately keeps the two
endpoints' failure-fast policies independent rather than forcing them to
match — a swapchain capture request against a minimized window is simply
allowed to time out like any other slow-to-arrive frame).

### 3.2 — `NetworkServer`/`NetworkRoutes` wiring for `/get_swapchain`

Add a second route to `RegisterRoutes()` in `NetworkServer.cpp`, IDENTICAL
in shape to Phase 3's `/get_game_view` route, with only
`FrameCaptureKind::GameView` replaced by `FrameCaptureKind::Swapchain` and
the path replaced by `/get_swapchain`. If the two route lambdas end up
byte-for-byte identical apart from that one enum value and path string,
factor out a single shared helper function,

```cpp
void RegisterCaptureRoute(httplib::Server& server, const char* path, FrameCaptureKind kind, FrameCaptureBridge* captureBridge);
```

called twice from `RegisterRoutes()` (once per path/kind) — this is a
reasonable, small DRY refactor at this point (two real, live call sites
now exist, which is exactly when this codebase's own conventions elsewhere
consider extracting a shared helper worthwhile, e.g. `BonePoseMath.h`'s
`ComputeBoneLocalMatrix()` being pulled out once a SECOND caller needed the
identical logic) — do NOT extract this preemptively before Phase 5 if Phase
3 was written first without anticipating it; either phase order is fine, but
only extract once both call sites genuinely exist and are provably
identical apart from their two parameters.

### 3.3 — What NOT to do in this phase

- Do not touch `ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()` —
  reused completely unchanged from Phase 3.
- Do not add any new HTTP status code beyond the three already established
  in Phase 3 (`503`/`504`/`409`) — `/get_swapchain` uses the exact same
  mapping.
- Do not add a Scene-view route — out of scope (see
  `PHASE0_MASTER_STRATEGY.md`'s Non-Goals).

### 3.4 — Tests

- Extend `tests/Network/NetworkRoutesTests.cpp` only if `RegisterCaptureRoute()`
  (3.2) exposes any NEW pure logic beyond what Phase 3 already covers — in
  practice, there should be none; this phase's route wiring itself is Tier
  2 (needs a live `httplib::Server` at minimum, and ultimately a live
  `Renderer`/window for a true end-to-end check) and is covered by Phase 6's
  dedicated end-to-end test instead.
- Manual verification: build, run `GreatTamanaEngine.exe`,
  `curl http://127.0.0.1:8080/get_swapchain -o swap.png` and open it —
  confirm it shows the actual current window contents (the Editor's dock
  layout, in an Editor build) rather than a stale/garbage/black image. Also
  verify `curl "http://127.0.0.1:8080/get_swapchain?format=base64"` returns
  a JSON body whose `data_base64` field, decoded (e.g. via
  `certutil -decode` on Windows or any online base64 tool), is byte-for-byte
  the same PNG as the raw-bytes response.
