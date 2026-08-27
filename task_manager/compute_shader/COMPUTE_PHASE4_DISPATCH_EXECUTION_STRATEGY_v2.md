# COMPUTE_PHASE4_DISPATCH_EXECUTION_STRATEGY_v2.md

### Child document 4 of 7 — see `COMPUTE_SHADER_MASTER_STRATEGY_v2.md` for the full campaign map.
### Corresponds to the user's requested **"Module 2, Key Implementation Detail B: The Dispatch Math"**.

> **v2 (2nd-iteration review):** this document's Step 1-5 body is IDENTICAL to
> `COMPUTE_PHASE4_DISPATCH_EXECUTION_STRATEGY_v1.md`. New material was
> appended as **Step 6** below, after re-reading this plan directly against
> the real, currently-shipped `Renderer.cpp/.h`, `RenderGraph.cpp/.h`, and
> `RenderPasses.cpp` — specifically how `Renderer::Submit()`/
> `BeginGraphPassRecording()`/`PassContext::recordDraw` actually interact in
> shipped code today. Read Step 6 before implementing `Renderer::Dispatch()`
> — it simplifies (and in one place corrects) the plan below.

## Step 1: The Goal

Teach the engine the correct, reusable arithmetic for turning "I need to
process N total items" into "dispatch this many work groups", and wire up
the actual `vkCmdDispatch` call site (`Renderer::Dispatch()`), mirroring
`Renderer::Submit()`'s existing role for graphics draws.

## Step 2: The Situation

- No `vkCmdDispatch`/`vkCmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE)`
  call exists anywhere in this codebase.
- There is no thread-group-count computation helper anywhere — every
  existing "how many of X do I need" calculation in this engine
  (`DrawStats.h`'s triangle-count math, `RenderGraphNameSlotTable`'s slot
  indexing) is a different *kind* of arithmetic; dispatch math specifically
  needs a **ceiling division** (`groupCount = ceil(totalThreads /
  localGroupSize)`), which, done carelessly with integer division, silently
  truncates and leaves the last partial group of items unprocessed — a
  correctness bug, not just a performance one.
- GLSL compute shaders declare their own local work-group size via
  `layout(local_size_x = X, local_size_y = Y, local_size_z = Z) in;`
  directly in shader source — the C++ dispatch call must independently
  know these same X/Y/Z values to compute the correct group COUNT. Today,
  nothing connects a `.comp` file's declared local size to any C++ constant
  — the two would have to be kept in sync purely by a comment, which is
  fragile (a shader author changing `local_size_x` without updating the
  matching C++ constant is a silent, hard-to-diagnose bug: the shader still
  runs, just processes the wrong number of total threads).
- `cmake/CompileShaders.cmake`'s `gte_add_shader()` currently takes no
  extra preprocessor defines — every existing shader's constants are
  baked directly into its own GLSL source with no build-time parameterization.

## Step 3: The Plan

- **New `src/Renderer/ComputeDispatch.h`** — a small, Vulkan-header-FREE
  pure-math header, mirroring `GpuTiming.h`'s and `DrawStats.h`'s own
  "always-compiled, pure logic, Tier-1-testable" precedent exactly:
  - `std::uint32_t ComputeGroupCount(std::uint32_t totalItems, std::uint32_t
    localGroupSize)` — `(totalItems + localGroupSize - 1) / localGroupSize`,
    with an explicit, documented defensive floor (`localGroupSize == 0`
    is treated as an error condition — `assert`/return 0 — never a
    divide-by-zero) and a regression test for the exact "totalItems is not
    an exact multiple of localGroupSize" case (e.g. 100 items, group size
    64, must yield group count 2, covering all 128 slots including the 28
    that do nothing) — this is the single most important test in this
    whole phase, since it's the case a naive `totalItems / localGroupSize`
    silently gets wrong.
  - A 3D overload, `ComputeGroupCount3D(Extent3D totalItems, Extent3D
    localGroupSize)` (a plain 3-`std::uint32_t` struct, not `VkExtent3D` —
    keep this header Vulkan-free), for a future 2D/3D-dispatched shader
    (e.g. a per-pixel image-processing compute shader dispatched over
    width/height rather than a flat 1D buffer element count).
  - Every GLSL shader's declared local work-group size is restated as a
    named C++ constant living NEXT TO the pass that dispatches it (e.g.
    `constexpr std::uint32_t kBoxBlurLocalSizeX = 16;` near the blur pass's
    own setup code in Phase 7) — not centralized in one giant shared
    constants file, since each compute shader's optimal group size is a
    property of that ONE shader, not a shared engine convention. Document,
    at each such constant, the matching `.comp` file's own
    `layout(local_size_x = ...)` line it must stay equal to.
  - As a STRETCH, more robust option (evaluate during implementation,
    not mandatory for a first landing): extend `gte_add_shader()`
    (`cmake/CompileShaders.cmake`) to optionally accept extra `-D` defines
    forwarded to `glslc` (e.g. `gte_add_shader(target Shaders/BoxBlur.comp
    DEFINES LOCAL_SIZE_X=16 LOCAL_SIZE_Y=16)`), so a single named constant
    can be shared verbatim between the GLSL `layout(local_size_x =
    LOCAL_SIZE_X, ...)` declaration and a matching C++ constant of the exact
    same name/value, generated or asserted-equal at build time. If this
    proves too invasive for a first landing, fall back to the plain
    "restate the number as a comment-linked constant" approach above and
    note the stretch goal as a documented follow-up.
- **`Renderer::Dispatch(const ComputePipeline& pipeline, VkDescriptorSet
  descriptorSet, const void* pushConstants, std::uint32_t pushConstantBytes,
  std::uint32_t groupCountX, std::uint32_t groupCountY = 1, std::uint32_t
  groupCountZ = 1)`** — the compute sibling of `Renderer::Submit()`. Mirrors
  `Submit()`'s existing "queue vs. direct" duality (see `Renderer.cpp`'s
  `Submit()` body): while a render-graph pass is currently being recorded
  (`m_currentGraphPassCmd != VK_NULL_HANDLE`, exactly the same flag
  `Submit()` already checks), issues
  `vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Native())`
  → `vkCmdBindDescriptorSets(...)` (skipped if `descriptorSet ==
  VK_NULL_HANDLE`) → `vkCmdPushConstants(...)` (skipped if
  `pushConstantBytes == 0`) → `vkCmdDispatch(cmd, groupCountX, groupCountY,
  groupCountZ)` directly against the pass's own command buffer. There is no
  "legacy `FrameRecorder`-queued" fallback path the way `Submit()` has for
  graphics — compute dispatch is introduced ONLY as a render-graph-pass
  operation from day one (no pre-existing non-graph compute call site to
  preserve backward compatibility with, unlike `Submit()`'s graphics
  history).
- Optionally, mirror `PassContext::recordDraw`'s own shape with a NEW
  `ctx.recordDispatch(groupCountX, groupCountY, groupCountZ)` callback
  (Phase 6's concern to actually wire up, but worth designing the SHAPE of
  here since `Renderer::Dispatch()`'s signature should already anticipate
  being called from inside a `PassContext`-driven `execute` callback,
  exactly like `Submit()` is today). **[v2] See Step 6 — this bullet is
  superseded; do not implement `ctx.recordDispatch()` as a caller-facing
  API.**

## Step 4: What We Will NOT Do

- No `vkCmdDispatchIndirect` (indirect dispatch driven by GPU-computed
  counts) — every dispatch in this campaign has a CPU-known group count
  at record time. Note this explicitly as future work alongside indirect
  DRAW (already covered by the companion GPU-driven document), should a
  future workload need the GPU itself to decide how much compute work to
  issue.
- No automatic/adaptive work-group-size tuning based on queried hardware
  characteristics (subgroup size, etc.) — every compute shader's local
  size is a fixed, hand-chosen constant.
- No generalized "compute uniform buffer" system beyond the existing
  128-byte push-constant convention already used for graphics — a compute
  shader needing more per-dispatch data than fits in a push constant range
  uses a small, purpose-built UBO/SSBO of its own, documented per-shader.

## Step 5: Their Role

- Write `ComputeGroupCount()`'s tests FIRST, before touching
  `Renderer::Dispatch()` at all — this is genuinely Tier-1-testable, pure
  arithmetic, and it is exactly the kind of function where an off-by-one
  error silently drops the last partial batch of work (100 items at group
  size 64 must dispatch group count 2, never 1).
- Validate `Renderer::Dispatch()` against the same throwaway
  `Passthrough.comp` shader Phase 2 introduced, before attempting the real
  culling (companion document) or blur (Phase 7) workloads — confirm a
  single `vkCmdDispatch(1,1,1)` correctly writes into a bound
  `RWStructuredBuffer` and that the value is readable back on the CPU side
  afterward (a `BufferMemoryUsage::GpuToCpu` readback buffer, or simply
  reading a `CpuToGpu` buffer that was ALSO written by the shader, whichever
  is simpler for a one-off manual test).

---

## Step 6: V2 Revision Notes (2nd-Iteration Review)

Checked directly against the real, currently-shipped `Renderer::Submit()`/
`Renderer::BeginGraphPassRecording()`/`Renderer::EndGraphPassRecording()`
(`src/Renderer/Renderer.cpp`), `RenderGraph::ExecuteCompiledGraph()`
(`src/Renderer/RenderGraph/RenderGraph.cpp`), and the real pass bodies in
`src/Application/RenderPasses.cpp` — two amendments:

1. **Do NOT add `PassContext::recordDispatch` as a new caller-facing API —
   the shipped code already solves this problem more simply, for draws, and
   the same trick applies to dispatch for free.** Reading the real,
   already-landed code shows that pass authors NEVER call
   `ctx.recordDraw(...)` themselves. `RenderPasses.cpp`'s
   `AddGameViewPass()`/`AddSceneViewPass()`/`AddPresentPass()` each do
   exactly this, once, inside their `execute` callback:
   ```cpp
   renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
   game.Render(renderer, aspectWidthOverHeight);   // calls Renderer::Submit() internally, any number of times
   renderer.EndGraphPassRecording();
   ```
   `Renderer::BeginGraphPassRecording()` stores `ctx.recordDraw` as
   `m_currentGraphPassRecordDrawStats`, and `Renderer::Submit()` invokes it
   automatically, every single time it's called, for as long as recording
   is active — the pass author's own draw-issuing code (here,
   `Game::Render()` → `RenderSystem::Draw()` → `Renderer::Submit()`) never
   needs to know a `PassContext` even exists. `Renderer::Dispatch()` should
   follow this EXACT same, already-proven pattern: a compute pass's
   `execute` callback calls `renderer.BeginGraphPassRecording(ctx.cmd, ...)`
   once, then calls whatever compute-issuing code it likes (which calls
   `Renderer::Dispatch()` zero or more times), then
   `renderer.EndGraphPassRecording()` — with `Renderer::Dispatch()` itself
   responsible for automatically fusing into the pass's own stats via the
   SAME stored callback mechanism `Submit()` already uses, if there is
   anything meaningful to fuse at all (see point 2 below, there mostly
   isn't). This removes the need for `ctx.recordDispatch()` entirely: no new
   `PassContext` field, and no new manual-call-site burden on future
   compute-pass authors to remember.
2. **A compute dispatch needs essentially NO new stats-recording plumbing,
   because GPU timing is already fully automatic per-pass regardless of
   content, and there is no compute equivalent of "triangle count."**
   `RenderGraph::ExecuteCompiledGraph()` (`RenderGraph.cpp`) already calls
   `m_timestampPool.WriteBegin(cmd, isPipelined, pipelinedBufferIndex,
   timingSlot)` / `WriteEnd(...)` around EVERY pass in its execution-order
   loop, completely unconditionally — it does not branch on whether that
   pass happens to be graphics or compute. A compute pass therefore gets
   real, driver-measured GPU timing for free, with zero code changes to
   this timing path, the moment it's declared via `AddPass()`/
   `AddComputePass()` (Phase 6). `PassGpuStats::drawStats`
   (`RenderGraphSnapshot.h`) — draw-call count / triangle count — has no
   meaningful equivalent for a dispatch (a "1 dispatch, N threads" count
   isn't currently visualized anywhere the Profiler/Render Graph panel
   shows `drawStats`, and inventing a parallel `dispatchStats` field this
   early, with no second real consumer yet, would be exactly the kind of
   speculative surface-area growth `AGENTS.md`'s "keep `PassContext`'s
   surface area minimal" precedent warns against). **Land this phase with
   `Renderer::Dispatch()` NOT touching `PassGpuStats::drawStats` at all** —
   a pure-compute pass's `RenderGraphSnapshot` row will simply show its
   default, zeroed `drawStats` alongside its real GPU timing, which is
   correct and expected, not a gap. If a future need for per-dispatch
   metrics (e.g. total threads issued) ever arises, add a narrowly-scoped
   field then, with a real consumer already in hand — do not add one now
   "just in case."
3. **`Renderer::Dispatch()` must fail loudly, not silently, when called
   outside of an active `BeginGraphPassRecording()`/`EndGraphPassRecording()`
   bracket.** Since (per this phase's own "What We Will NOT Do") there is
   deliberately no legacy/queued fallback path the way `Submit()` has via
   `FrameRecorder`, a stray `Dispatch()` call with
   `m_currentGraphPassCmd == VK_NULL_HANDLE` has nothing sensible to do at
   all — unlike `Submit()`, which can always fall back to queuing.
   `Dispatch()` should `assert(m_currentGraphPassCmd != VK_NULL_HANDLE &&
   "Renderer::Dispatch() called outside of a render-graph pass recording")`
   (debug builds) and simply no-op in release, mirroring how this engine
   already treats "should never happen, but stay safe if it somehow does"
   conditions elsewhere (e.g. `ResourcePool::TryGet()` on a stale handle).
   v1 never stated this failure mode explicitly.
