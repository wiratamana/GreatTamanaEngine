# PHASE4 — First Real Consumer: GBuffer Validation Pass (Additive, Debug-Only)

Parent: `PHASE0_MASTER_STRATEGY.md` — **read that file first**, and read
`PHASE1`/`PHASE2`/`PHASE3`'s own completion reports before starting.

**Use `ask_questions` whenever you hit a genuine ambiguity or a design choice
this document (or `PHASE0_MASTER_STRATEGY.md`) doesn't already pin down.** If
you delegate any further sub-task, that delegation prompt must repeat this
same instruction.

## Step 1: The Goal (Where are we going?)

Prove the whole MRT mechanism end-to-end with the smallest possible real
consumer, exactly mirroring `src/Editor/ComputeBlurValidation.h`/`.cpp`'s
already-proven shape (lazy init, persistent owned `RenderTexture`s, declared
via `RenderGraphBuilder::AddRenderPass()`, toggled by a small Scene-panel
checkbox, `ViewScope::SceneView` + `RenderPassCategory::Debug`, zero effect on
the default Game View):

1. A new "GBufferValidation" GRAPHICS pass writes **2 color attachments in
   one draw** (`outAlbedo`, `outNormal`) using PHASE1-3's new MRT mechanism —
   proving "N targets written by one pass, one draw, one pipeline".
2. A second, small pass reads ONE of those two textures back (a plain
   `ReadTexture(albedoHandle, ShaderRead)`) and copies/visualizes it into its
   own separate output — proving "a later pass reading one of N outputs,
   cross-pass, barrier-synchronized automatically by the existing
   `RenderGraphBarrierPlanner`, with zero new barrier code".
3. Both passes are additive/opt-in — a checkbox in the Scene panel toggles
   whether they are declared at all each frame, exactly like
   `ComputeBlurValidation`'s own "Show Compute Blur (debug)" checkbox.

## Step 2: The Situation (Where are we now?)

- `src/Editor/ComputeBlurValidation.h`/`.cpp` is the EXACT precedent to copy
  the shape of — re-read both files in full before writing a single line of
  this phase's own code (they are short; `PHASE0_MASTER_STRATEGY.md`'s Step
  2 already quotes the key structural facts, but read the real files
  directly, not just this summary).
- `ComputeBlurValidation` is a COMPUTE pass (`PassKind::Compute`,
  `WriteTexture()`/`ResourceAccess::ComputeShaderWrite`) reading the Scene
  View via a plain `ShaderRead` texture sample. This phase's new pass is a
  GRAPHICS pass instead (a real fragment shader with N `out` color
  attachments via `WriteColorAttachment()` — the whole point of proving the
  MRT mechanism) — the SHAPE (lazy init, persistent RenderTexture ownership,
  `AddRenderPass()` declaration, `ViewScope::SceneView` +
  `RenderPassCategory::Debug`, a toolbar checkbox, `FinalizeForSampling()`-
  style layout transition before ImGui samples it) is what to copy; the pass
  KIND and the actual shader work are different.
- `Renderer::CreateRenderTexture()` (via `GpuResourceFactory`) is how
  `ComputeBlurValidation` creates its own persistent output — this phase's
  pass needs TWO or more such persistent color outputs (albedo/normal) PLUS
  a small third one for the "visualize one channel" pass's own destination —
  or, more simply, the visualize pass could write directly into a THIRD
  small persistent `RenderTexture` sized/format however is easiest for
  `ImGui::Image()` display (mirrors `ComputeBlurValidation`'s own
  `blurredOutput`, explicit `VK_FORMAT_R8G8B8A8_UNORM`).
- `Renderer::CreatePipeline()`'s existing single-format overload cannot build
  this pass's PSO — PHASE3's new N-format overload is required here, its
  FIRST real consumer.
- **A real, non-obvious wrinkle confirmed directly against `Pipeline.cpp`
  (lines 135-141): every `Pipeline` built via `Renderer::CreatePipeline()` —
  including PHASE3's new N-format overload, which does not change this —
  ALWAYS enables depth test AND depth write
  (`depthTestEnable = VK_TRUE`/`depthWriteEnable = VK_TRUE`/
  `depthCompareOp = VK_COMPARE_OP_LESS`), unconditionally, with NO parameter
  to disable/customize this.** This is exactly why
  `AtmosphereSkyBackgroundRenderer`/`SceneGridRenderer` (both full-screen-
  triangle passes with non-standard depth needs — `VK_COMPARE_OP_EQUAL`,
  depth write disabled) BYPASS `Renderer::CreatePipeline()`/`Pipeline`
  entirely and hand-build their own `VkPipeline` directly (see
  `AtmosphereSkyBackgroundRenderer.h`'s own header comment) — copying their
  DRAW-CALL technique (Step 2's own note above) does NOT mean this phase
  also needs their bypass-Pipeline technique. A SECOND, related consequence
  of the same root cause: `Pipeline`'s constructor also ALWAYS declares
  `vertexInput.vertexBindingDescriptionCount = 1` (one real vertex binding,
  per whichever `VertexLayout` was requested) — confirmed
  `AtmosphereSkyBackgroundRenderer.cpp` (line ~145-146) builds its own
  zero-binding `VkPipelineVertexInputStateCreateInfo` for exactly this
  reason, since its `vkCmdDraw(cmd, 3, 1, 0, 0)` call binds NO vertex buffer
  at all. Choosing path (a) below therefore also means either binding SOME
  real (even trivial/unused) 3-vertex buffer before this pass's own draw
  call, or accepting a pipeline that declares a vertex binding the draw call
  never actually binds a buffer for (verify this is validation-layer-clean
  in practice before assuming it is fine either way). Two workable choices
  exist for this phase's own pass overall, and this document deliberately
  does NOT pick one — use `ask_questions` if genuinely unsure:
  (a) **Simplest — go through the standard `Renderer::CreatePipeline()`/
  `Pipeline` path anyway**, and ALSO declare a real (possibly scratch/unused)
  depth attachment for this pass via `WriteDepthStencilAttachment()` so the
  mandatory depth test has something real to test against (matching every
  other real graphics pass in the engine today, all of which pair a color
  write with a depth write) — the simplest, smallest diff, and consistent
  with this phase's own "smallest possible real consumer" goal; OR
  (b) hand-build a dedicated `VkPipeline` for this pass the same way
  `AtmosphereSkyBackgroundRenderer` does, with depth testing disabled
  entirely, avoiding a depth attachment altogether — more faithful to "this
  pass has no real depth data and needs none", but a bigger diff that
  sidesteps PHASE3's whole new N-format `Pipeline` capability (this
  campaign's own mechanism under test) for its own first real consumer,
  which would undercut PHASE0's stated goal of PHASE3/PHASE4 proving that
  exact mechanism together. Given that undercutting risk, (a) is the
  document's own weak recommendation, but this is explicitly left as a real
  design decision for whoever implements this phase, not a already-settled
  fact.
- Vertex geometry: a full-screen triangle/quad draw
  (`RenderPassDrawKind::DrawQuad`, mirroring
  `AtmosphereSkyBackgroundRenderer.cpp`'s own `vkCmdDraw(cmd, 3, 1, 0, 0)`
  no-vertex-buffer full-screen-triangle technique) is the simplest way to
  fill 2 G-buffer targets with clearly-distinguishable test content (e.g. a
  UV-based gradient into `outAlbedo`, a fixed/derived pseudo-normal pattern
  into `outNormal`) with NO real mesh/scene data needed at all — this keeps
  the proof pass entirely self-contained and independent of whatever the
  Scene View itself is currently showing. Confirm this approach is
  acceptable (it is the simplest possible proof and matches this project's
  existing "prove the mechanism with the simplest content that exercises
  it" precedent, e.g. `BoxBlur.comp`'s own deliberately-simple, non-
  production blur) — if a richer proof (e.g. actually deriving albedo/normal
  from real Scene View geometry) seems more valuable, `ask_questions` before
  committing extra scope.
- Shader compilation: every `.vert`/`.frag`/`.comp` file needs one
  `gte_add_shader(GreatTamanaEngine src/Shaders/<file>)` line in the root
  `CMakeLists.txt`, **wrapped in its own `if(GTE_ENABLE_EDITOR) ... endif()`
  block** (confirmed convention for an Editor-only shader — see
  `Shaders/BoxBlur.comp`'s own registration, `CMakeLists.txt` line ~856-858,
  or `SceneGrid.vert`/`.frag`'s equivalent block, line ~844-847; `Mesh.frag`/
  `TexturedMesh.frag` at line 801/810 are registered UNCONDITIONALLY and are
  the WRONG precedent to copy for this Editor-only pass — see Step 3.4
  below).


## Step 3: The Plan (How do we get there?)

### 3.1 — `Shaders/GBufferValidation.vert`/`GBufferValidation.frag`

- `.vert`: a plain full-screen-triangle vertex shader with NO vertex buffer
  input (mirrors `Shaders/AtmosphereSkyBackground.vert`'s own
  `gl_VertexIndex`-driven full-screen-triangle technique — read that file
  directly for the exact pattern to copy) outputting a UV varying.
- `.frag`: TWO outputs —
  ```glsl
  layout(location = 0) out vec4 outAlbedo;
  layout(location = 1) out vec4 outNormal;
  ```
  `outAlbedo` = a simple, clearly-recognizable procedural pattern from the
  UV varying (e.g. a checkerboard or gradient) so a human looking at it via
  `GET /get_texture` can immediately tell it's real, distinct content, NOT a
  copy of `outNormal`. `outNormal` = a DIFFERENT, equally simple but visibly
  distinguishable procedural pattern (e.g. a radial gradient, or a
  synthetic "fake normal" `vec3` mapped to `[0,1]` and written as
  `vec4(fakeNormal * 0.5 + 0.5, 1.0)`, the standard normal-buffer packing
  convention) — the goal is that a screenshot of BOTH channels immediately,
  visibly proves "these are two independent images, not the same one twice".

### 3.2 — `src/Editor/GBufferValidation.h`/`.cpp`

New class, `GTE_ENABLE_EDITOR`-only (lives under `src/Editor/`, exactly like
`ComputeBlurValidation`), owning:
- Two persistent `RenderTexture`s (`m_albedoOutput`/`m_normalOutput`), lazily
  created in `EnsureInitialized()` against a live `Renderer&`, matching
  whatever extent the Scene panel currently is (mirrors
  `ComputeBlurValidation::EnsureInitialized()`'s exact resize-on-demand
  discipline, including the `vkDeviceWaitIdle()` + `Resize()` pattern for a
  panel-resize event).
- A `Pipeline` built via `Renderer::CreatePipeline()`'s NEW N-format overload
  (PHASE3), with `colorFormats = { albedoFormat, normalFormat }` (both
  formats can simply be the same explicit format the two `RenderTexture`s
  were created with — no need for them to differ) — built ONCE, lazily, in
  `EnsureInitialized()`, exactly like `ComputeBlurValidation` lazily builds
  its own `ComputePipeline`.
- A third small persistent `RenderTexture` (`m_visualizedOutput`, explicit
  `VK_FORMAT_R8G8B8A8_UNORM`, mirroring `ComputeBlurValidation`'s own
  `blurredOutput` format choice exactly) that the SECOND pass writes into —
  this second pass can be EITHER a tiny graphics full-screen-triangle pass
  (`WriteColorAttachment(visualizedHandle)`, reading `albedoHandle` via
  `ReadTexture(..., ShaderRead)`, a trivial pass-through fragment shader) OR
  a tiny compute pass (`WriteTexture()`/`ComputeShaderWrite`, mirroring
  `ComputeBlurValidation`'s own compute shape exactly, just doing a plain
  copy instead of a blur) — prefer whichever is the SMALLER real diff; a
  compute copy pass is likely simplest since `ComputeBlurValidation` already
  proves that exact "compute pass reads one texture, writes another,
  cross-pass-synchronized" shape and this phase can reuse its descriptor-
  set-layout/dispatch pattern almost verbatim, just with a trivial
  `imageStore(dst, coord, imageLoad(...))`-style copy shader instead of a
  blur kernel. Use `ask_questions` if genuinely torn between the two.
- `AddPass(RenderGraphBuilder& builder, Renderer& renderer, VkExtent2D
  sceneExtent) -> std::pair<rg::TextureHandle, rg::TextureHandle>` (or a
  small named struct) returning the albedo/normal (and visualized) handles
  for the caller to add to `finalOutputs` — mirrors
  `ComputeBlurValidation::AddPass()`'s own return-handle contract exactly
  (Step 2's own note on `PHASE0`: a write with no reachable root is silently
  culled).
- `FinalizeForSampling(VkCommandBuffer cmd)` — transitions whichever outputs
  need it from their post-write layout to `ShaderRead`, mirroring
  `ComputeBlurValidation::FinalizeForSampling()` exactly (including its
  "safe no-op if AddPass() wasn't called this frame" `m_writtenThisFrame`-
  style guard).
- `RenderPassEvent`: use `ask_questions` if unsure, but the same reasoning
  `ComputeBlurValidation.cpp` line 142-162 documents applies almost
  identically here — if this pass reads the Scene View (optional, see Step
  2's full-screen-triangle simplification note — if this phase's pass does
  NOT read the Scene View at all, this concern does not apply and a simple
  default `RenderPassEvent` tag is fine); if it DOES end up sampling real
  Scene View content, tag it `AfterTransparents` for the identical reason
  `ComputeBlurValidation` does.

### 3.3 — Wiring: `EditorLayer.h`/`ImGuiEditorLayer.cpp`/`NullEditorLayer.cpp`/`EditorContext.h`/`ScenePanel.cpp`

Mirror `ComputeBlurValidation`'s existing wiring exactly:
- `IEditorLayer` (`EditorLayer.h`) gains two new PURE VIRTUAL methods
  (`AddGBufferValidationPass()`/`FinalizeGBufferValidationForSampling()`),
  matching `AddBlurValidationPass()`/`FinalizeBlurValidationForSampling()`'s
  own shape/doc-comment style exactly.
- **`src/Editor/NullEditorLayer.cpp` MUST ALSO gain a matching no-op override
  for BOTH new methods in this SAME phase.** This is not optional/cosmetic:
  `IEditorLayer` is a pure-virtual interface compiled in BOTH build
  configurations (confirmed directly — `EditorLayer.h` has no
  `GTE_ENABLE_EDITOR` guard of its own), and `NullEditorLayer` (compiled
  INSTEAD of `ImGuiEditorLayer.cpp` whenever `GTE_ENABLE_EDITOR` is OFF — see
  root `CMakeLists.txt` line ~692/its own header comment) must implement
  EVERY pure virtual method or the release/non-Editor build fails to compile
  at all (an abstract class cannot be instantiated by
  `CreateEditorLayer()`). Every existing method on this interface already has
  a `NullEditorLayer` stub (see `AddBlurValidationPass()`'s own
  `return std::nullopt;`/`FinalizeBlurValidationForSampling()`'s own empty
  `{ }` body in that file for the exact pattern to copy) — this phase's two
  new methods are no exception. Do not discover this gap only when the
  non-Editor configuration fails to build; add the stub up front, as part of
  this same change.
- `ImGuiEditorLayer` owns an `m_gbufferValidation` member (mirrors
  `m_blurValidation`), and rebuilds/refreshes its own ImGui descriptor(s) for
  the new persistent output texture(s) the same way it already does for
  `m_blurValidation`'s output.
- `Application::Run()` (or wherever `AddBlurValidationPass()` is currently
  called — search for that exact call site) gains a parallel, symmetric call
  for the new pass, gated behind its OWN toggle (see below) exactly the same
  way the existing one is gated.
- `EditorContext.h` gains its own new toggle field (e.g.
  `showGBufferValidationOutput`, mirroring `showBlurredSceneOutput`'s exact
  declaration/doc-comment shape at `EditorContext.h` line ~181) plus whatever
  descriptor field(s) the chosen preview approach needs (mirroring
  `blurredSceneOutputDescriptor`) — confirmed as its own separate field,
  never reusing `showBlurredSceneOutput` itself, so the two debug tools stay
  independently toggleable.
- `Panels/ScenePanel.cpp` gains a second, small, clearly-labeled checkbox —
  e.g. "Show GBuffer Validation (debug)" — right next to the existing "Show
  Compute Blur (debug)" one (`ScenePanel.cpp` line ~52), controlling whether
  `AddGBufferValidationPass()` is even called this frame (mirrors that
  existing checkbox's exact wiring, including its
  `showingBlurredOutput`-style "swap the Scene panel's own displayed image"
  precedent at `ScenePanel.cpp` lines ~54-57 — decide, and record in the
  completion report, whether this phase's checkbox swaps the Scene panel's
  displayed image the same way, or is purely a HTTP-query-only debug toggle
  with no own preview image at all; either is acceptable, see the next bullet).
- Somewhere sensible in the Editor (a small, temporary or permanent debug
  display — use `ask_questions` if unsure whether this needs its OWN
  ImGui::Image() preview area or whether "just query it via `GET
  /get_texture`" is sufficient for this campaign's proof purposes; the
  latter is likely sufficient and simpler, matching this campaign's Non-Goal
  of "no Frame Debugger per-output picker UI" — a full custom multi-image
  preview panel would be scope creep beyond what `PHASE0` asked for).

### 3.4 — CMake

Add `gte_add_shader(GreatTamanaEngine src/Shaders/GBufferValidation.vert)`/
`gte_add_shader(GreatTamanaEngine src/Shaders/GBufferValidation.frag)` (and,
if the second/visualize pass is a compute shader, its own `.comp` file too),
**wrapped in `if(GTE_ENABLE_EDITOR) ... endif()`** — confirmed directly
against the real `CMakeLists.txt`: `Mesh.frag`'s own registration (line 801)
is the WRONG precedent to copy here, since that shader pair is registered
UNCONDITIONALLY (it is a core gameplay feature, `Game.cpp` always loads it).
The CORRECT precedent, since `GBufferValidation.h`/`.cpp` is
`GTE_ENABLE_EDITOR`-only code exactly like `ComputeBlurValidation`, is
`Shaders/BoxBlur.comp`'s own registration (`CMakeLists.txt` line ~856-858) —
`if(GTE_ENABLE_EDITOR) gte_add_shader(GreatTamanaEngine
src/Shaders/BoxBlur.comp) endif()` — or `SceneGrid.vert`/`.frag`'s
equivalent `if(GTE_ENABLE_EDITOR)` block (line ~844-847) — mirror EITHER of
those two, never the unconditional `Mesh.frag`/`TexturedMesh.frag` shape. Add
`src/Editor/GBufferValidation.h`/`.cpp` to whatever target list
`ComputeBlurValidation.h`/`.cpp` are already part of (same
`GTE_ENABLE_EDITOR`-guarded source list).

### 3.5 — What this phase does NOT touch

- The Game View's actual default rendered output — completely unaffected,
  verify this explicitly as part of this phase's own live check (Definition
  of Done).
- No new `RenderPassCategory` — reuses `RenderPassCategory::Debug` (Locked
  Design Decision 5, `PHASE0`).
- No Frame Debugger code changes — the existing generic debug-texture
  registration/Render Graph panel/Frame Debugger tree grouping already
  handles this pass correctly with zero changes (confirmed by `PHASE0`'s own
  research); this phase's own live check (Definition of Done) is what
  PROVES that claim for this real pass, it does not need to add any new
  Frame-Debugger-side code to make it true.

### Definition of Done for this phase

- `GBufferValidation` pass runs ONLY when its Scene-panel checkbox is
  checked; unchecked (the default), the Game View and Scene View render
  identically to before this whole campaign.
- When checked: `GET /list_textures` (or the equivalent Render Graph panel
  texture list) shows the albedo/normal outputs (and the visualized copy) as
  independently-named, independently-inspectable entries; `GET /get_texture`
  against each one returns visibly DIFFERENT image content for albedo vs.
  normal (confirmed via `load_image` on the fetched result, or via a
  side-by-side screenshot) — this is the campaign's single most important
  visual proof and must be captured/described in this phase's completion
  report.
- The Render Graph panel shows the "GBufferValidation" pass with 2 real
  write names; the Frame Debugger shows a sane, correctly-grouped
  (Debug-category-excluded) leaf for it.
- A fast, targeted incremental build succeeds — **explicitly confirm this for
  BOTH `GTE_ENABLE_EDITOR=ON` (the pass's own real code) AND
  `GTE_ENABLE_EDITOR=OFF`** (proving `NullEditorLayer.cpp`'s new stub overrides
  compile and the release configuration is not broken by this phase's two new
  `IEditorLayer` pure virtual methods — see Step 3.3) — a live
  `run_app_background`/`gte_send_request` check (per this instruction set's
  own available tooling) confirms the above with the checkbox both off and
  on.
- `PHASE4_COMPLETION_REPORT.md` written, including which of the Step 3.2
  "second pass" shapes (graphics pass-through vs. compute copy) was chosen
  and why, and the visual-proof description/evidence above.
