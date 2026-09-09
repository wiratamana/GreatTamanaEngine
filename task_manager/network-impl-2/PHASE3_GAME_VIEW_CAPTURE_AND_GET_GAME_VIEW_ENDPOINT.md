# PHASE3 — Game-View Synchronous Capture + `GET /get_game_view` Endpoint

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 1 (encoding
utilities), Phase 2 (`FrameCaptureBridge`).

## Step 1: The Goal (Where are we going?)

Ship the FIRST fully working, end-to-end endpoint: `GET /get_game_view`,
returning a PNG (or JSON+base64) of the Game view `RenderTexture`'s current
pixels. This deliberately comes BEFORE the harder swapchain capture (Phase
4) because the Game view rendering path is already **synchronous**
(`Renderer::RenderOffscreen()`/`EndOffscreenRenderGraphRecording()` already
blocks until the GPU finishes) — proving out the ENTIRE plumbing (Vulkan
readback → pixel conversion → PNG encode → bridge → network route → HTTP
response, including the format-negotiation logic BOTH endpoints share) once,
against the easy case, before Phase 4 tackles the pipelined swapchain case
on top of already-known-working plumbing.

## Step 2: The Situation (Where are we now?)

- `Application::Run()` (`src/Application/Application.cpp`) already computes
  `RenderTexture* gameTarget = m_editorLayer->GameViewTarget();` once per
  frame (~line 240) — `nullptr` whenever no Game view exists this frame
  (release build with no Editor compiled in at all, OR an Editor build where
  the "Game" dock tab is currently hidden/inactive — see `EditorContext`'s
  `gameViewVisible` flag, `AGENTS.md`'s "Editor Module Structure").
  **This is exactly the "target not available" case `FrameCaptureBridge::
  FailPendingRequest(FrameCaptureKind::GameView, TargetNotAvailable)` exists
  for** (Phase 2).
- After `m_renderer.EndOffscreenRenderGraphRecording()` returns
  (~line 341 of `Application.cpp`), `gameTarget`'s color image is guaranteed
  fully rendered AND already transitioned to
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` — see
  `FinalizeRenderTextureForExternalSampling(offscreenCmd, *gameTarget)`,
  called just before `EndOffscreenRenderGraphRecording()` (~line 328). This
  is the exact point in the frame where a Game-view capture must happen: the
  GPU has already, synchronously, finished drawing it (no fence-wait
  needed — `EndOffscreenRenderGraphRecording()`'s own underlying
  `FramePresenter::EndOffscreenRecording()` already calls
  `vkWaitForFences(...)` before returning, per `FramePresenter.cpp`
  ~line 338), and Dear ImGui hasn't sampled it yet this frame (a capture
  reading, rather than writing, the image is safe to interleave anywhere
  after this point without disturbing ImGui's own later read).
- `Renderer::ImmediateSubmit(recordFn)` (`Renderer.h`) already exists:
  "Records a one-time-submit command buffer... submits it to the graphics
  queue, and blocks until it finishes" — exactly the primitive a SECOND,
  independent, on-demand copy (outside the frame's own normal recording)
  needs. `Renderer::CreateBuffer(size, usage, BufferMemoryUsage::GpuToCpu,
  debugName)` is the matching factory for the host-visible destination.
- `RenderTexture::Image()`/`Format()`/`Extent()` (`RenderTexture.h`) already
  expose everything needed to know what to copy: `VkImage`, `VkFormat`
  (defaults to `VK_FORMAT_B8G8R8A8_UNORM` — see `RenderTexture.h`'s own
  constructor default, and `Renderer::CreateRenderTexture()`'s `format`
  parameter, which the Editor's Game view construction call resolves via
  `VK_FORMAT_UNDEFINED` → `Renderer::ColorFormat()`, itself whatever
  `VulkanSwapchain::ChooseSurfaceFormat()` actually negotiated at runtime —
  in practice this is essentially always a `*_B8G8R8A8_*` variant on this
  engine's supported GPUs, but the capture code must READ the real format
  rather than assume it, and only call `Encoding::ConvertBgraToRgbaInPlace()`
  when the reported format is actually a BGRA variant), and `VkExtent2D`.
- The render graph's own low-level barrier helpers (used by
  `FramePresenter.cpp`'s manual PRESENT_SRC_KHR finalize block — see
  `PHASE0_MASTER_STRATEGY.md`'s Step 2) —
  `rg::RequiredStateFor(ResourceAccess, isDepthResource)` and
  `rg::EmitImageBarrier(cmd, image, subresourceRange, previousState,
  nextState)` — are declared in a header reachable from
  `src/Renderer/RenderGraph/` (locate the exact header via
  `RequiredStateFor`'s own declaration — likely
  `RenderGraphBarrierPlanner.h` or similar; confirm by grep before use) and
  are plain, Vulkan-header-only pure functions with no render-graph-instance
  dependency — safe to call directly from a hand-written `ImmediateSubmit()`
  body with no `RenderGraphBuilder`/`RenderGraph` object involved at all.
  Reuse them here rather than hand-writing a THIRD copy of the same
  `VkImageMemoryBarrier2` boilerplate `FramePresenter.cpp` already has one
  copy of and Phase 4 will need a second.

## Step 3: The Plan

### 3.1 — `Renderer::CaptureRenderTexturePixels()`

Add a new public method to `Renderer` (`Renderer.h`/`Renderer.cpp`):

```cpp
// Renderer.h, near CreateRenderTexture()
struct CapturedRawPixels {
    std::vector<std::uint8_t> pixels; // tightly packed, width*height*4 bytes
    int width = 0;
    int height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED; // the format `pixels` is ACTUALLY in - caller decides whether a BGRA->RGBA swizzle is needed.
};

// Synchronously reads back `texture`'s CURRENT color pixels (whatever it
// was last rendered to - the caller is responsible for calling this only
// once it independently knows the texture's contents are final for this
// frame, e.g. right after Renderer::RenderOffscreen()/
// EndOffscreenRenderGraphRecording() returns for it). Uses a throwaway,
// on-demand host-visible readback Buffer + Renderer::ImmediateSubmit() -
// this is a genuinely EXTRA GPU submission/wait beyond the frame's own
// normal recording, acceptable ONLY because this is invoked at most once
// per network request, never unconditionally every frame (see
// Application::Run()'s own IsCaptureRequested() guard).
//
// texture must currently be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
// (the state Renderer::RenderOffscreen() always leaves a RenderTexture in)
// - this call transitions it to TRANSFER_SRC_OPTIMAL, copies it, and
// transitions it back to SHADER_READ_ONLY_OPTIMAL before returning, so a
// later ImGui sample of the SAME texture this same frame is unaffected.
CapturedRawPixels CaptureRenderTexturePixels(RenderTexture& texture) const;
```

Implementation (`Renderer.cpp`):

1. `const VkExtent2D extent = texture.Extent();`
   `const VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * 4;`
2. Create a throwaway `Buffer readback = CreateBuffer(size,
   VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferMemoryUsage::GpuToCpu,
   "CaptureReadback");` (local variable — destroyed automatically at the end
   of this function; no need to keep it alive longer since
   `ImmediateSubmit()` already blocks until the GPU finishes with it).
3. `ImmediateSubmit([&](VkCommandBuffer cmd) { ... });` recording, inside the
   lambda:
   - Transition `texture.Image()`'s color subresource
     (`VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }`)
     from `rg::RequiredStateFor(ResourceAccess::ShaderRead, false)` to a
     `TransferSrcOptimal` state (mirror the exact `ResourceState` shape
     `FramePresenter.cpp`'s manual finalize block already constructs by
     hand for `PRESENT_SRC_KHR` — build the equivalent for
     `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` /
     `VK_PIPELINE_STAGE_2_TRANSFER_BIT` / `VK_ACCESS_2_TRANSFER_READ_BIT`)
     via `rg::EmitImageBarrier(cmd, texture.Image(), range, previous, next)`.
   - `VkBufferImageCopy region{}; region.imageSubresource = {
     VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; region.imageExtent = { extent.width,
     extent.height, 1 };` (offsets default to 0 — this is a whole-image
     copy). `vkCmdCopyImageToBuffer(cmd, texture.Image(),
     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.Native(), 1, &region);`
   - Transition back: `TransferSrcOptimal` → the SAME `ShaderRead` state
     it started in, via a second `rg::EmitImageBarrier()` call.
4. After `ImmediateSubmit()` returns (already blocked until GPU-complete):
   `std::memcpy` `size` bytes out of `readback.MappedData()` into the
   result's `pixels` vector (never keep `readback` itself, or a pointer into
   its mapped memory, alive past this function — it is destroyed at scope
   exit).
5. Return `{ pixels, (int)extent.width, (int)extent.height, texture.Format() }`.

### 3.2 — Application-side wiring

In `Application.h`: add a `FrameCaptureBridge m_captureBridge;` member,
declared BEFORE `Network::NetworkServer m_networkServer;` (so it is
constructed first and destroyed last relative to it — matches
`Application.h`'s own existing "declared in dependency order" convention).

In `Application.cpp`'s constructor, pass `&m_captureBridge` into
`m_networkServer`'s constructor call (see Phase 5 for the exact
`NetworkServer` signature change — this phase only needs to know the
pointer exists and gets threaded through; the ACTUAL route registration for
`/get_game_view` is added in this phase's own step 3.4 below, since it's the
first endpoint, while `/get_swapchain` itself is deferred to Phase 5).

**IMPORTANT — a placement gotcha, confirmed by direct inspection of the live
`Application.cpp`, that would otherwise silently break the fast-fail path in
exactly the case it exists to handle.** `gameTarget`
(`RenderTexture* gameTarget = m_editorLayer->GameViewTarget();`, ~line 240)
is followed by one single outer block,
`if (gameTarget != nullptr || sceneTarget != nullptr) { ... }` (~lines
248-355 as currently written) — NOT a standalone `if (gameTarget != nullptr)`
block as an earlier draft of this document described. Both
`FinalizeRenderTextureForExternalSampling(offscreenCmd, *gameTarget);`
(~line 328) and `m_renderer.EndOffscreenRenderGraphRecording();` (~line 341)
live INSIDE that outer block, which only ever runs at all when
`gameTarget != nullptr || sceneTarget != nullptr`. Confirmed directly against
`src/Editor/NullEditorLayer.cpp`: **in a release build with no Editor
compiled in, `GameViewTarget()`/`SceneViewTarget()` both unconditionally
return `nullptr`, every frame, for the entire process lifetime** — so that
whole outer block NEVER runs in such a build (the same is true, rarer, in an
Editor build where both the "Game" and "Scene" dock tabs are simultaneously
hidden). If the "target not available" fast-fail branch were placed anywhere
inside that outer block, it would never execute in precisely the scenario it
exists to handle — every `/get_game_view` request against a release build
would then silently fall through to the bridge's full 3-second timeout
(HTTP `504`) instead of the intended fast, immediate HTTP `409`. To avoid
this, split the new logic into TWO separate insertions, at two different,
independent points in `Run()`:

1. **The fast-fail branch — placed immediately after `gameTarget` itself is
   computed (~line 240), unconditionally, BEFORE the
   `if (gameTarget != nullptr || sceneTarget != nullptr)` block — so it runs
   every single frame regardless of whether that block ends up executing at
   all this frame:**

   ```cpp
   RenderTexture* gameTarget = m_editorLayer->GameViewTarget();
   RenderTexture* sceneTarget = m_editorLayer->SceneViewTarget();

   if (gameTarget == nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
       m_captureBridge.FailPendingRequest(FrameCaptureKind::GameView, FrameCaptureFailureReason::TargetNotAvailable);
   }
   ```

2. **The success-path capture — stays inside the existing offscreen block,
   right after `m_renderer.EndOffscreenRenderGraphRecording();` returns
   (~line 341, before that same block's own closing brace at ~line 355).**
   This placement needs no change: it is already guarded by
   `gameTarget != nullptr`, which by construction only evaluates true on a
   frame where the outer `if` was ALSO already true (i.e. the block's own
   body, including `EndOffscreenRenderGraphRecording()`, is guaranteed to
   have actually run this frame):

   ```cpp
   if (gameTarget != nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
       Renderer::CapturedRawPixels raw = m_renderer.CaptureRenderTexturePixels(*gameTarget);
       if (IsBgraFormat(raw.format)) { // small local helper - see below
           Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
       }
       std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
       m_captureBridge.FulfillPendingRequest(FrameCaptureKind::GameView,
           CapturedPngImage{ std::move(png), raw.width, raw.height });
   }
   ```

   (`CaptureRenderTexturePixels()` needs its OWN separate `ImmediateSubmit()`
   call — a fresh command buffer, fresh fence — NOT `offscreenCmd`, which is
   still open/unsubmitted earlier in the function — so it must run AFTER
   `EndOffscreenRenderGraphRecording()` has already been called and returned,
   i.e. once the frame's own offscreen submission is complete and idle.)

`IsBgraFormat(VkFormat)` — a tiny local free function/lambda in
`Application.cpp` (or a one-line addition to a shared small helper if one
already exists for format checks) returning true for
`VK_FORMAT_B8G8R8A8_UNORM`/`VK_FORMAT_B8G8R8A8_SRGB`. **Confirmed directly
against the live `VulkanSwapchain.cpp`'s `ChooseSurfaceFormat()`: it
explicitly prefers `VK_FORMAT_B8G8R8A8_UNORM` +
`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`, but FALLS BACK to `formats.front()`
(whatever surface format the platform/driver happens to report first) if
that exact preferred combination isn't available** — so the two hardcoded
values above are the realistic, commonly-negotiated case on this engine's
actually-tested GPUs/drivers, not a Vulkan-spec-guaranteed exhaustive list.
Should that fallback ever actually trigger on some future GPU/driver with a
channel order this two-value check doesn't recognize, `IsBgraFormat()`
returning `false` is only safe if the real format already happens to be
RGBA-ordered — an unrecognized BGRA-like variant would silently produce a
channel-swapped (red/blue reversed) PNG with no error at all. This is an
accepted, narrow risk (out of scope to fully generalize here — see
`PHASE0_MASTER_STRATEGY.md`'s Non-Goals), but leave a one-line comment
at the `IsBgraFormat()` definition site itself noting this, so a future
"screenshot has wrong colors" bug report has an obvious first place to look.

### 3.3 — Response-format negotiation (shared by BOTH endpoints)

Add to `src/Network/NetworkRoutes.h/.cpp` (pure, httplib-independent, per
that file's own existing convention):

```cpp
// NetworkRoutes.h
enum class CaptureResponseFormat { RawPng, JsonBase64 };

// Resolves which shape a capture endpoint's response should take, given the
// request's own `?format=` query parameter value (empty string if absent)
// and `Accept` header value (empty string if absent) - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #3 for the exact,
// locked precedence rules this must implement:
//   - queryFormat == "png"            -> RawPng
//   - queryFormat == "base64"/"json"  -> JsonBase64
//   - queryFormat is anything else non-empty -> RawPng (an unrecognized
//     value is NOT an error - falls back to the default, same spirit as
//     this engine's other "unknown-value falls back to a safe default"
//     precedents, e.g. GpuTimingSample's own tri-state resolution)
//   - queryFormat is empty AND acceptHeader contains "application/json"
//     (a simple substring check - real Accept headers can have multiple,
//     weighted values; this engine only ever needs the simple case) -> JsonBase64
//   - otherwise (queryFormat empty, Accept doesn't ask for JSON) -> RawPng
CaptureResponseFormat ResolveCaptureResponseFormat(const std::string& queryFormat, const std::string& acceptHeader);

// Builds the JSON body for the JsonBase64 response shape - a small, fixed,
// hand-formatted JSON object (see PHASE0_MASTER_STRATEGY.md's own
// "no JSON library" decision): {"width":<int>,"height":<int>,"format":"png","data_base64":"<...>"}
// `base64Png` must already be valid base64 text (see Encoding::EncodeBase64)
// - this function does no escaping of it (base64's own alphabet contains no
// character that needs JSON-string escaping).
std::string BuildCaptureJsonBody(int width, int height, const std::string& base64Png);
```

`NetworkServer.cpp`'s route lambda is what extracts the raw strings from
`httplib::Request` (`req.get_param_value("format")`,
`req.get_header_value("Accept")`) and passes them into
`ResolveCaptureResponseFormat()` — never inline the precedence logic itself
into the lambda; the lambda stays a thin wiring layer exactly like every
existing route in this file.

### 3.4 — `NetworkServer`/`NetworkRoutes` wiring for `/get_game_view`

- `NetworkServer`'s constructor gains a new, DEFAULTED parameter:
  `explicit NetworkServer(FrameCaptureBridge* captureBridge = nullptr);`
  — this keeps every existing `gte::Network::NetworkServer server;` call
  site in `tests/Network/NetworkServerTests.cpp` compiling completely
  unchanged (Locked Design Decision — see `PHASE0_MASTER_STRATEGY.md`'s
  Step 2, "NetworkServer's constructor takes no arguments today"). Store it
  as a private `FrameCaptureBridge* m_captureBridge = nullptr;` member (a
  non-owning pointer — `NetworkServer` never owns it, `Application` does).
- `RegisterRoutes()` (`NetworkServer.cpp`'s anonymous namespace) gains a new
  parameter, `FrameCaptureBridge* captureBridge`, and a new route:

```cpp
server.Get("/get_game_view", [captureBridge](const httplib::Request& req, httplib::Response& res) {
    if (captureBridge == nullptr) {
        res.status = 503;
        res.set_content("capture bridge not available", "text/plain; charset=utf-8");
        return;
    }
    FrameCaptureBridge::RequestResult result = captureBridge->RequestCaptureAndWait(FrameCaptureKind::GameView);
    if (result.alreadyPending) {
        res.status = 503;
        res.set_content("capture already in progress", "text/plain; charset=utf-8");
        return;
    }
    if (result.failure.has_value()) {
        res.status = (*result.failure == FrameCaptureFailureReason::TimedOut) ? 504 : 409;
        res.set_content("capture failed", "text/plain; charset=utf-8");
        return;
    }
    const CapturedPngImage& image = *result.image;
    const CaptureResponseFormat format = ResolveCaptureResponseFormat(
        req.get_param_value("format"), req.get_header_value("Accept"));
    if (format == CaptureResponseFormat::RawPng) {
        res.set_content(reinterpret_cast<const char*>(image.pngBytes.data()), image.pngBytes.size(), "image/png");
    } else {
        const std::string base64 = Encoding::EncodeBase64(image.pngBytes);
        res.set_content(BuildCaptureJsonBody(image.width, image.height, base64), "application/json");
    }
});
```

  (`NetworkRoutes.h`'s own doc comment already establishes "every future
  endpoint's own response-computation logic must be added here the same
  way" — `ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()` above
  ARE that logic; the lambda itself stays as thin as every other route.)
- `NetworkServer`'s constructor forwards `m_captureBridge` into
  `RegisterRoutes(m_impl->server, m_captureBridge);`.
- `Application`'s constructor now constructs `m_networkServer` as
  `Network::NetworkServer(&m_captureBridge)` — update the member
  initializer list accordingly (`Application.h`'s member declaration order
  already puts `m_captureBridge` — added in step 3.2 above — before
  `m_networkServer`, so this is safe).

### What NOT to do in this phase

- Do not implement `/get_swapchain` yet — Phase 4/5's job. `ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()` built here are REUSED verbatim by Phase 5, not reimplemented.
- Do not add a Scene-view capture endpoint (out of scope — see `PHASE0_MASTER_STRATEGY.md`'s Non-Goals).
- Do not have `CaptureRenderTexturePixels()` keep the readback `Buffer` alive across calls / turn it into a per-`Renderer` persistent member — it is deliberately a fresh, throwaway allocation per call (this endpoint is invoked at most a handful of times per debugging session, never per-frame — allocation cost here is irrelevant, and a persistent buffer would need its own resize-on-Game-view-resize bookkeeping for no real benefit).

### 3.5 — Tests

- `tests/Network/NetworkRoutesTests.cpp`: table-driven tests for
  `ResolveCaptureResponseFormat()` covering every precedence rule enumerated
  in step 3.3 above, plus `BuildCaptureJsonBody()`'s exact output shape for
  a known width/height/base64 string (assert the literal expected JSON
  string, including field order and absence of extraneous whitespace, so a
  future accidental reformat is caught).
- A real, end-to-end `tests/Network/NetworkServerTests.cpp`-style test is
  deferred to Phase 6 (needs a live `Renderer`/window, i.e. Tier 2 — not
  appropriate for this phase's otherwise Tier-1-only test additions); this
  phase's own manual verification is: build, run `GreatTamanaEngine.exe`
  with the Editor's "Game" panel visible, `curl http://127.0.0.1:8080/get_game_view -o out.png`
  and open `out.png` in an image viewer.
