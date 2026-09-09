# PHASE3 — Completion Report: Game-View Synchronous Capture + `GET /get_game_view` Endpoint

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document executed:
`PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md`.

## Summary

Implemented exactly what the phase document specified: the FIRST fully
working, end-to-end endpoint, `GET /get_game_view`, returning a PNG (or
JSON+base64) snapshot of the Editor's "Game view" `RenderTexture`. This
proves the entire plumbing chain — Vulkan readback → pixel conversion → PNG
encode → `FrameCaptureBridge` → network route → HTTP response, including the
response-format-negotiation logic Phase 5's `/get_swapchain` will reuse
verbatim — against the easy, already-synchronous offscreen rendering regime,
before Phase 4 tackles the harder pipelined swapchain case.

## What was done

1. **`Renderer::CaptureRenderTexturePixels()`** (`src/Renderer/Renderer.h`/
   `.cpp`) — a new public method returning a `Renderer::CapturedRawPixels`
   (`pixels`/`width`/`height`/`format`). Implementation follows the phase
   document's own step 3.1 exactly: a throwaway `BufferMemoryUsage::GpuToCpu`
   readback `Buffer` (`"CaptureReadback"`), a single `ImmediateSubmit()` call
   that (a) transitions the texture's color image from `ShaderRead` to
   `TransferSrc` via `rg::EmitImageBarrier()`/`rg::RequiredStateFor()`
   (reusing `RenderGraphBarrierPlanner.h`'s existing pure helpers, per the
   phase document's own instruction to avoid a third hand-rolled barrier copy),
   (b) `vkCmdCopyImageToBuffer()`, (c) transitions the texture back to
   `ShaderRead`, then a final `std::memcpy` out of the readback buffer's
   `MappedData()`.
2. **Deviation (a deliberate, defensive addition, not a plan change): an
   explicit host-read-visibility buffer barrier.** The phase document itself
   did not call for this, but `PHASE4_MIDDLEMAN_REVIEW_REPORT.md` (already
   present in this folder, reviewing Phase 4's *own* near-identical
   readback pattern) flags this exact class of gap as a genuine Vulkan
   correctness bug: a fence wait alone (`ImmediateSubmit()`'s own wait)
   guarantees the GPU write finished *executing*, but the Vulkan spec still
   requires an explicit memory dependency targeting
   `VK_ACCESS_2_HOST_READ_BIT`/`VK_PIPELINE_STAGE_2_HOST_BIT` before that
   write is guaranteed *visible* to a later host read. Since Phase 3's
   readback is mechanically the same shape (GPU write into a `GpuToCpu`
   buffer, then a CPU read after a fence wait) as the one the middleman
   review examined for Phase 4, I added the identical fix here too, one
   extra `rg::EmitBufferBarrier()` call (`{TRANSFER, TRANSFER_WRITE}` →
   `{HOST, HOST_READ}`) immediately after `vkCmdCopyImageToBuffer()` — using
   the already-existing, already-pure `EmitBufferBarrier()`/`ResourceState`
   half of `RenderGraphBarrierPlanner.h`, no new machinery. This is
   *forward-looking* consistency with Phase 4, not a fix for an observed bug
   in this phase (this engine's driver/memory type combination would very
   likely have "worked" without it, per the review's own note on why this
   class of bug is easy to miss) — flagging it explicitly here so Phase 4's
   own implementer knows Phase 3 already carries this pattern and can treat
   it as the (now real, not just reviewed-on-paper) precedent to copy.
3. **`Application::m_captureBridge`** (`src/Application/Application.h`) — a
   new `FrameCaptureBridge` member, declared right after `m_game` and before
   `Network::NetworkServer m_networkServer` (constructed first/destroyed
   last relative to it, as the phase document requires).
   `Application::Application()`'s constructor now passes `&m_captureBridge`
   into `m_networkServer`'s constructor.
4. **`Application::Run()` wiring** (`src/Application/Application.cpp`),
   split into the exact TWO separate insertion points the phase document's
   own "placement gotcha" section calls for:
   - The **fast-fail branch**, placed immediately after
     `gameTarget`/`sceneTarget` are computed, *unconditionally*, before the
     `if (gameTarget != nullptr || sceneTarget != nullptr)` block — so it
     runs every frame regardless of whether that block executes at all this
     frame (confirmed this placement is load-bearing: a release build, or
     an Editor build with both "Game"/"Scene" hidden, never enters that
     block, and the manual verification below empirically hit exactly this
     branch, fast, every time).
   - The **success-path capture**, inside the existing offscreen block,
     immediately after `m_renderer.EndOffscreenRenderGraphRecording()`
     returns (before the pre-existing `FinalizeSynchronousGpuTiming()`
     call) — guarded by `gameTarget != nullptr`, which only evaluates true
     on a frame where the enclosing block (and thus
     `EndOffscreenRenderGraphRecording()`) already ran this frame.
   - A new local `IsBgraFormat(VkFormat)` helper (anonymous namespace,
     alongside `AspectRatioOf()`/`ToProfilingGpuSampleStatus()`) recognizing
     `VK_FORMAT_B8G8R8A8_UNORM`/`_SRGB`, with the phase document's own
     "narrow, accepted risk" comment attached verbatim.
5. **Response-format negotiation** (`src/Network/NetworkRoutes.h`/`.cpp`) —
   `CaptureResponseFormat` (`RawPng`/`JsonBase64`), `ResolveCaptureResponseFormat()`
   implementing the exact locked precedence rules (query `?format=` always
   wins; unrecognized non-empty value falls back to `RawPng`; otherwise the
   `Accept` header's `"application/json"` substring decides), and
   `BuildCaptureJsonBody()` producing the exact fixed JSON shape
   `{"width":<int>,"height":<int>,"format":"png","data_base64":"<...>"}`.
6. **`NetworkServer`/`NetworkRoutes` wiring for `/get_game_view`**
   (`src/Network/NetworkServer.h`/`.cpp`) — `NetworkServer`'s constructor
   gained a defaulted `FrameCaptureBridge* captureBridge = nullptr`
   parameter (every one of the seven existing no-argument
   `tests/Network/NetworkServerTests.cpp` call sites keeps compiling
   unchanged — verified, see below), stored as a private non-owning
   `m_captureBridge` member and forwarded into `RegisterRoutes()`. The new
   `/get_game_view` route is a thin wiring lambda exactly matching the
   phase document's own snippet: `503` when `captureBridge == nullptr` or a
   same-kind request is already pending, `504`/`409` on timeout/
   target-not-available, otherwise the negotiated PNG/JSON body.
   `NetworkServer.h` forward-declares `gte::FrameCaptureBridge` (mirroring
   its existing `httplib::Server` forward-declare) so this header stays
   cheap for any consumer that doesn't need the bridge's own type directly;
   `NetworkServer.cpp` includes `"../Application/FrameCaptureBridge.h"` (the
   one real cross-Network/Application include this campaign's own design
   requires) and `"../Encoding/Base64.h"`.
7. **Tests** (`tests/Network/NetworkRoutesTests.cpp` — this file already
   existed from Phase 1/2's own CMake wiring but only covered
   `HandleHelloWorld()`; extended it in place) — a table-driven
   `INSTANTIATE_TEST_SUITE_P` covering all eight `ResolveCaptureResponseFormat()`
   precedence cases the phase document's step 3.5 enumerates (`?format=png`
   wins over `Accept: application/json`; `base64`/`json` both map to
   `JsonBase64`; an unrecognized `?format=bogus` falls back to `RawPng` even
   with a JSON `Accept`; empty `?format=` defers to the `Accept` header;
   both empty falls back to `RawPng`), plus two `BuildCaptureJsonBody()`
   tests asserting the exact literal JSON string (including field order and
   no extraneous whitespace) for both a normal and a degenerate
   empty-base64/zero-size case.

## Deviations from the phase document

- **The host-read-visibility buffer barrier described in item 2 above** is
  the one real, deliberate addition beyond the phase document's own literal
  text — added because `PHASE4_MIDDLEMAN_REVIEW_REPORT.md` (already sitting
  in this same folder, reviewing Phase 4's near-identical pattern) flagged
  this exact gap as a genuine correctness bug, and Phase 3's own readback is
  mechanically the same shape. Not fixing a bug *found* in Phase 3 — Phase 3
  had not been implemented yet — but proactively applying a fix the
  project's own later-phase review already proved necessary, rather than
  implementing a known-incomplete Vulkan barrier sequence and waiting for
  Phase 4 to "discover" the same gap a second time.
- **`tests/Network/NetworkRoutesTests.cpp` already existed** (with only
  `HandleHelloWorldReturnsExactContractedString` in it) from this
  campaign's earlier CMake wiring — extended in place rather than created
  fresh; no `tests/CMakeLists.txt` change was needed since it was already
  registered.
- No other deviations — every other step (3.1 through 3.4) was implemented
  as literally specified, including the exact two-insertion-point
  `Application::Run()` placement the phase document's own "placement
  gotcha" section calls out.

## Compile/test verification performed

- `cmake --build build --target gte_core` — succeeded (23 objects
  recompiled/relinked, including the modified `Renderer.cpp`,
  `Application.cpp`, `NetworkServer.cpp`, `NetworkRoutes.cpp`).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded.
- Ran the directly-relevant tests:
  `tests\GreatTamanaEngineTests.exe --gtest_filter=*NetworkRoutes*:*NetworkServer*:*FrameCaptureBridge*`
  — **24/24 passed** (7 `FrameCaptureBridgeTest`, 5 `NetworkServerTests`, 3
  plain `NetworkRoutesTests`, 9 parameterized
  `ResolveCaptureResponseFormatTest` cases).
- Built the full `GreatTamanaEngine.exe` and ran it as a live manual smoke
  test (per the phase document's own step 3.5):
  - `GET /http_hello_world` still returns `200`/`"hello world"` — the
    pre-existing route is completely unaffected.
  - `GET /get_game_view` (and `?format=json`) returned `409` ("capture
    failed") in **~14 ms**, not a 3-second timeout — this is the CORRECT,
    documented behavior: by default, the Editor's dock layout starts with
    "Scene" as the active tab and "Game" hidden behind it as an inactive
    tab (`ImGuiEditorLayer.cpp`'s own pre-existing `GameViewTarget()`
    comment: *"Not visible last frame (inactive tab behind 'Scene'...)"*),
    so `gameTarget == nullptr` this session and the fast-fail branch fires
    immediately, exactly as designed — this is direct, empirical proof the
    fast-fail placement fix (this phase document's own "placement gotcha")
    actually works: the request returned in ~14 ms rather than hanging for
    the bridge's full 3-second timeout.
  - The window/engine kept running/responding throughout (confirmed via the
    still-`200` `/http_hello_world` call issued right after) — the
    non-negotiable "engine keeps rendering, completely undisturbed by the
    HTTP request" requirement holds.
  - **Not verified end-to-end in this session**: the actual PNG-bytes
    success path (would require clicking the "Game" tab to the front in the
    live Editor UI first, which this session's tooling has no mouse/UI
    automation for). The success-path code itself was read back
    line-by-line against `Renderer.h`/`Buffer.h`/`RenderGraphBarrierPlanner.h`'s
    real, confirmed signatures (mirroring exactly how `FinalizeRenderTextureForExternalSampling()`
    already does the identical `ColorAttachmentWrite`⇄`ShaderRead` half of
    this dance in `RenderPasses.cpp`), and compiles cleanly, but a live
    "the Game tab is the active/visible one" run was not performed. **The
    next phase (or a future manual QA pass) should click "Game" to the
    front once, in a live Editor session, and re-run
    `curl http://127.0.0.1:8080/get_game_view -o out.png` to confirm the
    PNG opens correctly** — this is the one genuine gap in this phase's own
    manual verification, called out explicitly rather than silently assumed
    to work.
- Per this campaign's workflow rules, a full `ctest`/full rebuild was
  deliberately NOT run — only the targeted `gte_core`/`GreatTamanaEngineTests`
  builds above, the directly-relevant tests, and the live manual smoke test,
  as instructed for a non-final phase.

## What the next phase (PHASE4) should know

- `Renderer::CaptureRenderTexturePixels()` is ready to use as a reference
  pattern for the swapchain path too (Phase 4's own harder, pipelined case)
  — in particular, **copy this phase's host-read-visibility buffer barrier
  verbatim**; `PHASE4_MIDDLEMAN_REVIEW_REPORT.md`'s own correction #3
  already calls for the exact same fix independently, so Phase 4's
  implementer should treat this phase's `Renderer.cpp` as a second, now-real
  (not just reviewed-on-paper) precedent for it, not reinvent the barrier
  shape from scratch.
- `NetworkServer`'s constructor now takes a defaulted
  `FrameCaptureBridge* captureBridge = nullptr` — Phase 5's `/get_swapchain`
  route is added into the SAME `RegisterRoutes()` function/lambda list,
  reusing this same parameter (no further constructor signature change
  needed).
- `ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()`
  (`src/Network/NetworkRoutes.h`) are ready to be reused **verbatim** by
  Phase 5's `/get_swapchain` route — do not reimplement or copy this logic
  a second time.
- The one open manual-verification gap noted above (the actual PNG
  success path against a visible "Game" tab) should ideally be closed
  before/alongside Phase 6's own final end-to-end test pass, if not sooner
  by a quick manual click-and-curl session.
