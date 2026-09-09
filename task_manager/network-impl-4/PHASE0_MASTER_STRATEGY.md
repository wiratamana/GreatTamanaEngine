# PHASE0 — Master Strategy: `GET /get_texture` — Capture Any Named Engine Texture by Name (`network-impl-4`)

> **Second-iteration audit note (this revision):** this file was re-checked
> against the live source tree. Several factual inaccuracies/gaps were found
> and corrected in place — see the inline call-outs below (search this file
> for "AUDIT NOTE" if you already read a previous revision and want to find
> exactly what changed). The most important one: **the "graph-external
> manual finalize" correctness gap (Locked Design Decision 7) actually
> affects FOUR existing named textures today, not two** — `"Swapchain"` and
> `"BlurredSceneOutput"` need the exact same correction-hook treatment as
> `"GameView"`/`"SceneView"`, or `/get_texture` would emit an incorrect
> barrier (wrong assumed previous layout) the first time either of those two
> is ever requested. Phase 3/4's own documents must be updated to match
> before they are (re-)executed — see that phase's own file for whether this
> has already been done.

This document is the **orchestrator**. It does not itself contain
implementation steps — it defines the goal, the current situation, the
locked design decisions (confirmed with the project owner before any phase
document below was written — see "Locked Design Decisions" below, each one
traceable to an explicit `ask_questions` answer), and the map of child phase
documents that carry out the actual code changes, in order. Every child
phase document follows the same three-step shape (Goal / Situation / Plan)
and must be executed in numeric order — each phase's code depends on the
previous one existing.

Read this file first. Then execute, in order:

- `PHASE1_DEBUG_TEXTURE_REGISTRY_CORE_DATA_MODEL.md`
- `PHASE2_RENDERGRAPH_INTEGRATION_AND_AUTO_REGISTRATION.md`
- `PHASE3_GENERIC_IMAGE_READBACK_AND_DEPTH_VISUALIZATION.md`
- `PHASE4_FRAMECAPTUREBRIDGE_NAMED_TEXTURE_SUPPORT.md`
- `PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md`
- `PHASE6_TESTS_DOCS_AND_REGRESSION_SAFETY.md`

Always re-read the previous phase's own completion report (write a short
`PHASEn_COMPLETION_REPORT.md` next to this file once that phase's code
compiles and its own tests pass — mirroring every other campaign in this
repository, e.g. `task_manager/network-impl-2/`, `task_manager/network-impl-3/`)
before starting the next one — it may record a decision or a snag that
changes a later phase's exact plan.

---

## Step 1: The Goal (Where are we going?)

`network-impl-2` gave this engine's embedded HTTP server the ability to
capture two FIXED, hardcoded render targets as PNG: the literal OS-window
swapchain (`GET /get_swapchain`) and the Editor's off-screen "Game view"
`RenderTexture` (`GET /get_game_view`). This campaign generalizes that same
capability to **any texture the render graph knows about, looked up by its
own human-readable name, decided entirely at request time** — so a future
multi-pass effect (the stated motivating example: atmosphere scattering,
with intermediate LUTs like a Transmittance LUT or a Sky-View LUT that never
appear on screen on their own) can be visually debugged the exact same way,
with **zero extra glue code required at the time that feature is built** —
the moment a pass author writes `builder.CreateTexture("TransmittanceLUT",
desc)` (or `ImportTexture(...)`), that texture becomes automatically
requestable over HTTP, forever, with no separate registration step.

Concretely, one new endpoint (plus one small companion discovery endpoint —
see Locked Design Decision 3):

```
GET http://127.0.0.1:8080/get_texture?texture_name=<name>[&channel=color|depth][&format=png|base64|json]
    -> 200 OK, a PNG of the NAMED texture's current pixels (whichever
       RenderGraph texture handle was declared this session under the
       EXACT string `name` passed to RenderGraphBuilder::CreateTexture()/
       ImportTexture() - e.g. "GameView", "SceneView", "Swapchain"
       (see Step 2's own note on the exact literal — NOT "Present", which
       is only the PASS name), or any future LUT/compute-pass output).
       `channel` defaults to "color"; "depth" visualizes the texture's
       companion depth buffer instead, if it has one (see Phase 3/5).
       `format` is the exact same response-format negotiation
       network-impl-2 already established (raw PNG bytes by default, or a
       JSON envelope with base64 data + metadata) - see Locked Design
       Decision 2.

GET http://127.0.0.1:8080/list_textures
    -> 200 OK, a JSON array of every texture name this engine has ever
       registered so far this session, each with its regime, extent,
       format, whether it has a depth buffer, and how "fresh" its last
       capture is - the discoverability companion to /get_texture (see
       Locked Design Decision 3), so an LLM doing visual debugging never
       has to guess an exact name blind.
```

While the engine's own window keeps rendering/updating at full frame rate
for every OTHER frame — exactly like every existing capture endpoint. Unlike
`/get_swapchain`/`/get_game_view`, a `/get_texture` request MAY, once per
request, insert one single full-GPU-idle stall into the frame it is finally
serviced on (see Locked Design Decision 1) — this is an explicit, accepted,
narrow exception to network-impl-2's own "zero added GPU stall" swapchain
design, justified below. Because `FrameCaptureBridge` (reused unchanged,
see Step 2) only ever allows ONE request per `FrameCaptureKind` to be
in-flight at a time, at most one such stall can ever be pending at once —
a second, concurrent `/get_texture` call gets an immediate `503` instead of
compounding a second overlapping stall (see Locked Design Decision 1's own
note on this).

## Step 2: The Situation (Where are we now?)

- **`network-impl-2` already shipped the entire plumbing this campaign
  reuses almost unchanged**: `FrameCaptureBridge` (the one sanctioned
  cross-thread bridge a Network route handler may touch —
  `src/Application/FrameCaptureBridge.h/.cpp`), the PNG/base64 encoding
  utilities (`src/Encoding/`), and the response-format-negotiation pure
  functions (`ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()`,
  `src/Network/NetworkRoutes.h/.cpp`). This campaign extends all three
  rather than inventing parallel versions.
- **`network-impl-3` already vendored a real JSON library**
  (`nlohmann::json`, see `cmake/FetchJson.cmake`, already used by
  `src/Network/NetworkRoutes.cpp`'s `ParseInstantiatePrimitiveRequest()`/
  `BuildInstantiatePrimitiveResponseJson()`) — this campaign's own new JSON
  response shapes (`/get_texture`'s JSON envelope with its new
  `frames_since_update` field, and the whole of `/list_textures`) are built
  with `nlohmann::json` too, NOT hand-formatted like
  `BuildCaptureJsonBody()` — that hand-formatting trick was only ever safe
  because it emits nothing but integers and base64 text (no escaping
  needed); a texture NAME echoed back into a response, and an array of
  several of them, is exactly the kind of caller-influenced string content
  hand-formatting is unsafe for.
- **This engine already has a fully-general, already-shipped Render Graph**
  (`src/Renderer/RenderGraph/`, campaign completed prior to all of
  `network-impl-*`) in which EVERY texture a pass declares — transient
  (`RenderGraphBuilder::CreateTexture(name, desc)`) or imported
  (`RenderGraphBuilder::ImportTexture(name, externalTarget, currentLayout)`)
  — already carries a human-readable `name` (`CompiledGraphInput::
  textureNames`, parallel to `textureDescs`/`textureImportInfo`). This is
  the exact vocabulary this campaign's whole feature is built on — **no new
  "please name your texture" mechanism needs inventing**, it already exists
  and is already used by every pass in the engine today: `"GameView"`/
  `"SceneView"` (`Application.cpp`'s two `ImportTexture()` calls),
  `"Swapchain"` (`FramePresenter.cpp`'s own `ImportTexture("Swapchain",
  target, ...)` — **AUDIT NOTE: confirmed live; the swapchain's registered
  TEXTURE name is `"Swapchain"`, never `"Present"` — `"Present"` is only
  the PASS name a nearby `AddPass()` call happens to use. An earlier
  revision of this document conflated the two in its "Definition of Done"
  section below; that has been corrected. Do not reintroduce the
  `"Present"` spelling anywhere a TEXTURE name is expected.**), and
  `"BlurredSceneOutput"` (`ComputeBlurValidation.cpp`'s own
  `ImportTexture()` call — Editor-only, only declared while the "Show
  Compute Blur (debug)" toggle is on and "Scene" is visible).
  **AUDIT NOTE — a corrected/removed example:** an earlier revision of this
  bullet also cited "per-model GPU-skinning output buffer names" as another
  instance of "this same vocabulary" — that was WRONG and has been removed.
  `RenderGraphBuilder::CreateBuffer()`/`ImportBuffer()`'s own parallel
  `CompiledGraphInput::bufferNames` list (what GPU-skinning's per-model
  output buffers actually use) is a **completely separate mechanism**,
  keyed by `BufferHandle`, not `TextureHandle` — this campaign's registry
  (Phase 1/2) is built ONLY by walking `ExecuteCompiledGraph()`'s resolved
  `physicalTextures`, never its sibling `physicalBuffers`, so a buffer's own
  name is NEVER visible to `/get_texture`/`/list_textures`, by design (this
  feature only ever deals in images — see Non-Goals). Do not cite a buffer
  name as if it were an example of a capturable texture name anywhere in
  this campaign's documents or code comments.
- **`RenderGraph::ExecuteCompiledGraph()` (`RenderGraph.cpp`) already
  resolves every declared texture handle into a real, physical
  `PhysicalTexture` (`RenderGraph.h`, private) — `{ bool resolved; bool
  isImported; bool hasDepth; RenderTarget target; VkSampler sampler;
  ResourceState colorState; ResourceState depthState; }` (field order
  confirmed live; re-check `RenderGraph.h` directly before implementing, as
  this struct may gain fields over time) — before running any pass's own
  `execute` callback.** This is the exact, ready-made data source a
  name→physical-resource registry needs; nothing new needs to be computed,
  only CAPTURED and kept around (today, `physicalTextures` is a purely
  local `std::vector`, rebuilt from scratch and thrown away at the end of
  every single `ExecuteCompiledGraph()` call — see Phase 2 for exactly
  where to intercept it before it's discarded).
- **`RenderGraph::Execute()` is called EXACTLY TWICE per engine frame**
  (`RenderGraph.h`'s own top-of-file comment; confirmed live in
  `Application::Run()`, `src/Application/Application.cpp`) — once tagged
  `ExecuteTimingMode::SynchronousImmediateReadback` (the off-screen Game
  view + Scene view regime, which already blocks until the GPU finishes
  before returning — see `Renderer::EndOffscreenRenderGraphRecording()`'s
  own doc comment), once tagged `ExecuteTimingMode::PipelinedDeferredReadback`
  (the swapchain Present regime, which does NOT block — see
  `network-impl-2/PHASE4_SWAPCHAIN_PIPELINED_CAPTURE_SERVICE.md`'s own
  extensive analysis of why that regime is hard to read back from safely
  with zero added stall). **Both regimes share the exact same, single
  `rg::RenderGraph m_renderGraph` instance** (`Application.h`) — there are
  NOT two separate `RenderGraph` objects, just two differently-tagged
  `Execute()` calls against one. This matters: a name→snapshot registry
  living as a member of `RenderGraph` itself automatically sees textures
  from BOTH regimes with no extra plumbing.
  **AUDIT NOTE — a consequence worth stating explicitly:** the
  `SynchronousImmediateReadback` call (and therefore this campaign's own
  "engine frame" counter behind `frames_since_update`, see Locked Design
  Decision 4) only ever runs when `Application::Run()`'s own
  `if (gameTarget != nullptr || sceneTarget != nullptr)` guard is true —
  i.e. it is skipped ENTIRELY on any frame where both the Editor's "Game"
  and "Scene" panels are hidden/absent (including every frame of a
  `-DGTE_ENABLE_EDITOR=OFF` release build, which never has either target at
  all). On such a frame, `RenderGraph::CurrentDebugTextureFrameCounter()`
  simply does not advance — it is not a bug, but it does mean
  `frames_since_update` measures "how many times the offscreen regime has
  actually executed", not literal wall-clock/engine frames, and a caller
  should not over-interpret it as the latter in a configuration where that
  regime can go long stretches without running at all.
- **A render-graph pass that only READS a resource is silently culled and
  never runs** (`RenderGraphCompiler::Compile()` — the exact same fact
  `network-impl-2/PHASE0_MASTER_STRATEGY.md` already discovered and
  designed around for the swapchain case). This campaign inherits that
  same constraint and the same answer: **no new pass is ever added for
  capture purposes** — the registry this campaign builds is populated
  PASSIVELY, by observing what `ExecuteCompiledGraph()` already resolved
  for reasons of its own, exactly mirroring how `network-impl-2` hooked
  into `FramePresenter.cpp`'s existing manual PRESENT_SRC_KHR transition
  rather than adding a graph-native pass.
- **A texture's `PhysicalTexture::colorState` as tracked INSIDE one
  `ExecuteCompiledGraph()` call does NOT reflect any manual, graph-EXTERNAL
  barrier applied afterwards — and this is NOT limited to "GameView"/
  "SceneView".** A live grep for every graph-external manual finalize call
  site in this codebase today found **FOUR**, not two:
  1. `Application::Run()` calls `FinalizeRenderTextureForExternalSampling(
     offscreenCmd, *gameTarget)` (`RenderPasses.cpp`) AFTER the
     `SynchronousImmediateReadback` `Execute()` call returns, transitioning
     `"GameView"` from `ColorAttachmentWrite` to `ShaderRead` by hand.
  2. The same call, for `*sceneTarget` → `"SceneView"`.
  3. **`FramePresenter.cpp`'s own manual transition of `"Swapchain"`** from
     `ColorAttachmentWrite` to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`, right
     before `vkQueuePresentKHR` — the exact same "graph-external, manual,
     precedented seam" `network-impl-2` already relied on for its own
     swapchain capture. **AUDIT NOTE: an earlier revision of this document
     only listed the two `Application.cpp` call sites above and implied
     Locked Design Decision 7's correction hook only needed adding at
     those two — that was WRONG.** Without the equivalent correction call
     for `"Swapchain"`, `/get_texture?texture_name=Swapchain` would feed a
     stale `ColorAttachmentWrite` `previousState` into
     `Renderer::CaptureImagePixels()` (Phase 3) — the WRONG actual layout
     for an image that has, by the time it's captured, already been
     transitioned to (and possibly already presented in)
     `PRESENT_SRC_KHR` — a real correctness bug (an incorrect image-layout
     transition barrier), not just a cosmetic metadata error. `FramePresenter::
     PresentViaRenderGraph(rg::RenderGraph& graph, ...)` already receives the
     exact `RenderGraph&` reference it needs to call
     `graph.NotifyDebugTextureStateOverride("Swapchain", presentSrcState)`
     directly, right alongside its existing manual `PRESENT_SRC_KHR`
     transition — no new plumbing is required to fix this, only remembering
     to add the call.
  4. **`ComputeBlurValidation::FinalizeForSampling()`'s own manual
     transition of `"BlurredSceneOutput"`** from `ComputeShaderWrite` to
     `ShaderRead` (`src/Editor/ComputeBlurValidation.cpp`), called from
     `Application::Run()` via `IEditorLayer::FinalizeBlurValidationForSampling()`
     right after the `"GameView"`/`"SceneView"` finalize calls. Editor-only,
     and only actually runs on a frame where the compute-blur-validation
     debug toggle is on AND "Scene" is visible (`m_writtenThisFrame` guards
     it) — but exactly the same class of bug applies whenever it does run.
  A registry populated ONLY from `ExecuteCompiledGraph()`'s own internal
  state would therefore report all FOUR of these names with a stale,
  wrong `ResourceState` the instant their own graph-external finalize runs.
  **Phase 3 must close this gap for ALL FOUR call sites**, not just the
  original two, via the small, explicit "correction" call already designed
  (Locked Design Decision 7) — see Phase 2/3 for the full mechanism, and
  re-grep for `ImportTexture(` / a manual `EmitImageBarrier` transitioning
  toward `ShaderRead`/`PRESENT_SRC_KHR` at implementation time to confirm
  this list of four is still exhaustive (a fifth could have been added
  since this audit). Any FUTURE named texture that has NO such
  external-finalize step at all (the realistic shape of a future
  atmosphere-scattering LUT, which nothing else needs to sample outside the
  render graph) needs no correction call whatsoever — the registry's own
  automatic, `ExecuteCompiledGraph()`-driven snapshot is already exactly
  right for it.
- **This engine's `Renderer` does not itself own the `rg::RenderGraph`
  instance — `Application` does** (`Application.h`'s `m_renderGraph`
  member; `Renderer::PresentViaRenderGraph(rg::RenderGraph& graph, ...)`
  only ever receives it BY REFERENCE, per-call). This means the new
  name→snapshot registry (owned BY `RenderGraph`, per Phase 1/2) and the
  actual pixel-readback primitive (owned BY `Renderer`, extending Phase 3
  of `network-impl-2`) are two different objects that only `Application`
  has simultaneous access to — exactly like `IsCaptureRequested()`/
  `CaptureRenderTexturePixels()`/`FulfillPendingRequest()` are already
  only ever combined together inside `Application::Run()` today, never
  inside `Renderer` or `RenderGraph` themselves. This campaign's own
  `Application::Run()` wiring (Phase 4) follows the exact same shape.
  (`FramePresenter.cpp`'s own correction call for `"Swapchain"`, above, is
  the one exception — it calls `RenderGraph::NotifyDebugTextureStateOverride()`
  directly against the `RenderGraph&` it's already handed, since it has no
  need for `Renderer`/`FrameCaptureBridge` to do so.)

## Step 3: The Plan

### Locked Design Decisions

These were confirmed with the project owner (via `ask_questions`) before any
phase document below was written, and MUST NOT be silently changed by a
later phase without updating this file first:

1. **Cross-regime capture via a deliberate, request-scoped
   `vkDeviceWaitIdle()` stall — not a `SwapchainCaptureService`-style
   per-texture pipelined machinery, and not a "synchronous regime only"
   restriction.** A named texture may belong to EITHER
   `ExecuteTimingMode` regime (today: "GameView"/"SceneView" are
   synchronous; "Swapchain" is pipelined; a future pass could add a
   texture to either). Rather than building a second, generalized,
   per-texture-name version of `network-impl-2`'s
   `SwapchainCaptureService` (a large, high-risk undertaking — see that
   campaign's own Phase 4 for how much machinery a single hardcoded
   pipelined resource already needed), `/get_texture`'s own capture path
   (Phase 4) calls a full, blocking `vkDeviceWaitIdle()` immediately before
   reading back ANY named texture's pixels, regardless of which regime it
   belongs to. Once every in-flight GPU submission (both regimes, every
   frame-in-flight slot) has genuinely finished, EVERY tracked texture's
   `ResourceState` the registry remembers is now provably, simply correct
   — with no frame-in-flight bookkeeping, no per-regime special-casing, and
   no risk of reading a torn/stale/about-to-be-overwritten image. This is
   an EXPLICIT, ACCEPTED exception to `network-impl-2`'s own "zero added
   GPU stall" swapchain-capture rule: `/get_texture` is a rare,
   human/LLM-triggered debugging call, never invoked every frame or in any
   performance-sensitive path, so the one-time frame-pacing hitch it causes
   on the single frame it actually fires is an acceptable, deliberate
   trade for dramatically lower implementation risk and complexity. This
   also means `/get_texture` needs NO "this texture belongs to the
   unsupported regime" failure branch at all — every registered texture,
   in either regime, is captureable the same way. **This stall can never
   compound/stack from concurrent callers**: `FrameCaptureBridge` (reused
   unchanged from `network-impl-2`) allows only ONE in-flight request per
   `FrameCaptureKind` at a time (its existing per-kind `Slot` shape, see
   Phase 4) — a second, concurrent `/get_texture` request while one is
   already pending gets an immediate `503`, never a second overlapping
   `vkDeviceWaitIdle()`.
2. **Response format negotiation is IDENTICAL to `network-impl-2`**:
   `ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()`
   (`NetworkRoutes.h/.cpp`) are reused, not reimplemented — `?format=png`
   (default) → raw PNG bytes; `?format=base64`/`?format=json` (or an
   `Accept: application/json` header with no explicit override) → a JSON
   envelope. The envelope's SHAPE gains new fields for this campaign only
   (see Locked Design Decision 4/5 below) — `BuildCaptureJsonBody()` itself
   is NOT modified (it stays exactly as `/get_game_view`/`/get_swapchain`
   need it); `/get_texture` builds its OWN, slightly richer JSON body via a
   new function (Phase 5), using `nlohmann::json` (already vendored — see
   Step 2).
3. **A companion discovery endpoint, `GET /list_textures`, is IN SCOPE for
   this campaign** (confirmed via `ask_questions`) — every texture name
   ever registered this session, plus its regime/extent/format/hasDepth/
   freshness, so an LLM never has to guess an exact `texture_name` value
   blind. See Phase 5.
4. **The JSON/base64 response envelope for `/get_texture` includes a
   `"frames_since_update"` field** (confirmed via `ask_questions`, after
   discussing the concrete atmosphere-scattering-LUT use case: a LUT that
   is only recomputed when its underlying parameters change enough could
   otherwise silently hand back a screenshot that is many frames — or,
   for a hidden Editor panel, arbitrarily old — out of date, with nothing
   in the response distinguishing "this is what the shader produced just
   now" from "this is a stale leftover from long before your last code
   change", which risks a wrong debugging conclusion). This is
   ADDITIVE-ONLY metadata: the raw-PNG response mode is completely
   unaffected (it has no room for metadata and none is added to it); only
   the JSON/base64 mode gains the field. See Phase 1/2 for the underlying
   frame-counter mechanism and Phase 5 for where it's surfaced. **Reminder
   (see Step 2's own audit note above): this counter only advances once per
   real `SynchronousImmediateReadback` `Execute()` call, so it can pause
   for long stretches (or forever, in a `GTE_ENABLE_EDITOR=OFF` build) —
   `frames_since_update` is "renders since last update", not a wall-clock
   measurement; do not build any future feature that assumes it advances
   every engine frame unconditionally.**
5. **Depth-channel capture is IN SCOPE for this campaign** (confirmed via
   `ask_questions`) — `/get_texture?...&channel=depth` visualizes a named
   texture's companion depth buffer (if it has one — e.g. "GameView"/
   "SceneView" both do) as a grayscale PNG, instead of its color image.
   `channel` defaults to `"color"` if omitted; requesting `"depth"` against
   a texture with no depth buffer at all is a clean, fast, well-defined
   error (see Phase 4/5), never a crash or a silently-wrong image. See
   Phase 3 for the depth-format-aware grayscale conversion this needs
   (this engine's depth format is whichever of
   `VK_FORMAT_D32_SFLOAT`/`VK_FORMAT_D32_SFLOAT_S8_UINT`/
   `VK_FORMAT_D24_UNORM_S8_UINT` `VulkanDevice::PickDepthFormat()`
   negotiated at startup — never assume which one without checking).
6. **Automatic registration, zero opt-in.** ANY texture declared via
   `RenderGraphBuilder::CreateTexture(name, desc)` or
   `RenderGraphBuilder::ImportTexture(name, ...)`, in EITHER
   `ExecuteTimingMode` regime, becomes automatically visible to
   `/get_texture`/`/list_textures` the instant it is first resolved by
   `RenderGraph::ExecuteCompiledGraph()` — no separate "please make this
   one debuggable" call is ever required from a pass author. This is what
   makes a future atmosphere-scattering LUT "just work" with zero glue code
   the day it's implemented (see Step 1). **Buffers declared via
   `CreateBuffer()`/`ImportBuffer()` are NEVER visible here, by design —
   see Step 2's own audit note for why this registry deliberately only ever
   walks `physicalTextures`, never `physicalBuffers`.** The one, narrow
   exception on the texture side is the "correction" mechanism from Locked
   Design Decision 7 below, which is purely about KEEPING an
   already-automatically-registered entry's tracked `ResourceState`
   accurate across a graph-external manual transition — it never changes
   WHETHER a texture is registered, only what state it's registered AS.
7. **A small, explicit `RenderGraph::NotifyDebugTextureStateOverride(name,
   newColorState)` correction hook** is added and called from EVERY place
   that performs a graph-external manual finalize on an already-registered
   named texture. **A live grep as of this revision found FOUR such call
   sites (see Step 2's own detailed bullet above) — this is a change from
   an earlier revision of this document, which only named two:**
   - `Application::Run()`'s two `FinalizeRenderTextureForExternalSampling()`
     call sites, for `"GameView"`/`"SceneView"` respectively.
   - `FramePresenter.cpp`'s own manual `PRESENT_SRC_KHR` transition, for
     `"Swapchain"` — called directly against the `RenderGraph&` that
     function already receives as a parameter.
   - `ComputeBlurValidation::FinalizeForSampling()`, for
     `"BlurredSceneOutput"` (Editor-only; a safe no-op path when the
     compute-blur-validation toggle is off, since that function itself
     early-returns when nothing was written this frame).
   Phase 3 must re-confirm this list is still exhaustive at implementation
   time (grep for `ImportTexture(` and for a manual `EmitImageBarrier(...)`
   whose destination state is `ShaderRead`/`PRESENT_SRC_KHR` immediately
   after a render-graph `Execute()` call returns) before considering this
   Locked Design Decision satisfied — a future pass with no equivalent
   external finalize step never needs to call this at all.
8. **No new CMake toggle.** Gated entirely by the existing
   `GTE_ENABLE_NETWORK` switch (default `ON`), exactly like every other
   Network endpoint. The registry itself (Phase 1/2) has zero Network
   dependency and always compiles/runs unconditionally, whether or not
   networking is enabled. **This is a deliberate, accepted trade, not a
   literally-free one**: it costs a small, bounded, always-on CPU
   bookkeeping overhead every real engine frame (a handful of
   string-compared `Upsert()` calls, O(declared textures that call) — see
   Phase 2), even with `GTE_ENABLE_NETWORK=OFF` and nobody ever querying
   it. This is negligible at this engine's scale (a single-digit-to-low-
   double-digit number of distinct texture names per frame, the same "no
   hashing on the hot path, plain vector scan" convention `AGENTS.md`
   already establishes elsewhere) — but should not be described to a future
   contributor as costing "nothing" outright.
9. **Loopback-only, no auth, no HTTPS — unchanged.** Inherits every one of
   `network-impl-1`'s Non-Goals.

### Non-Goals (explicitly out of scope for `network-impl-4`)

- **No configurable resolution/downscaling/cropping** — a captured texture
  is always its full, current resolution, exactly like every existing
  capture endpoint.
- **No video/continuous streaming** — one still frame per request.
- **No `POST`/request-body-driven behavior** — both new endpoints are
  bodyless `GET`s.
- **No Editor UI panel** for browsing/triggering a named-texture capture —
  purely an HTTP-driven, headless-tooling feature, exactly like
  `network-impl-2`.
- **No mip-level/array-layer selection** — every texture this campaign's
  render graph can declare today is a single 2D image, single mip, single
  layer (`TextureDesc`, `RenderGraphTypes.h`, confirmed live — `width`/
  `height`/`format`/`hasDepth` only, no mip/array-layer count fields exist
  at all) — there is nothing to select between yet.
- **No buffer capture of any kind** — see Step 2/Locked Design Decision 6's
  own note: `CreateBuffer()`/`ImportBuffer()`-declared resources (e.g.
  GPU-skinning's per-model output buffers) are a completely separate
  `BufferHandle`/`bufferNames` vocabulary this registry never touches; a
  future "capture a named GPU buffer's raw bytes" feature would be a
  distinct, later campaign, not an extension of this one.
- **No change to `/get_swapchain`/`/get_game_view`'s own existing
  behavior, response shape, or implementation** — this campaign only ADDS
  `/get_texture`/`/list_textures`; Phase 3's small refactor of
  `Renderer::CaptureRenderTexturePixels()` (extracting a shared primitive)
  must leave that method's existing public signature and behavior
  byte-for-byte identical for its existing caller.
- **No attempt to build/implement the atmosphere-scattering feature
  itself** — that is explicitly a FUTURE campaign; this campaign only
  ensures that whenever it IS built, its LUTs are debuggable for free.

### Phase Map

| Phase | Deliverable |
|---|---|
| **1** | `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h/.cpp` — a new, pure, Tier-1-testable, Vulkan-header-only (no live device) name→snapshot table: insert/update/query/enumerate, tagged by `ExecuteTimingMode` regime and a monotonically increasing "last updated" frame counter. |
| **2** | Wires the registry into `RenderGraph` itself (a new private member, populated automatically at the end of every `ExecuteCompiledGraph()` call, for both regimes) + the frame-counter increment (reusing the existing `RenderGraphResourcePool::BeginFrame()` call site as the "one real engine frame" signal) + `RenderGraph::DebugTextureSnapshotFor()`/`ListDebugTextures()`/`NotifyDebugTextureStateOverride()` public accessors. |
| **3** | `Renderer` gains a generalized, Vulkan-level image-readback primitive (refactoring `CaptureRenderTexturePixels()` to share code, zero behavior change for its existing caller) usable against ANY `VkImage`/format/extent/`ResourceState` — not just a `RenderTexture&` — plus a `WaitForGpuIdle()` wrapper over `vkDeviceWaitIdle()`, plus a new depth-buffer→grayscale-RGBA8 conversion utility (`src/Encoding/`). **ALL FOUR** graph-external manual-finalize call sites (`Application.cpp`'s two `FinalizeRenderTextureForExternalSampling()` calls for `"GameView"`/`"SceneView"`, `FramePresenter.cpp`'s own `"Swapchain"` `PRESENT_SRC_KHR` finalize, and `ComputeBlurValidation::FinalizeForSampling()`'s `"BlurredSceneOutput"` finalize — see Locked Design Decision 7, corrected in this revision) are paired with the new `NotifyDebugTextureStateOverride()` correction call. |
| **4** | `FrameCaptureBridge` gains a new `FrameCaptureKind::NamedTexture` + a dynamic (texture name, channel) request payload + its own `Slot`. `Application::Run()` gains the per-frame wiring: look the requested name up in the registry; if known, `WaitForGpuIdle()` + generic readback + (color or depth) encode + `FulfillPendingRequest()`; if not yet known this session, leave the request pending (the existing fixed timeout eventually resolves it either way). |
| **5** | `GET /get_texture` + `GET /list_textures` routes (`NetworkRoutes.h/.cpp`, `NetworkServer.cpp`) — new `nlohmann::json`-built response shapes (including `frames_since_update`), new query-parameter parsing (`texture_name`, `channel`), and every new failure-status mapping (missing/empty `texture_name` → 400, unknown-depth-channel-on-a-texture-with-none → 409, bridge unavailable/busy → 503, timeout → 504). |
| **6** | Automated tests for every new Tier-1 module (registry, JSON builders/parsers, depth conversion) + `AGENTS.md`/`README.md` documentation updates + full build/`ctest` regression pass + manual end-to-end verification against a running `GreatTamanaEngine.exe`. |

### Definition of Done for the whole campaign

- `cmake --build build` succeeds (default `GTE_ENABLE_NETWORK=ON` config).
- Running the built `GreatTamanaEngine.exe` (Editor build, "Game" panel
  visible) and issuing `GET http://127.0.0.1:8080/get_texture?texture_name=GameView`
  returns a valid PNG, byte-for-byte visually identical to what
  `GET /get_game_view` already returns for the same frame (proving the new,
  generic path produces the same correct result as the existing, dedicated
  one for a texture both can reach).
- `GET http://127.0.0.1:8080/get_texture?texture_name=GameView&channel=depth`
  returns a valid grayscale PNG.
- `GET http://127.0.0.1:8080/get_texture?texture_name=Swapchain` returns a
  valid PNG that is visually plausible as a recent frame of the presented
  window content — this is the specific regression check for Locked Design
  Decision 7's corrected, four-call-site scope (see Step 2's own audit
  note): a failure here (a garbled image, a validation-layer error, or a
  crash) most likely means the `"Swapchain"` correction call
  (`NotifyDebugTextureStateOverride()` from `FramePresenter.cpp`) was
  forgotten.
- `GET http://127.0.0.1:8080/get_texture?texture_name=DoesNotExist` returns
  HTTP `504` (times out — never hangs forever, never crashes) with a
  friendly plain-text/JSON body.
- `GET http://127.0.0.1:8080/list_textures` returns a JSON array containing
  at least `"GameView"`/`"SceneView"`/`"Swapchain"` (whichever are actually
  visible/rendering this session — **note the correct texture name is
  `"Swapchain"`, not `"Present"`; see Step 2's own audit note**), each with
  correct regime/extent/format/hasDepth metadata.
- `ctest` (see the regression command below) passes, including every new
  test file this campaign adds.
- `AGENTS.md` has an updated "Networking" section documenting
  `/get_texture`/`/list_textures` and the `RenderGraphDebugTextureRegistry`
  as the mechanism behind them; `README.md`'s "Status" section has a new
  bullet.

### Regression / build commands (reference — see each phase for exact use)

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
