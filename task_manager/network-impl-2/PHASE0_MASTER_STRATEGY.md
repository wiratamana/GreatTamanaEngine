# PHASE0 — Master Strategy: Vulkan Swapchain / Game-View PNG Screenshot over HTTP (`network-impl-2`)

This document is the **orchestrator**. It does not itself contain
implementation steps — it defines the goal, the current situation, the
locked design decisions (confirmed with the project owner before any phase
document below was written), and the map of child phase documents that carry
out the actual code changes, in order. Every child phase document follows the
same three-step shape (Goal / Situation / Plan) and must be executed in
numeric order — each phase's code depends on the previous one existing.

Read this file first. Then execute, in order:

- `PHASE1_THIRDPARTY_STB_IMAGE_WRITE_AND_PURE_ENCODING_UTILITIES.md`
- `PHASE2_CROSS_THREAD_FRAME_CAPTURE_BRIDGE.md`
- `PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md`
- `PHASE4_SWAPCHAIN_PIPELINED_CAPTURE_SERVICE.md`
- `PHASE5_GET_SWAPCHAIN_ENDPOINT_AND_FORMAT_NEGOTIATION_REUSE.md`
- `PHASE6_AUTOMATED_TESTS_DOCS_AND_REGRESSION_SAFETY.md`

Always re-read the previous phase's own completion report (write a short
`PHASEn_COMPLETION_REPORT.md` next to this file once that phase's code
compiles and its own tests pass — mirroring every other campaign in this
repository, e.g. `task_manager/network-impl-1/`, `task_manager/job_system/`)
before starting the next one — it may record a decision or a snag that
changes a later phase's exact plan.

---

## Step 1: The Goal (Where are we going?)

Give GreatTamanaEngine's already-working embedded HTTP server
(`task_manager/network-impl-1/`, `src/Network/`) its first **engine-state-
touching** endpoints: a remote caller can `GET` a PNG screenshot of what the
engine is currently rendering, over the same loopback-only HTTP server that
already answers `GET /http_hello_world`. Concretely, two new endpoints:

```
GET http://127.0.0.1:8080/get_swapchain   -> 200 OK, a PNG of the literal
                                              OS-window swapchain image that
                                              was actually just presented
                                              (in an Editor build, this is a
                                              screenshot of the whole Editor
                                              UI - dock panels, menu bar,
                                              chrome and all; in a release/
                                              no-Editor build, this is
                                              whatever Game rendered directly
                                              into the window).

GET http://127.0.0.1:8080/get_game_view   -> 200 OK, a PNG of the Game's own
                                              3D scene render target alone
                                              (the "Game view" RenderTexture
                                              an Editor build renders into
                                              and displays inside an ImGui
                                              panel) - available only when a
                                              Game view actually exists and
                                              is currently visible this
                                              session.
```

Both endpoints accept an optional `?format=` query parameter (or an
`Accept: application/json` request header) to choose between raw PNG bytes
(the default) and a JSON envelope carrying base64-encoded PNG data — see
Phase 3/5's own "Response format negotiation" section for the exact,
locked contract. This is deliberately the same shape of payload the
`load_image` tool (an unrelated, external AI-tooling project — see this
campaign's own originating conversation) already hands back to an LLM as a
base64 image block — this campaign's JSON envelope is what makes that same
consumption pattern possible directly from this engine's own HTTP server.

While the engine's own window keeps rendering/updating at full frame rate,
completely undisturbed by the HTTP request having arrived or being served —
exactly the same non-negotiable constraint `network-impl-1`'s own Goal
already established for `/http_hello_world`, now proven against a genuinely
harder case (an endpoint that actually needs live GPU pixel data, not a
canned string).

## Step 2: The Situation (Where are we now?)

- **`network-impl-1` already shipped a working, loopback-only, background-
  thread HTTP server** (`src/Network/NetworkServer.h/.cpp`,
  `NetworkRoutes.h/.cpp`), auto-started by `Application`'s constructor
  (`GTE_ENABLE_NETWORK`, default `ON`), bound to `127.0.0.1:8080`. See
  `AGENTS.md`, "Networking", and `task_manager/network-impl-1/
  PHASE0_MASTER_STRATEGY.md` for the full history. Every route handler
  registered today (`RegisterRoutes()` in `NetworkServer.cpp`) is a pure
  function of its own request data with **zero** access to any engine
  subsystem — `network-impl-1`'s own "Non-Goals" section explicitly deferred
  "an endpoint that needs to read/write engine state" to dedicated, future,
  reviewed work. **This campaign is that follow-on work.**
- **No screenshot/pixel-readback capability exists anywhere in the engine
  today.** There is no `vkCmdCopyImageToBuffer` call anywhere under `src/`,
  no host-visible readback `Buffer` usage, no PNG *encoder* (only a PNG/JPEG/
  etc. *decoder*, `third_party/stb/stb_image.h`, used for loading asset
  textures — see `cmake/FetchSTB.cmake`). This campaign adds the encode
  direction from scratch.
- **The engine renders through two fundamentally different regimes that
  both matter here**, discovered by direct inspection of
  `src/Application/Application.cpp` and `src/Renderer/FramePresenter.cpp`:
  - The **off-screen "Game view"/"Scene view" regime**
    (`Renderer::RenderOffscreen()` / `BeginOffscreenRenderGraphRecording()`
    + `EndOffscreenRenderGraphRecording()`) is **synchronous** — it already
    blocks until the GPU finishes before returning (see `Renderer.h`'s own
    doc comment: "Deliberately synchronous for now... blocks until the GPU
    finishes"). Reading pixels back out of a `RenderTexture` right after this
    call is comparatively simple: no multi-frame bookkeeping needed at all.
  - The **swapchain "Present" regime**
    (`Renderer::PresentViaRenderGraph()` → `FramePresenter::
    PresentViaRenderGraph()`) is **pipelined**, `kFramesInFlight == 2`
    (`FramePresenter.h`) — the CPU records/submits a frame and moves on
    without waiting for the GPU to finish it. The ONLY point this code
    already blocks on a specific frame-in-flight slot's own prior GPU work
    being complete is a single line at the very top of
    `FramePresenter::PresentViaRenderGraph()`:
    `vkWaitForFences(m_device, 1, &fence, ...)` where
    `fence = m_frameSync.InFlightFence(m_currentFrame)` (`FramePresenter.cpp`,
    ~line 355-356). This is the exact, pre-existing synchronization point a
    swapchain readback can piggyback on with **zero added GPU stall** — see
    Phase 4.
  - The swapchain image's own final layout transition
    (`ColorAttachmentWrite` → `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`) is done
    **manually, by hand, OUTSIDE the render graph itself**, in
    `FramePresenter::PresentViaRenderGraph()` right after `graph.Execute()`
    returns (`FramePresenter.cpp`, ~lines 415-432), using the render graph's
    own low-level `rg::RequiredStateFor()`/`rg::EmitImageBarrier()` helpers —
    with an explicit code comment explaining exactly why: *"the render graph
    itself never learns that THIS particular imported resource is about to be
    handed to `vkQueuePresentKHR`... so the swapchain image's final
    transition... is done here, by hand"*. **This is the established,
    precedented seam a swapchain pixel-readback copy must also hook into** —
    see Phase 4 for why a *render-graph pass* approach (an extra
    `AddPass()` call) does **not** work here (see below).
- **A render-graph pass that ONLY reads a resource (no write at all) is
  silently culled and never executes.** Direct inspection of
  `src/Renderer/RenderGraph/RenderGraphCompiler.cpp`'s `Compile()` confirms
  this: a pass is only ever marked "kept" if it (a) WRITES a texture that is
  itself in the caller's `finalOutputs` root set, or (b) is a
  backward-reachable predecessor of such a pass (i.e., something else's
  input depends on it). A hypothetical "SwapchainCapture" pass that only
  declares a `ResourceAccess::TransferSrc` **read** of the swapchain texture,
  with no write of its own, would never satisfy either condition and would
  simply never run — its `execute` callback would silently never be invoked.
  Confirming this **up front** is what steers this campaign away from trying
  to add a new render-graph pass for the capture (which would additionally
  require widening `RenderGraphCompiler::Compile()`'s/`RenderGraph::
  Execute()`'s root-detection to understand buffer-only outputs — real,
  invasive, cross-cutting surgery on a well-tested shared module) and
  towards the manual, graph-external approach described above instead —
  exactly mirroring the precedent the engine's own authors already
  established for the PRESENT_SRC_KHR transition itself.
- **`ResourceAccess::TransferSrc` already exists** in
  `RenderGraphTypes.h`'s `ResourceAccess` enum (unused today) — useful
  vocabulary/precedent even though this campaign ends up using the manual
  barrier helpers directly rather than the render graph's own pass
  declaration API (see above).
- **`Buffer`/`BufferMemoryUsage::GpuToCpu`** (`src/Renderer/Buffer.h`)
  already exists and is exactly the primitive a readback needs: "Host-
  visible memory, persistently mapped... optimized for the GPU writing and
  the CPU later reading it back (e.g. a query/readback buffer)." Nothing
  currently constructs a `GpuToCpu` buffer anywhere in the engine — this
  campaign is its first real consumer.
- **`GpuTimingService`** (`src/Renderer/GpuTimingService.h/.cpp`) already
  solves an *almost identical* cross-frame problem — reading back GPU-written
  data (timestamp query results) from the pipelined Present regime without
  adding a stall — via a documented, working pattern: `MarkPresentSlotWritten()`
  / `ReadPresentResultIfAvailable()`, keyed by frame-in-flight index, read
  the NEXT time that same slot comes back around (by which point the
  existing per-slot fence wait already guarantees the earlier GPU work is
  done). Phase 4 deliberately mirrors this exact pattern for pixel readback
  instead of inventing a new one.
- **The Network module's own thread-safety rule (`AGENTS.md`, "Networking")
  currently reads: "a route handler must be a PURE function of its own
  request data only — it must NEVER touch Registry/Renderer/Game/
  AssetDatabase/... directly or indirectly, full stop"**, with an explicit
  carve-out already anticipated: *"a future endpoint that genuinely needs
  engine data needs a dedicated, reviewed thread-safe bridge... never a raw
  pointer/reference into live engine state handed to a handler lambda."*
  This campaign is the FIRST to build that bridge (`FrameCaptureBridge` —
  Phase 2) and must update this `AGENTS.md` section to document it as the
  one sanctioned exception, not weaken the underlying rule for anything else.
- **`NetworkServer`'s constructor takes no arguments today**
  (`NetworkServer()`, registering its fixed route table from its own
  constructor body) and its existing tests
  (`tests/Network/NetworkServerTests.cpp`) construct it exactly that way in
  SEVEN separate places (verified directly against the live file — four
  locals literally named `server`, plus one each named `probe`, `collider`,
  `healthy` across the five `TEST(...)` bodies; an earlier draft of this
  document undercounted this as "five separate places" — always re-grep the
  live file rather than trusting this number if it ever changes again). Any
  change to `NetworkServer`'s public constructor signature must keep every
  one of those call sites compiling — see Phase 3's own locked decision (a
  defaulted pointer parameter) for how this campaign avoids touching that
  test file at all in earlier phases.
- **No JSON library is vendored**, and `network-impl-1` deliberately never
  added one ("No JSON (de)serialization" — an explicit Non-Goal, scoped to
  *request*-body parsing). This campaign's own JSON *output* is a single,
  fixed, three-field shape with no user-controlled string content that could
  need escaping (base64 text, plus two integers) — hand-formatting it is
  simpler and more consistent with this codebase's existing "roll it by hand
  when the shape is this small" philosophy (see `AGENTS.md`'s own math/ECS
  precedents) than pulling in a real JSON library for one call site.

## Step 3: The Plan (How do we get there?)

### Locked Design Decisions

These were confirmed with the project owner before any phase document below
was written, and MUST NOT be silently changed by a later phase without
updating this file first:

1. **Two endpoints, not one.** `GET /get_swapchain` captures the literal,
   currently-presented OS-window swapchain image (Editor UI and all, in an
   Editor build). `GET /get_game_view` captures the Game's own off-screen
   "Game view" `RenderTexture` alone (pure 3D scene, no ImGui chrome) —
   returns a clear, fast (not full-timeout) error if no Game view exists or
   is currently visible this session (see point 4).
2. **The swapchain capture is implemented "properly" against the real,
   pipelined Present regime** — a per-frame-in-flight readback buffer pair,
   read back one "round" later with **zero added GPU stall**, mirroring
   `GpuTimingService`'s existing Present-timing pattern (see Step 2 above and
   Phase 4). It is NOT a simplified wait-idle/stall-based shortcut.
3. **Response format is negotiable, not fixed**: `?format=png` (or no
   `?format=` at all and no matching `Accept` header) → raw bytes,
   `Content-Type: image/png`. `?format=base64` or `?format=json` (or an
   `Accept: application/json` header with no explicit `?format=` override) →
   a JSON body `{"width":<int>,"height":<int>,"format":"png","data_base64":"<...>"}`.
   `?format=` always wins over the `Accept` header when both are present.
   See Phase 3's "Response format negotiation" section for the exact pure
   function this logic lives in.
4. **Cross-thread contract**: the network route handler (background thread)
   never touches Vulkan/Renderer/Application state directly. It calls into
   one shared `FrameCaptureBridge` (Phase 2), which:
   - Immediately returns HTTP `503` (`"capture already in progress"`) if a
     request of the SAME kind (Swapchain vs. GameView) is already pending —
     never queues a second one.
   - Otherwise marks that kind "requested" and blocks (the network thread
     only — never the main thread) on a condition variable for up to a fixed
     **3 second** timeout. On success, returns the captured PNG bytes +
     width/height. On timeout, resets back to idle and the route responds
     HTTP `504`. If the main thread positively determines the requested
     target doesn't exist this frame (e.g. `/get_game_view` with no visible
     Game view, or the OS window currently minimized), it fails the request
     immediately with a dedicated reason instead of waiting out the full
     timeout, and the route responds HTTP `409`.
5. **New vendored dependency: `stb_image_write.h`** (PNG *encoder* — the
   write-side counterpart of the already-vendored `stb_image.h` *decoder*),
   fetched via a new `cmake/FetchSTBImageWrite.cmake`, mirroring
   `cmake/FetchSTB.cmake`'s exact shape (single-header, downloaded directly
   from GitHub's raw content endpoint, staged into `third_party/stb/`,
   pinned to a specific commit, its own `.gte_fetched_ref` marker, its own
   `stb_image_write` INTERFACE CMake target).
6. **No new CMake toggle.** This feature is gated entirely by the EXISTING
   `GTE_ENABLE_NETWORK` switch (default `ON`) — there is no
   `GTE_ENABLE_SCREENSHOT` or similar. The new pure encoding modules
   (Base64/pixel-conversion/PNG-encode — Phase 1) always compile
   unconditionally (same "class always compiles" precedent as every other
   toggleable subsystem in this engine — see `AGENTS.md`), and cost nothing
   when never called.
7. **Swapchain readback buffers are allocated LAZILY**, only the first time
   a `/get_swapchain` request is ever actually received this session — never
   unconditionally on startup — mirroring `FramePresenter`'s own existing
   `m_depthBuffers`/`EnsureDepthBuffersForSwapchain()` "only pay for it once
   it's actually needed" precedent.
8. **Loopback-only, no auth, no HTTPS — unchanged from `network-impl-1`.**
   This campaign inherits every one of `network-impl-1`'s own Non-Goals
   (no LAN exposure, no TLS, no API key) — it only adds two new `GET`
   routes to the exact same existing, already-reviewed server.

### Non-Goals (explicitly out of scope for `network-impl-2`)

- **No video/continuous streaming.** Each request captures exactly one
  still frame; there is no MJPEG/WebSocket streaming endpoint.
- **No configurable resolution/downscaling/cropping.** A captured image is
  always the full, current resolution of whichever target it captures —
  no query parameters to resize or crop the result.
- **No `POST`/request-body-driven behavior of any kind** — both endpoints
  are bodyless `GET`s, exactly like the existing `/http_hello_world`.
- **No Scene-view capture endpoint.** Only Game view and the literal
  swapchain are covered. A future `/get_scene_view` would be a small,
  symmetric addition once this campaign's `RenderTexture` capture path
  exists (Phase 3) but is not built here.
- **No Editor UI panel for triggering/previewing a capture.** This remains
  a purely HTTP-driven, headless-tooling feature.
- **No change to `network-impl-1`'s existing `/http_hello_world` route or
  its own tests** — this campaign only ADDS routes and, where unavoidable
  (`NetworkServer`'s constructor), does so via a backward-compatible
  default parameter, never a breaking signature change.

### Phase Map

| Phase | Deliverable |
|---|---|
| **1** | `cmake/FetchSTBImageWrite.cmake` (new dependency) + `src/Encoding/Base64.h/.cpp`, `src/Encoding/PixelConversion.h/.cpp`, `src/Encoding/PngEncoder.h/.cpp` — pure, Tier-1-testable, Vulkan-free utility functions: bytes→base64 text, BGRA8→RGBA8 conversion, RGBA8 pixel buffer→in-memory PNG bytes. |
| **2** | `src/Application/FrameCaptureBridge.h/.cpp` — the one reviewed, thread-safe cross-thread bridge a route handler is allowed to touch. Tier-1-testable with a fake producer/consumer thread pair, zero Vulkan/Renderer dependency. Updates `AGENTS.md`'s "Networking" section to document this as the sanctioned exception. |
| **3** | Renderer-level synchronous Game-view pixel readback (piggybacking on the already-synchronous offscreen regime) + `Application::Run()` wiring + the `GET /get_game_view` route (`NetworkRoutes.h/.cpp`, `NetworkServer.h/.cpp`) + the response-format-negotiation pure function, reused by Phase 5. First fully working end-to-end endpoint. |
| **4** | `src/Renderer/SwapchainCaptureService.h/.cpp` + `FramePresenter.cpp` integration — the harder, pipelined, frame-in-flight-aware swapchain pixel readback, mirroring `GpuTimingService`'s existing Present-timing pattern, with zero added GPU stall. |
| **5** | `GET /get_swapchain` route, wired to Phase 4's data, reusing Phase 3's format-negotiation helper untouched. |
| **6** | Automated tests for every new Tier-1 module + a real, end-to-end `NetworkServerTests.cpp`-style test hitting both routes over a real socket + `AGENTS.md`/`README.md` documentation + full build/`ctest` regression pass. |

### Definition of Done for the whole campaign

- `cmake --build build` succeeds (default `GTE_ENABLE_NETWORK=ON` config).
- Running the built `GreatTamanaEngine.exe` and issuing
  `GET http://127.0.0.1:8080/get_swapchain` and
  `GET http://127.0.0.1:8080/get_game_view` (from the same machine, in an
  Editor build with the Game view panel visible) each return a valid PNG
  (openable in any image viewer) while the engine window keeps rendering.
- The same two requests with `?format=base64` return a JSON body whose
  `data_base64` field, base64-decoded, is byte-for-byte identical to the
  `?format=png` response body.
- `ctest` (see the regression command below) passes, including every new
  `Encoding/`/`Application/`/`Renderer/`/`Network/` test file this campaign
  adds.
- `AGENTS.md` has an updated "Networking" section documenting
  `FrameCaptureBridge` as the sanctioned engine-state bridge; `README.md`'s
  "Status" section has a new bullet describing this feature.

### Regression / build commands (reference — see each phase for exact use)

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
