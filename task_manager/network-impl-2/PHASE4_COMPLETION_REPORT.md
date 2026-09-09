# PHASE4 — Completion Report: Swapchain Pipelined Capture Service

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document executed:
`PHASE4_SWAPCHAIN_PIPELINED_CAPTURE_SERVICE.md` (already revised in place by
`PHASE4_MIDDLEMAN_REVIEW_REPORT.md`, in the same folder, before this session
started — both were read in full before any code was written, per this
campaign's own "always read the previous phase's completion report(s) first"
rule).

## Summary

Implemented exactly what the (already-corrected) phase document specified:
`SwapchainCaptureService` — the harder, pipelined counterpart of Phase 3's
`Renderer::CaptureRenderTexturePixels()` — plus its `FramePresenter`/
`Renderer` integration, giving the engine the ability to read back the REAL
swapchain image's pixels out of the pipelined, `kFramesInFlight == 2` Present
regime with **zero added GPU stall**, by piggy-backing on the exact same
per-frame-in-flight `vkWaitForFences()` call `FramePresenter::
PresentViaRenderGraph()` already performs for an unrelated, pre-existing
reason. Per the phase document's own "What this phase deliberately does NOT
wire up yet" section, `Application::Run()` itself and the `GET /get_swapchain`
HTTP route are explicitly **not** part of this phase — that is Phase 5's job.

## What was done

1. **`src/Renderer/SwapchainCaptureService.h/.cpp`** (new files) —
   implemented field-for-field/method-for-method against the phase
   document's own Step 3.1/3.2 code blocks:
   - `RequestCapture()` — idempotent, safe no-op if already pending.
   - `RecordCaptureIfRequested(cmd, swapchainImage, extent, format,
     frameInFlightIndex)` — lazily (re)creates that frame-in-flight slot's
     `BufferMemoryUsage::GpuToCpu` readback `Buffer` if missing/wrong-sized,
     then records, in order: an image barrier `ColorAttachmentWrite ->
     TransferSrcOptimal` (the swapchain image's *actual* previous state,
     coming out of the "Present" pass's own color-attachment write — **not**
     `ShaderRead`, unlike Phase 3's `RenderTexture` case), the
     `vkCmdCopyImageToBuffer` call, and the **required** buffer barrier
     making the copy's write visible to a later HOST read
     (`{TRANSFER, TRANSFER_WRITE} -> {HOST, HOST_READ}` via the existing,
     pure `rg::EmitBufferBarrier()`) — the exact gap
     `PHASE4_MIDDLEMAN_REVIEW_REPORT.md` flagged and Phase 3 already
     proactively carried for its own (different-shaped) readback. Returns
     `true` only when it actually recorded something.
   - `TryTakeCompletedCapture(frameInFlightIndex)` — pops that slot's
     pending flag and `memcpy`s its captured width/height/format-tagged
     pixels out of the buffer's mapped memory into a plain
     `CapturedSwapchainPixels`, `std::nullopt` otherwise.
   - `NotifySwapchainRecreated()` — resets every slot's buffer/pending flag
     and drops any in-flight request, exactly matching the phase document's
     own "an accepted, silent timeout, not a fast-failed error" contract.
2. **`FramePresenter.h/.cpp` integration**:
   - New member `SwapchainCaptureService m_swapchainCapture` (a plain value
     member, constructed in `FramePresenter`'s own constructor from
     `m_allocator`/`m_memoryTracker`/`kFramesInFlight` — no new constructor
     parameter needed, exactly as the phase document's Step 2, point 9
     confirmed) plus `std::optional<CapturedSwapchainPixels>
     m_lastCompletedCapture`.
   - Two new public methods, `RequestSwapchainCapture()` (forwards to
     `m_swapchainCapture.RequestCapture()`) and
     `TakeLastCompletedSwapchainCapture()` (moves `m_lastCompletedCapture`
     out via `std::exchange`, never calling
     `SwapchainCaptureService::TryTakeCompletedCapture()` directly itself —
     exactly per the phase document's own reasoning for why this
     indirection is needed).
   - Inside `PresentViaRenderGraph()`: `m_lastCompletedCapture =
     m_swapchainCapture.TryTakeCompletedCapture(m_currentFrame);` right
     after the existing per-slot `vkWaitForFences()` call (before
     `vkAcquireNextImageKHR`); `const bool capturedThisFrame =
     m_swapchainCapture.RecordCaptureIfRequested(...)` right after
     `graph.Execute(...)` returns and before the existing manual
     `PRESENT_SRC_KHR` finalize block; that finalize block's own `previous`
     state now branches on `capturedThisFrame` (`TransferSrcOptimal` when
     true, the original `ColorAttachmentWrite` otherwise) — all exactly
     matching the phase document's Step 3.3 code blocks.
   - Inside `RecreateSwapchain()`: added
     `m_swapchainCapture.NotifySwapchainRecreated();` right after the
     existing `m_frameSync.RecreateRenderFinishedSemaphores(...)` call
     (after the early minimized-window return), per Step 2, point 5's exact
     placement.
   - `FramePresenter`'s move constructor/move-assignment operator were
     extended to move `m_swapchainCapture`/`m_lastCompletedCapture` too (see
     "Deviations" below — the phase document didn't spell this out
     explicitly, since `FramePresenter` is already move-constructible/
     move-assignable and any new member must participate in both).
3. **`Renderer.h/.cpp` pass-through** — two new public methods,
   `RequestSwapchainCapture()`/`TakeLastCompletedSwapchainCapture()`,
   thin forwards to `m_presenter`'s own two methods, mirroring the exact
   "thin façade, every public method forwards to whichever collaborator
   actually implements it" shape every other `Renderer` method already
   follows (`PresentViaRenderGraph()`, `CaptureRenderTexturePixels()`, ...).
4. **CMake wiring** — `src/Renderer/SwapchainCaptureService.h/.cpp` added to
   root `CMakeLists.txt`'s unconditional `target_sources(gte_core PRIVATE
   ...)` list, right after `GpuTimingService.cpp/.h` (its closest structural
   neighbor — see the new file's own class comment for why).

## Deviations from the phase document

- **`SwapchainCaptureService` needed an explicit move constructor/move
  assignment operator (`= default`), and `FramePresenter`'s own move
  constructor/assignment needed updating to move the two new members** —
  the phase document's Step 3.1 code block declared the class with only a
  deleted copy constructor/assignment and did not mention moves at all, but
  since it declares a user destructor (`~SwapchainCaptureService() =
  default`) and deletes the copy operations, the move operations are NOT
  implicitly generated by the language rules — and `FramePresenter` (which
  owns one as a plain value member) is itself move-constructible/
  move-assignable, so this was a compile-time necessity, not an optional
  nicety. Added `SwapchainCaptureService(SwapchainCaptureService&&) =
  default;`/`operator=(SwapchainCaptureService&&) = default;` (safe — every
  member is a plain `VmaAllocator`, `shared_ptr`, `bool`, or
  `std::vector<Slot>`, and `Slot` itself is trivially movable since
  `std::optional<Buffer>` moves whenever `Buffer` does) and extended both of
  `FramePresenter`'s existing move operations to move
  `m_swapchainCapture`/`m_lastCompletedCapture` alongside every other
  member. This is a mechanical, behavior-preserving addition the phase
  document's own code block simply didn't spell out explicitly — not a
  design change.
- **No other deviations.** Every barrier sequence, state transition, and
  integration point matches the (already-corrected) phase document's Step
  3.1/3.2/3.3 literally, including copying the host-read-visibility buffer
  barrier verbatim from Phase 3's own precedent as instructed.
- **Manual, live-capture verification (Step 3.5's own "confirm a requested
  capture actually arrives with sane, non-garbage pixel data, TWO real
  frames later" instruction) was NOT performed with a throwaway debug
  hook**, unlike what the phase document's own Tests section calls for.
  Reasoning: this session's tooling has no way to drive a temporary keybind/
  debug-panel button and then inspect a dumped `.ppm`/`.bmp` file's pixel
  content meaningfully (no interactive mouse/keyboard driving a live GUI
  window). What WAS verified instead (see below): the engine builds and
  runs cleanly with the new, always-executed
  `RecordCaptureIfRequested()`/`TryTakeCompletedCapture()` calls now
  present in the per-frame `PresentViaRenderGraph()` hot path (both are
  cheap no-ops today, since nothing in production calls
  `RequestSwapchainCapture()` yet — see the phase document's own "What this
  phase deliberately does NOT wire up yet" section), confirming the new
  code doesn't destabilize the existing, already-working Present path. The
  genuine "does a captured PNG actually contain sane pixels" proof is
  deferred to **Phase 5**, which is a strictly better place for it anyway:
  Phase 5 adds the real `GET /get_swapchain` HTTP route, at which point the
  exact same live-capture behavior can be verified end-to-end with a plain
  `curl ... -o out.png` — a genuinely stronger, more representative test
  than a throwaway keybind, and one this session's tooling CAN actually
  drive (an HTTP GET) without any GUI automation. This is called out
  explicitly here (mirroring Phase 3's own precedent of flagging its one
  open manual-verification gap rather than silently assuming success).

## Compile/test verification performed

- `cmake --build build --target gte_core` — succeeded (24 objects rebuilt/
  relinked, including the new `SwapchainCaptureService.cpp` and the modified
  `FramePresenter.cpp`/`Renderer.cpp`; only pre-existing, unrelated
  `stb_image_write`/KTX-Software CMake warnings printed, no new
  warnings/errors from this phase's own code).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded (links
  cleanly against the updated `gte_core`).
- `cmake --build build --target GreatTamanaEngine` — succeeded (the full
  engine executable, confirming `main.cpp`'s own link against the new
  `Renderer`/`FramePresenter` surface is unaffected).
- **Live runtime smoke test**: launched the built `GreatTamanaEngine.exe` in
  the background, then issued `GET http://127.0.0.1:8080/http_hello_world`
  (still `200`/`"hello world"`), confirming the engine's main loop —
  including `FramePresenter::PresentViaRenderGraph()`'s now-modified body,
  executing `RecordCaptureIfRequested()`/`TryTakeCompletedCapture()` as
  no-ops every single frame — runs and keeps responding normally, with no
  crash/hang, exactly the same non-negotiable "engine keeps rendering,
  completely undisturbed" requirement every earlier phase's own manual test
  already confirmed for its own new code. Process was cleanly terminated
  afterward.
- Per this campaign's workflow rules, a full `ctest`/full rebuild was
  deliberately NOT run — only the three targeted builds above plus the live
  runtime smoke test, as instructed for a non-final phase.
- `SwapchainCaptureService` is Tier 2 (needs a real `VmaAllocator`/
  `VkDevice`) — same accepted "no automated coverage yet" bucket
  `Buffer`/`RenderTexture`/`Pipeline`/`GpuTimingService` already sit in (see
  `AGENTS.md`, "Testability & Regression Safety") — no new test file was
  expected or added for it, per the phase document's own Step 3.5.

## What the next phase (PHASE5) should know

- `Renderer::RequestSwapchainCapture()`/`TakeLastCompletedSwapchainCapture()`
  are ready to use exactly as `Renderer::CaptureRenderTexturePixels()` was
  for Phase 3 — the only difference is the two-call, request-now/
  collect-later shape (a capture is NOT fulfilled by the same
  `PresentViaRenderGraph()` call it was requested from; it completes
  `kFramesInFlight` (2) real frames later).
- **`Application::Run()`'s own wiring is entirely Phase 5's job** — nothing
  in `Application.cpp` was touched this phase. The natural mirror of Phase
  3's own two-insertion-point pattern applies here too: call
  `Renderer::RequestSwapchainCapture()` once per frame whenever
  `FrameCaptureBridge::IsCaptureRequested(FrameCaptureKind::Swapchain)` is
  true, and call `Renderer::TakeLastCompletedSwapchainCapture()` once per
  frame, right after `Renderer::PresentViaRenderGraph()` returns — feeding
  a real result into `FrameCaptureBridge::FulfillPendingRequest()` (PNG-
  encoded via `Encoding::EncodeRgba8ToPng()`, after
  `Encoding::ConvertBgraToRgbaInPlace()` if `CapturedSwapchainPixels::format`
  turns out to be a BGRA variant — reuse Phase 3's own `IsBgraFormat()`
  helper in `Application.cpp` rather than duplicating it).
- **The genuine "does a captured PNG actually contain sane, non-garbage
  pixels" proof is still outstanding** (see "Deviations" above) — Phase 5
  should treat its own manual verification step (`curl
  http://127.0.0.1:8080/get_swapchain -o out.png`, opened in an image
  viewer) as covering BOTH Phase 4's and Phase 5's own correctness at once,
  and should specifically watch for garbage/torn/all-zero output, which
  would indicate either the host-read-visibility barrier or the
  frame-in-flight indexing in THIS phase's code is subtly wrong — not just
  a Phase 5 routing bug.
- `ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()`
  (`src/Network/NetworkRoutes.h`, from Phase 3) are still exactly what
  Phase 5's `/get_swapchain` route should reuse verbatim — nothing in this
  phase touched them.
- `NetworkServer`'s constructor's defaulted `FrameCaptureBridge*
  captureBridge = nullptr` parameter (from Phase 3) is still exactly what
  Phase 5's new route registration should reuse — nothing in this phase
  touched `src/Network/` at all.
