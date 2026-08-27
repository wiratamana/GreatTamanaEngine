# RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md
### (Part 7 of 9 - see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` for the full campaign)

## V2 Revision Notes (2nd iteration review)

Three changes from v1:

1. **The `Application::Run()` code sample is corrected to use TWO
   `RenderGraph::Execute()` calls, not one, matching
   `RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md`'s resolved design.**
   v1's sample wrapped Game view, Scene view, AND Present all inside one
   `Execute()` call sharing one `cmd`/one `outputs` vector - this
   contradicted Phase 6's own prose (which implied multiple calls) and,
   more importantly, doesn't match reality: Game/Scene use
   `FramePresenter`'s synchronous offscreen command buffer/fence, while
   Present uses its own pipelined, frame-in-flight command buffer/fence/
   semaphores - these cannot share one recorded command buffer without
   either making Present synchronous (a real frame-pacing regression) or
   inventing unspecified multi-submission machinery. See Step 3.2 below
   for the corrected sample.
2. **The minimized-window / "Present recorded nothing this frame" case is
   now explicitly addressed**, since it interacts directly with fix #1:
   today, `FramePresenter::Present()` can independently skip a frame
   (return `std::nullopt`) while Game/Scene `RenderOffscreen()` calls
   keep running completely normally (a minimized OS window doesn't pause
   the Editor's own off-screen Game/Scene views - see
   `FramePresenter.cpp`). With two independent `Execute()` calls (fix #1),
   this falls out naturally: the offscreen `Execute()` call always runs
   when `gameTarget`/`sceneTarget` are non-null, completely independent of
   whether the Present `Execute()` call's own swapchain acquire succeeds
   this frame - see Step 3.2 below for exactly how this is threaded
   through.
3. **A new Step 3.7, "Validate the investment - a real cross-pass read,"**
   added per `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision
   Note 4: none of the three real passes this phase migrates
   (`AddGameViewPass`/`AddSceneViewPass`/`AddPresentPass`) ever declares a
   cross-pass `ReadTexture()` - Dear ImGui samples the Game/Scene
   `RenderTexture`s entirely on its own, outside the graph's resource
   model, via its own descriptor sets. That means the single most novel,
   most valuable capability this whole nine-phase campaign built -
   resolving a declared read of one pass's output inside another pass,
   with a real, graph-synthesized barrier in between - is NEVER exercised
   in shipped, production code by Phases 1-8 alone. Step 3.7 below is the
   concrete, scoped follow-up that closes this gap immediately after this
   phase (and Phase 8) land, using the already-planned Scene-view
   outline-highlight post-process (`TODO.md`, "Editor / Debug UI") as the
   proving ground.

Everything else in this phase is unchanged from v1.

---

## Step 1: The Goal (Where are we going?)

Cut the engine's REAL frame - Game view, Scene view, Present/ImGui overlay -
over onto the `RenderGraph` built across Phases 1-6, as a single, carefully
bracketed change, with **zero observable behavior difference** to a user
and **zero signature changes** to `Game::Render()`, `RenderSystem::Draw()`,
`Renderer::Submit()`, or any Editor panel. When this phase is done,
`FrameRecorder`'s hand-written barrier code and `FramePresenter`'s
hand-sequenced pass orchestration are DEAD CODE, ready for deletion, and
every future rendering feature (a shadow pass, a post-process pass, the
already-flagged Scene-view outline highlight) can be added as one more
`AddPass()` call, with automatic ordering/culling/barriers, instead of a
bespoke hand-extension of `FrameRecorder`.

## Step 2: The Situation / The Problem (Where are we now?)

By the end of Phase 6, `RenderGraph` is a fully working, independently
verified class that NOTHING in production code calls yet.
`Application::Run()` (`src/Application/Application.cpp`) still manually
calls `m_game.Render(...)` followed by `m_renderer.RenderOffscreen(...)`
for Game view, then again for Scene view, then `m_renderer.Present(...)`
for the swapchain+ImGui overlay - exactly as it does today, described in
full in `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s Step 2. This phase is
where that real, currently-load-bearing sequence gets rewritten to declare
itself as (see V2 Revision Note 1) TWO `RenderGraph::Execute()` calls -
one covering Game view + Scene view together (today's synchronous
offscreen regime), one covering Present alone (today's pipelined swapchain
regime) - instead of one merged call.

The risk this phase manages, above all others in the campaign, is
REGRESSION: this is the one phase that touches code every existing Editor
panel and every existing gameplay call site actually depends on, live,
every frame. Every other phase (1-6) was purely additive; this one is not.

## Step 3: The Plan (How will we get there?)

### 3.1 - Define the three real passes, as thin wrapper functions

`src/Application/RenderPasses.h/.cpp` (a NEW, small, `Application`-layer
file - deliberately NOT inside `src/Renderer/RenderGraph/`, since these
three passes encode ENGINE-SPECIFIC, Editor-aware knowledge - which
`RenderTexture` is "Game," which is "Scene" - that `Renderer`/`RenderGraph`
itself must never know about, per this codebase's own Clean Architecture
rule and the exact same reasoning `GpuTimingSlot`'s deliberately generic
naming already established):

```cpp
void AddGameViewPass(rg::RenderGraphBuilder& b, Game& game, Renderer& r,
    rg::TextureHandle gameViewTarget, float aspect);
void AddSceneViewPass(rg::RenderGraphBuilder& b, Game& game, Renderer& r,
    rg::TextureHandle sceneViewTarget, float aspect, const Mat4& sceneViewProjection);
void AddPresentPass(rg::RenderGraphBuilder& b, rg::TextureHandle swapchainImage,
    const std::function<void(VkCommandBuffer)>& recordImGui);
```

Each function's body is a direct, literal translation of what
`Application::Run()`'s corresponding block already does today: `setup`
declares exactly one color-attachment write (plus, for Game/Scene, one
depth-attachment write) against the handle it's given; `execute` calls
`Game::Render()` (UNCHANGED signature) and, for Present, invokes the
supplied ImGui `recordExtra`-equivalent callback - the SAME callback shape
`Renderer::Present()` already accepts today, just relocated to run inside a
pass's `execute` instead of inside `FrameRecorder::RecordFrame()`.
`Game::Render()`'s own body (`src/Game/Game.cpp`) is **completely
untouched** - it still just calls `renderer.Clear()` +
`m_renderSystem.Draw(...)`, exactly as today; `RenderSystem::Draw()`
(`src/Game/RenderSystem.cpp`) is **completely untouched** too - it still
calls `renderer.Submit(...)` per draw command. This is the single most
important design property of this whole phase: **the render graph
integration happens entirely BELOW `Renderer::Submit()`, inside
`Renderer`/`Application`, never inside `Game`/`RenderSystem`/ECS.**

`Renderer::Submit()` itself needs exactly one small, additive change:
instead of appending directly into a single, global `FrameRecorder`
draw queue, it appends into WHICHEVER PASS is currently executing (i.e.
`RenderGraph::Execute()`'s currently-active `PassContext`, threaded through
via a thin, `Renderer`-owned "current pass" pointer/reference set for the
duration of one pass's `execute` callback and cleared afterward) - this is
the ONE surgical change needed for `Game`/`RenderSystem` to remain 100%
unaware anything changed underneath them, since they still just call
`Renderer::Submit()` exactly as before. **A material texture's descriptor
set (`Renderer::Submit()`'s existing `materialDescriptorSet` parameter,
used by a PMX-imported model's per-submesh diffuse texture) is completely
untouched by all of this - it is an already-live, externally-owned,
always-bound-by-value Vulkan object with no render-graph-managed lifetime
of its own, and continues to flow straight into `vkCmdBindDescriptorSets`
exactly as `FrameRecorder::RecordFrame()` already does it today. It is
NOT one of a pass's declared `ReadTexture()` resources - the render
graph's resource model governs render-TARGET reads/writes between passes,
never a mesh's own material assets.**

### 3.2 - Rewire `Application::Run()` (v2: TWO `Execute()` calls, one per
regime - see V2 Revision Notes 1 and 2)

Replace the existing three blocks (`if (gameTarget != nullptr) { ... }` /
`if (sceneTarget != nullptr) { ... }` / the final `Present()` block) with:

```cpp
// Call 1 of 2: the SYNCHRONOUS offscreen regime - Game view + Scene view
// together, sharing FramePresenter's existing dedicated offscreen command
// buffer/fence exactly as today. Runs unconditionally whenever either
// target is non-null, completely independent of whatever the swapchain
// is doing this frame (a minimized OS window does not affect this call at
// all - see this document's own V2 Revision Note 2).
if (gameTarget != nullptr || sceneTarget != nullptr) {
    m_renderGraph.Execute(offscreenCmd, rg::ExecuteTimingMode::SynchronousImmediateReadback,
        [&](rg::RenderGraphBuilder& b) {
            std::vector<rg::TextureHandle> outputs;
            if (gameTarget != nullptr) {
                const VkExtent2D extent = gameTarget->Extent();
                const float aspect = AspectRatioOf(...);
                const rg::TextureHandle h = b.ImportTexture("GameView", gameTarget->Target(),
                    /*currentLayout=*/ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                AddGameViewPass(b, m_game, m_renderer, h, aspect);
                outputs.push_back(h);
            }
            if (sceneTarget != nullptr) {
                // mirrors gameTarget's block, using IEditorLayer::SceneViewProjection()
                // exactly as today's code does, and the Scene view's own
                // last-known layout - VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                // on every frame after its first, exactly matching what
                // FrameRecorder::RecordFrame()'s final barrier already left
                // it in today.
            }
            return outputs;
        });
}

// Call 2 of 2: the PIPELINED swapchain-present regime - Present alone,
// using FramePresenter's existing per-frame-in-flight command buffer/
// fence/semaphores exactly as today. FramePresenter itself still owns the
// "should I even attempt this" decision (minimized window, pending
// resize, out-of-date swapchain) BEFORE ever calling into RenderGraph -
// exactly like FramePresenter::Present() already returns std::nullopt
// early today, without this call site needing to change shape at all.
const std::optional<DrawStats> presentStats = m_renderer.PresentViaRenderGraph(
    m_renderGraph, [&](rg::RenderGraphBuilder& b, rg::TextureHandle swapchainImage) {
        AddPresentPass(b, swapchainImage, [this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); });
        return std::vector<rg::TextureHandle>{ swapchainImage };
    });
```

This is intentionally a MUCH SIMPLER-LOOKING `Application::Run()` than
today's - that simplicity is a direct, visible dividend of the whole
campaign, not just an implementation detail: the "is this view even
visible" (`gameTarget != nullptr`) logic stays exactly where it already is
(`IEditorLayer::GameViewTarget()`'s existing nullptr-means-hidden
convention, completely unchanged - see AGENTS.md's "Editor Module
Structure"), but the actual SEQUENCING/barrier logic that used to be
hand-written across three separate `if` blocks collapses into two
declarative calls - one per submission regime, never one call spanning
both (see V2 Revision Note 1). `Renderer::PresentViaRenderGraph()` is a
new, small `Renderer`-layer method that internally does exactly what
`FramePresenter::Present()` already does today (acquire the swapchain
image, decide whether to skip this frame entirely, recreate on resize,
etc.) up until the point it used to call `FrameRecorder::RecordFrame()` -
which it now instead replaces with a call into
`m_renderGraph.Execute(cmd, ExecuteTimingMode::PipelinedDeferredReadback, build)`,
still returning `std::optional<DrawStats>` with EXACTLY the same
early-return semantics `FramePresenter::Present()` already has (see
`RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md`'s own Step 3 for why
this preserves the minimized-window/pending-resize behavior automatically,
by construction, rather than needing new logic invented for it).

### 3.3 - Threading `GpuTimingSlot`/`DrawStats` back into `Profiling::GpuPass`

Immediately after each `Execute()` call returns, `Application::Run()`
calls `m_renderGraph.LastKnownStatsFor("GameView")` /
`"SceneView"` / `"Present"` (the exact string literals used when calling
`AddGameViewPass`/etc. above) and feeds each result into
`Profiling::FrameProfiler::Instance().SetGpuPassDrawStats()`/
`SetGpuPassTiming()` exactly as today's code already does - this call
site's SHAPE is unchanged; only WHERE the `DrawStats`/`GpuTimingSample`
values came from (a pass-name lookup into `RenderGraph` instead of two/
three direct `Renderer::RenderOffscreen()`/`Present()` return values)
changes. `RenderGraph`'s own `ExecuteTimingMode`-aware slot table (Phase 6
v2) is what guarantees "GameView"/"SceneView" are always read back
synchronously and "Present" is always read back via the same
frame-in-flight warm-up discipline it uses today - `Application::Run()`
itself doesn't need to know or care which regime applies to which name;
it just asks `LastKnownStatsFor(name)` and gets back whatever is
currently, honestly known for that name (including `Absent`, exactly as
today).

### 3.4 - Migration safety net: build both paths side by side, briefly

For the duration of THIS phase's own development (never merged/left in the
tree afterward), keep the OLD `FrameRecorder`/`FramePresenter`-based path
compiled and reachable behind a single, temporary, code-level boolean
(never a CMake option - this is not a permanent feature toggle, just a
scaffold for A/B comparison during development) so the exact same frame can
be rendered through EITHER path and compared - screenshot-diffed by eye,
and cross-checked against the validation layer - before the old path is
deleted in the same change that flips the default. This mirrors, in
spirit, how this codebase already treats `NullEditorLayer`/`ImGuiEditorLayer`
as two swappable implementations of one interface, just temporarily,
locally, for this one migration's own safety rather than as a permanent
architectural feature.

### 3.5 - Deletion

Once the new path is confirmed correct (3.4) and this phase's own manual
QA pass (3.6) is complete, DELETE: the temporary boolean scaffold from 3.4,
`FrameRecorder::RecordFrame()`'s body (the class itself may be kept as a
thin, deprecated shim briefly if any other TODO-listed feature still
references it, but expect it to be fully retired), and
`FramePresenter::Present()`/`RenderOffscreen()`'s own hand-written
recording bodies - replaced by calls into `RenderGraph::Execute()` (two of
them, per V2 Revision Note 1) with `FramePresenter` reduced to owning
exactly what it must still own forever (the swapchain, per-frame sync
objects, command buffer allocation, the offscreen command buffer/fence -
see Phase 0 v2's own "we will not touch VMA/volk/dynamic rendering" scope
fence; `FramePresenter` still does all of that, it just no longer ALSO
hand-writes barriers/pass sequencing).

### 3.6 - Manual QA checklist (no automated Tier-2 coverage exists for this,
by design - see Phase 0)

- Editor build: Game/Scene/Present all render identically to a pre-Phase-7
  screenshot, in every one of: default docked layout, Scene+Game split
  side by side, Scene+Game tabbed with only one visible, window resize
  mid-session, minimize/restore.
- **v2, new: minimize the OS window and confirm the Editor's own Game/Scene
  views keep updating normally (their own `Execute()` call is untouched by
  the swapchain being unavailable) while only the swapchain Present
  portion pauses - this is the direct behavioral proof that the two-call
  design (V2 Revision Note 1) correctly preserves today's existing
  "minimizing the window doesn't pause the Editor's own off-screen views"
  behavior, which a single, merged `Execute()` call could not have done
  without new, unspecified logic.**
- Validation layers report ZERO new warnings/errors across the same
  scenarios (`kEnableValidation` is already on for every non-`NDEBUG`
  build - `Renderer.cpp`).
- `-DGTE_ENABLE_EDITOR=OFF` release build: straight-to-swapchain rendering
  (the `gameTarget == nullptr && sceneTarget == nullptr` path) still works,
  confirming the render graph handles the "only one pass, no offscreen
  targets at all" degenerate case correctly - in this configuration, only
  the Present-regime `Execute()` call ever runs; the offscreen-regime call
  is skipped entirely (mirroring today's `if (gameTarget == nullptr &&
  sceneTarget == nullptr)` fallback block, now simply "the offscreen
  Execute() call's `if` guard is false").
- "Memory" panel GPU resource count is STABLE frame-to-frame at steady
  state (Phase 4's own pooling promise, now exercised for real).
- "Profiler" panel's GPU Timing/draw-call/triangle-count numbers for Game
  View/Scene View/Present are non-`N/A`/plausible and roughly match
  pre-migration numbers (not bit-identical - GPU timing is inherently
  noisy - but the same ORDER OF MAGNITUDE).
- Full `ctest` suite passes, unchanged pass/fail counts for every test
  outside `tests/Renderer/RenderGraph/` (a strong signal nothing outside
  the rendering internals was disturbed).

### 3.7 - Validate the investment: a real cross-pass read (new in v2)

**This step is not optional busywork - it is the proof that this whole
nine-phase campaign actually delivers the capability it was built for.**
As `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision Note 4
explains, none of the three passes migrated above (3.1-3.6) ever declares
a cross-pass `ReadTexture()` - each only ever writes its own target. The
entire justification for this campaign (shadow maps, G-buffers, post-
processing - all fundamentally "one pass reads another pass's output")
remains, after Phase 7 lands, completely unproven in shipped code.

Immediately after this phase (and Phase 8's debug panel) land, implement
ONE small, real, already-planned feature that exercises a genuine
cross-pass dependency: the Scene-view outline-highlight post-process
already flagged in `TODO.md` ("Editor / Debug UI" - "a Scene-view outline
highlight... a new render pass," listed as a deferred follow-up to
click-to-select raycasting). A minimal version needs only:

- A new pass, declared via the exact same `AddPass()`/`PassBuilder` API
  every other pass uses, that `ReadTexture()`s the Scene view's own just-
  rendered color output (or a small, separately-rendered silhouette mask)
  and writes an outline composited on top - a textbook "pass B reads pass
  A's output" dependency, with a REAL barrier transition (color-attachment-
  write -> shader-read) synthesized by Phase 5's planner and REAL execution
  ordering enforced by Phase 3's compiler, both for the first time in
  production.
- No new picking/raycasting system is required to validate JUST the
  render-graph capability - a fixed/test entity (or the already-selected
  Hierarchy entity, once click-to-select lands separately per `TODO.md`)
  is enough to prove the mechanism works end to end.

This is deliberately scoped as small and narrow as possible - it exists
purely to prove Phase 1-8's investment was real, not to ship a finished
outline-selection feature (that remains its own, separately-scoped
follow-up, exactly as `TODO.md` already describes it). If, in attempting
this, the `ReadTexture()`/`PassContext::ResolveReadTexture()` API from
Phase 2/6 turns out to be awkward or incomplete, THIS is the moment to
discover that - far cheaper to fix here, against one small real pass, than
to discover it later against a larger, more load-bearing feature.

## Step 4: What We Will NOT Do (Focus)

- We will **not** change `Game::Render()`'s, `RenderSystem::Draw()`'s, or
  `Renderer::Submit()`'s PUBLIC SIGNATURES at all - if this phase's
  implementation seems to need a signature change to one of these, stop and
  reconsider the design; the entire value of Phases 1-6 was to make this
  phase possible WITHOUT touching them.
- We will **not** add any NEW rendering feature in this phase (no shadow
  pass, no post-process, no outline highlight) as part of the CORE
  migration (3.1-3.6) - that migration is a like-for-like translation, full
  stop. The one exception is the deliberately tiny, explicitly-scoped
  validation step in 3.7, which exists to PROVE the migration's own
  capability, not to ship a new user-facing feature.
- We will **not** leave the temporary side-by-side scaffold (3.4) in the
  tree past this phase's own completion - it is a development aid, not a
  shipped feature; its deletion is part of THIS phase's own deliverable,
  not a follow-up.
- We will **not** widen `Profiling::GpuPass` in this phase (see Phase 6's
  own Step 4) - the three existing named passes keep exactly their
  existing Profiler-facing identity; only their INTERNAL data source
  changes.
- We will **not** attempt this migration incrementally pass-by-pass (e.g.
  "migrate just Present first, leave Game/Scene on the old path for a
  while") - cut over as one bracketed change, per 3.4. **v2: this now
  means cutting over BOTH `Execute()` calls (offscreen regime and present
  regime) together, in the same change - not one now and one later.**
- We will **not** merge the two `Execute()` calls into one shared command
  buffer/one call "to simplify `Application::Run()` further" - see this
  document's own V2 Revision Note 1/Phase 6 v2. This is a permanent design
  decision, not a stepping stone.

## Step 5: Their Role (What does this mean for you?)

- If you are implementing this phase, budget real time for 3.6's manual QA
  checklist - this is the highest-regression-risk phase in the whole
  campaign specifically BECAUSE everything upstream of it was purely
  additive and low-risk. Do not skip the side-by-side comparison in 3.4 to
  save time; it is what makes 3.6 tractable to actually verify.
- **Do not consider this phase - or this whole campaign - "done" once 3.1-
  3.6 land and pass QA. Step 3.7 is part of this phase's own deliverable,
  not a someday-maybe.** A render graph that has migrated three
  write-only passes and never resolved a real cross-pass read in shipped
  code has not yet proven the thing it was built to prove.
- Write `RENDERGRAPH_PHASE7_COMPLETION_REPORT.md` with an EXPLICIT before/
  after description of `Application::Run()`'s own diff (a short excerpt is
  fine), an explicit note confirming TWO `Execute()` calls per frame (not
  one), and a section describing Step 3.7's outline-pass validation and
  what, if anything, it revealed about the Phase 2/6 API's own ergonomics -
  this is the cleanest place to show it.
- Once this phase lands, treat `FrameRecorder`/the old `FramePresenter`
  recording code as GONE, not "kept around for reference" - a lingering,
  uncalled parallel implementation is a maintenance trap (the next person
  to touch barrier logic might edit the wrong one). Delete it for real, in
  version control, where its history remains recoverable if ever needed.
