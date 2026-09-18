# PHASE2: GPU Skinning → RenderOpaque Blackboard Proof

_Child of `PHASE0_MASTER_STRATEGY.md` — read that file first. Also read
`PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md` — this phase is the first real
consumer of everything PHASE1 built. Part of the `render-pass-3` campaign._

## Step 1: The Goal

Prove the whole new declaration layer works end-to-end, against a REAL,
already-shipping cross-feature dependency, not a toy example — per
`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 3. Concretely: GPU
Skinning's per-model output buffer handles stop being a manually-threaded
`std::vector<rg::BufferHandle> gpuSkinningOutputBuffers` parameter passed
BY HAND into `AddRenderOpaquePass()`, and instead become a real
`RenderPassBlackboard` publish/fetch hand-off between two independent
`RenderPipeline` providers — `"GpuSkinning"` (publishes) and
`"RenderOpaque"` (fetches) — with neither one directly calling or knowing
about the other.

This phase deliberately keeps its blast radius SMALL: only the
Game-View Opaque draw is migrated. Scene View's own opaque draw
(`AddSceneViewPass()`) and `AddPresentPass()` still receive
`gpuSkinningOutputBuffers` the OLD way (an explicit vector, still returned
by the OLD `AddGpuSkinningPasses()` free function, still threaded by hand)
— unifying those onto the new system too is PHASE3's job, once the full
per-view provider loop exists. This phase is the narrow proof; PHASE3 is
the broad sweep.

## Step 2: The Situation

- `AddGpuSkinningPasses()` (`RenderPasses.cpp`) declares one
  `AddRenderPass(request.name, PassKind::Compute, ViewScope::Shared,
  RenderPassCategory::GpuSkinning, ...)` per GPU-skinning dispatch request,
  and returns every resulting `BufferHandle` in a plain
  `std::vector<rg::BufferHandle>`.
- `Application.cpp`'s `build` lambda (around line 560) calls
  `AddGpuSkinningPasses(b, m_game, m_renderer)` ONCE, then threads the
  resulting vector, BY HAND, into `AddRenderOpaquePass(...,
  gpuSkinningBuffers, ...)`, `AddSceneViewPass(...,
  gpuSkinningBuffers, ...)`, and `AddPresentPass(...,
  gpuSkinningBuffers)` later in the SAME lambda.
- `AddRenderOpaquePass()` (`RenderPasses.cpp`) calls
  `DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers)` inside its own
  `setup` lambda, which declares a phantom
  `ResourceAccess::VertexBufferRead` against every handle in the vector —
  this is what forces the barrier planner to order `"RenderOpaque"` AFTER
  whichever GPU Skinning compute pass(es) wrote them. This exact behavior
  MUST be preserved bit-for-bit by this phase — only the MECHANISM that
  gets the handles from GPU Skinning to Opaque changes, never the
  resulting barrier/ordering behavior.
- PHASE1 already built `RenderPipeline`/`RenderPassBlackboard`/
  `RenderPassDesc`/`RenderPassFrameContext` — this phase is the first place
  any of it is actually constructed and driven from `Application.cpp`.

## Step 3: The Plan

### 3.1 — `Application` gains a `RenderPipeline` member and registers two providers

Add `rg::RenderPipeline m_offscreenRenderPipeline;` as a new
`Application` member (`Application.h`/`.cpp`) — this SAME instance is
reused and grown by PHASE3; this phase only registers its first two
providers, at construction time (wherever `Application`'s constructor/
`Init()` already does one-time setup):

```cpp
constexpr rg::RenderPassId kGpuSkinningOutputsKey = "GpuSkinning.OutputBuffers"_passId;

m_offscreenRenderPipeline.Register("GpuSkinning", rg::ProviderScope::Once,
    [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
        // Body is a direct, literal translation of AddGpuSkinningPasses()'s
        // own existing loop (RenderPasses.cpp) - same
        // CollectGpuSkinningDispatchRequests()/ImportBuffer()/pipeline
        // selection/Dispatch() call, just producing RenderPassDesc values
        // into `out` instead of calling builder.AddRenderPass() directly,
        // and PUBLISHING the resulting handle vector onto the blackboard
        // instead of returning it.
        const std::vector<AnimationSystem::GpuSkinningDispatchRequest> requests =
            m_game.CollectGpuSkinningDispatchRequests();
        std::vector<rg::BufferHandle> handles;
        handles.reserve(requests.size());
        for (const auto& request : requests) {
            // NOTE: ImportBuffer() must still be called against the SAME
            // builder this frame - see Step 3.2's "setup runs against a
            // real PassBuilder&, but ImportBuffer() is a RenderGraphBuilder&
            // method, not a PassBuilder& one" resolution below.
            ...
        }
        frame.blackboard.Publish<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey, handles);
    });
```

**A real, important wrinkle to resolve here (do not skip)**:
`RenderGraphBuilder::ImportBuffer()` is a method on `RenderGraphBuilder&`
itself, not on `PassBuilder&` — but a `RenderPassProvider` callback
(`(const RenderPassFrameContext&, std::vector<RenderPassDesc>&) -> void`,
per PHASE1) has no `RenderGraphBuilder&` parameter at all, by design (the
whole point of the collect/sort/declare split in `RenderPipeline::
DeclareInto()` is that providers run BEFORE any real `AddRenderPass()`
call happens). Resolve this exactly the way the design doc's own Section
3 diagram implies passes are described BEFORE being added: a provider
cannot call `ImportBuffer()` itself. Instead, capture the needed
`rg::BufferHandle` INSIDE each `RenderPassDesc.setup` lambda by calling
`RenderGraphBuilder::ImportBuffer()` from THERE instead — `setup` already
receives a real `PassBuilder&`, but `ImportBuffer()` needs the owning
`RenderGraphBuilder&`, not the `PassBuilder&`. If `PassBuilder` has no way
to reach its owning builder, add ONE small, additive accessor to
`RenderGraphBuilder::PassBuilder` for this purpose (e.g. a private
`RenderGraphBuilder& m_owner` reference stored alongside `PassRecord&
m_pass`, with a new public accessor) — **this is the ONE place this phase
is allowed to touch `RenderGraphBuilder.h` beyond PHASE1's own trailing
`renderPassEvent` parameter, and only if no cleaner alternative exists**.
Before doing this, check whether `ImportBuffer()`'s result (a plain
`BufferHandle`, cheap POD) can instead be resolved once, OUTSIDE any
provider entirely, by `RenderPipeline::DeclareInto()`'s own caller
(`Application::Run()`) BEFORE calling `DeclareInto()` at all, and handed
into the frame context as ordinary per-frame data — if that resolves
cleanly (it likely does: `ImportBuffer()` only needs `request.name`/
`request.outputBuffer`/`request.outputBufferSize`, all already known to
`Application::Run()` via `m_game.CollectGpuSkinningDispatchRequests()`
BEFORE `DeclareInto()` runs), PREFER that — it keeps `RenderGraphBuilder.h`
completely untouched by this phase, which is the safer, lower-risk
resolution. Document whichever path is actually taken, loudly, in this
phase's own completion report.

```cpp
m_offscreenRenderPipeline.Register("RenderOpaque", rg::ProviderScope::PerActiveView,
    [this, gameViewTarget, aspectWidthOverHeight, frameDebuggerCapture](
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
        // This phase deliberately keeps this provider Game-View-only in
        // PRACTICE (frame.activeViews passed into THIS pipeline's own
        // DeclareInto() call, this phase, only ever contains the Game View
        // id - see Step 3.2) even though it is registered PerActiveView -
        // PHASE3 is what actually starts passing more than one view in,
        // and generalizes this provider's body to branch on
        // frame.currentView for target/camera selection. For THIS phase,
        // the body is a direct translation of AddRenderOpaquePass()'s
        // existing setup/execute lambdas (RenderPasses.cpp), with ONE
        // change: gpuSkinningOutputBuffers is no longer a captured
        // parameter - it is fetched from the blackboard instead:
        const std::vector<rg::BufferHandle> gpuSkinningBuffers =
            frame.blackboard.Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey).value_or(
                std::vector<rg::BufferHandle>{});

        rg::RenderPassDesc desc;
        desc.id = "RenderOpaque"_passId;
        desc.debugName = "RenderOpaque";
        desc.kind = rg::PassKind::Graphics;
        desc.order = rg::RenderPassEvent::Opaques;
        desc.view = frame.currentView;
        desc.legacyCategory = rg::RenderPassCategory::General; // unchanged Frame Debugger behavior
        desc.setup = [gameViewTarget, gpuSkinningBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(gameViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(gameViewTarget, kGameClearDepth);
            DeclareGpuSkinningReads(pass, gpuSkinningBuffers); // UNCHANGED helper, RenderPasses.cpp
        };
        desc.execute = [this, aspectWidthOverHeight, frameDebuggerCapture](rg::PassContext& ctx) {
            m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            m_game.Render(m_renderer, aspectWidthOverHeight, nullptr, frameDebuggerCapture);
            m_renderer.EndGraphPassRecording();
        };
        out.push_back(std::move(desc));
    });
```

### 3.2 — `Application.cpp`'s `build` lambda: drive the new pipeline

Inside the SYNCHRONOUS offscreen `Execute()` call's `build` lambda (the
same one that today calls `AddGpuSkinningPasses()`/`AddRenderOpaquePass()`
directly), REPLACE the direct `AddGpuSkinningPasses(b, m_game, m_renderer)`
+ `AddRenderOpaquePass(b, m_game, m_renderer, gameViewTarget, aspect,
gpuSkinningBuffers, frameDebuggerCapture)` call PAIR (only for the Game
View case — the `if (gameTarget != nullptr)` block) with:

```cpp
rg::RenderPassBlackboard blackboard;
blackboard.BeginFrame();
rg::RenderPassFrameContext frame{ /* activeViews = */ {}, /* currentView = */ rg::RenderViewId::Shared(), blackboard };
if (gameTarget != nullptr) {
    frame.activeViews.push_back(rg::RenderViewId::Named("Game"));
}
m_offscreenRenderPipeline.DeclareInto(b, frame);
#ifndef NDEBUG
blackboard.ReportUnusedPublishesIfAny();
#endif
for (const rg::TextureHandle handle : frame.finalTextureOutputs) { outputs.push_back(handle); }
```

`AddGpuSkinningPasses()`'s OLD free function, and `AddRenderOpaquePass()`'s
OLD free function, are BOTH KEPT, UNCHANGED, in `RenderPasses.cpp`/`.h` —
`AddSceneViewPass()` and `AddPresentPass()` STILL call the OLD
`AddGpuSkinningPasses()`-returned vector directly (call it a SECOND time,
or restructure so the SAME vector is available to both the new provider
path and the old direct calls — whichever is cleaner; a duplicate GPU
Skinning dispatch per frame would be a real, confirmed correctness bug, so
if `AddGpuSkinningPasses()` genuinely gets called twice this phase must
instead publish the SAME handles the new provider already fetched into a
local variable and pass THAT into `AddSceneViewPass()`/`AddPresentPass()`,
never re-running `AddGpuSkinningPasses()` a second time). Resolve this
explicitly, and state clearly in the completion report which of the two
approaches was taken.

### 3.3 — Verify identical behavior, not just "it compiles"

After wiring this up: launch the engine (`run_app_background`), confirm
via `gte_send_request /get_game_view` that a GPU-skinned/animated model
(if one is available in the current test scene/project — if not, this
step may confirm generic Opaque rendering is unaffected instead, and this
must be called out explicitly as a limitation of this phase's own live
verification, not silently skipped) still renders correctly with no visual
regression, and that the Frame Debugger (`GET /frame_debugger/capture` +
`/get_swapchain`) still shows `"RenderOpaque"` exactly as before (same
tree position, same expand-arrow/child behavior from `render-pass-2`) —
this phase must NOT change anything a human/HTTP client can observe about
the Frame Debugger's own tree shape, only the internal mechanism by which
`"RenderOpaque"`'s pass gets declared.

## Definition of Done

- `Application` owns a real `rg::RenderPipeline m_offscreenRenderPipeline;`
  member with exactly two providers registered: `"GpuSkinning"` (`Once`)
  and `"RenderOpaque"` (`PerActiveView`, invoked with exactly one active
  view — `"Game"` — this phase).
- GPU Skinning's output buffer handles reach `"RenderOpaque"`'s own
  `setup` lambda ENTIRELY through `RenderPassBlackboard::Publish`/`Fetch`,
  with zero direct call or shared captured variable between the two
  providers' own registration lambdas.
- `AddSceneViewPass()`/`AddPresentPass()` still receive GPU Skinning's
  buffer handles correctly (same barrier/ordering guarantee as before this
  phase), with GPU Skinning's own compute dispatch still declared exactly
  ONCE per frame (never twice).
- An incremental compile of `gte_core` succeeds.
- A live, HTTP-driven check (`run_app_background` +
  `gte_send_request`) confirms Game View still renders correctly and the
  Frame Debugger's tree shape for `"RenderOpaque"` is visually unchanged
  from before this phase.
- `PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md`'s own
  `ReportUnusedPublishesIfAny()` mechanism does NOT fire a warning during
  a normal frame where GPU Skinning legitimately has nothing to publish
  (an empty `requests` vector still means `Publish()` is called with an
  empty `std::vector<rg::BufferHandle>{}`, which — depending on how
  "unused" is defined — may or may not need special-casing; verify this
  explicitly and document whichever behavior results).

## What We Will NOT Do

- Do NOT migrate `AddSceneViewPass()`/`AddPresentPass()` onto the new
  provider system yet — they keep using the OLD, hand-threaded
  `gpuSkinningOutputBuffers` vector parameter, unchanged, in this phase.
  That migration is PHASE3's job.
- Do NOT migrate Atmosphere/Sky/Transparent onto the new system yet — this
  phase touches ONLY GPU Skinning + the Game-View Opaque pass.
- Do NOT attempt to generalize `"RenderOpaque"`'s provider body to branch
  on `frame.currentView` for Scene View support yet — `frame.activeViews`
  in THIS phase only ever contains the Game View id. PHASE3 does the real
  per-view generalization.
- Do NOT touch `FrameDebuggerData.cpp` in any way in this phase — the
  underlying `PassRecord`/`RenderGraphPassSnapshot` fields
  (`viewScope`/`category`/`kind`/`drawKind`) this pass produces via
  `RenderPipeline::DeclareInto()`'s internal translation must be
  BIT-FOR-BIT IDENTICAL to what the OLD direct `AddRenderOpaquePass()` call
  used to produce, so the Frame Debugger cannot tell the difference.
