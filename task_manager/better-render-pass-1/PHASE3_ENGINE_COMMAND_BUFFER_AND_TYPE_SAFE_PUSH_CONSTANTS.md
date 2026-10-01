# PHASE3 — Engine-Owned CommandBuffer + Type-Safe Push Constants

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). Depends on `PHASE2` (`ComputePipeline`'s
new `PushConstantSize()`/`LocalGroupSize()` accessors).

---

## Step 1 — The Goal

Build R1 (a new, engine-owned `gte::rg::CommandBuffer`) and R4 (type-safe push constants) as pure,
additive infrastructure — **zero real pass migrated in this phase, zero behavior change anywhere
in the engine**. By the end of this phase:

1. `gte::rg::CommandBuffer` exists (`src/Renderer/RenderGraph/CommandBuffer.h/.cpp`), wrapping a
   `VkCommandBuffer` + a non-owning `Renderer*`, with:
   `BindComputePipeline(const ComputePipeline&)`, `SetPushConstants(const void*, std::uint32_t)`,
   a templated `SetPushConstants<T>(const T&)`, `Dispatch(groupX, groupY = 1, groupZ = 1)`,
   `DispatchOverSize(width, height, depth = 1)`, `Draw(...)` (a thin wrap over
   `Renderer::Submit()`), `BindDescriptorSet(VkDescriptorSet)` (escape hatch), `Native()` (raw
   `VkCommandBuffer` escape hatch for anything not yet covered).
2. `PassContext` gains a way to obtain a `CommandBuffer` — WITHOUT removing or renaming its
   existing raw `VkCommandBuffer cmd` field (Q4's own answer from this campaign's design
   decisions: additive only; the raw field stays forever as a permanent escape hatch and because
   dozens of existing pass bodies already read it directly and must keep compiling unmodified).
3. `CommandBuffer::SetPushConstants<T>()` debug-asserts `sizeof(T) == boundPipeline->PushConstantSize()`
   whenever a compute pipeline is currently bound (R4) — turning the old silent hand-sync bug
   class into a caught assertion.
4. `CommandBuffer` must NOT require its caller to also separately call
   `renderer.BeginGraphPassRecording()`/`EndGraphPassRecording()` — those calls (and `DrawStats`
   bookkeeping) happen automatically inside `CommandBuffer`'s own methods.

---

## Step 2 — The Situation

### 2.1 What `PassContext` already has to build this from (`RenderGraph.h`, confirmed live)

- `RenderGraph` already holds a non-owning `Renderer* m_renderer` member (added by
  editor-core-separation-26 PHASE6 for the `Blit` pass kind) — `RenderGraph::BuildPassContext()`
  (private method, `RenderGraph.h`/`.cpp`) is the ONE place that constructs a `PassContext` per
  pass, per `Execute()` call — this is where a `CommandBuffer` gets threaded in.
- `PassContext::cmd` (raw `VkCommandBuffer`), `recordDraw` (a `RecordDrawFn` callable struct
  holding a non-owning `DrawStats*`), `recordIndirectDraw` (a `RecordIndirectDrawFn`, same shape)
  already exist and are already correctly scoped to exactly one pass's `execute` callback
  lifetime (never stored/copied beyond that call — see `PassContext`'s own extensive doc comment,
  `RenderGraph.h` ~line 641-666).
- `Renderer::BeginGraphPassRecording(VkCommandBuffer cmd, /* the recordDraw-shaped callback */)` /
  `EndGraphPassRecording()` bracket real Vulkan recording state (confirmed via
  `Renderer.h`/`CullingPipelines.cpp`-style real call sites: `renderer.BeginGraphPassRecording(ctx.cmd,
  ctx.recordDraw); renderer.Dispatch(...); renderer.EndGraphPassRecording();`). `CommandBuffer`'s
  own `Dispatch()`/`DispatchOverSize()`/`Draw()` methods must issue this EXACT same bracket
  internally, automatically, once per call — never leave it open across two
  `CommandBuffer` method calls, and never require the pass author to call
  `BeginGraphPassRecording()`/`EndGraphPassRecording()` themselves at all (R1's own explicit
  requirement).

### 2.2 `Renderer::Dispatch()`'s real signature (confirmed, `Renderer.h` ~line 682)

```cpp
void Dispatch(const ComputePipeline& pipeline, VkDescriptorSet descriptorSet, const void* pushConstants,
    std::uint32_t pushConstantBytes, std::uint32_t groupCountX, std::uint32_t groupCountY = 1,
    std::uint32_t groupCountZ = 1);
```

Note this single call ALREADY binds the pipeline + optionally binds ONE descriptor set (set 0) +
optionally pushes constants + dispatches — it does the `vkCmdBindPipeline`/
`vkCmdBindDescriptorSets`/`vkCmdPushConstants`/`vkCmdDispatch` sequence in one call. This means
`CommandBuffer::BindComputePipeline()` + `SetPushConstants<T>()` + `Dispatch()` as THREE separate
method calls (matching the client's own rough sketch: `cmd.BindPipeline(...); cmd.Dispatch(...);`
as two statements) is a DIFFERENT shape than `Renderer::Dispatch()`'s existing all-in-one
signature — `CommandBuffer` must accumulate state across its own method calls (remember the
currently-bound `ComputePipeline*`, remember the currently-bound `VkDescriptorSet`, remember the
currently-staged push-constant bytes) and only actually call `renderer.Dispatch(...)` (or a new,
lower-level `Renderer`/raw-Vulkan path — decide which, see Step 3.1) once `Dispatch()`/
`DispatchOverSize()` itself is finally called. This is the ONE piece of real, novel state-machine
logic this phase introduces — get its exact shape right, and confirm via `ask_questions` if the
three-separate-calls-accumulating-into-one-real-dispatch design feels wrong once you're looking at
`Renderer::Dispatch()`'s real implementation in `Renderer.cpp`.

### 2.3 `Renderer::Submit()`'s real signature (confirmed, `Renderer.h` ~line 484) — for
`CommandBuffer::Draw()`

```cpp
void Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
    const Mat4& viewProjMatrix = Mat4::Identity(), VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE);
```

`CommandBuffer::Draw(const Pipeline&, const Mesh&, ...)` per R1's requirement list is a thin wrap
over this existing method — mirror `Submit()`'s own parameters directly (this engine's whole draw
model is already matrix-push-constant-based, not a separate `BindPipeline()`+`Draw()` two-step the
way compute is): `CommandBuffer::Draw()` is simply a single-call thin forwarder to `Submit()`, and
`CommandBuffer::BindComputePipeline()`/`BindDescriptorSet()` above are COMPUTE-only state (real
per-object graphics draws in this engine are already one-call, not bind-then-draw — there is no
graphics-side `BindPipeline()` at all, and none is needed).

There is deliberately only ONE draw method, `Draw()` — no separate `DrawIndexed()`. Confirmed
directly against the real implementation: `Renderer::Submit()` (`Renderer.cpp` ~line 281-287) already
reads `mesh.HasIndexBuffer()` itself and forwards that bool straight into `FrameRecorder`'s queued
draw item (`FrameRecorder.cpp` ~line 134 branches on it when actually recording), so a single `Mesh`
parameter already transparently covers both the indexed and non-indexed case with zero extra
API surface needed — exactly mirroring `Submit()`'s own existing, already-unified shape.

---

## Step 3 — The Plan

1. **New file `src/Renderer/RenderGraph/CommandBuffer.h/.cpp`.** Class `gte::rg::CommandBuffer`:
   ```cpp
   class CommandBuffer {
   public:
       CommandBuffer(VkCommandBuffer cmd, Renderer& renderer, DrawStats& passDrawStats, /* recordDraw/recordIndirectDraw fns as needed */) noexcept;

       VkCommandBuffer Native() const noexcept { return m_cmd; } // raw escape hatch

       void BindComputePipeline(const ComputePipeline& pipeline) noexcept; // remembers m_boundComputePipeline
       void BindDescriptorSet(VkDescriptorSet set) noexcept; // remembers m_boundDescriptorSet - the escape hatch R1 names for "anyone not using bindless yet"
       void SetPushConstants(const void* data, std::uint32_t size) noexcept; // remembers m_pushConstantData/m_pushConstantSize; debug-asserts size == m_boundComputePipeline->PushConstantSize() when a pipeline is bound and PushConstantSize() != 0
       template <typename T> void SetPushConstants(const T& data) noexcept { SetPushConstants(&data, sizeof(T)); }

       void Dispatch(std::uint32_t groupX, std::uint32_t groupY = 1, std::uint32_t groupZ = 1); // calls renderer.Dispatch(*m_boundComputePipeline, m_boundDescriptorSet, ..., groupX, groupY, groupZ) - asserts m_boundComputePipeline != nullptr in debug
       void DispatchOverSize(std::uint32_t width, std::uint32_t height, std::uint32_t depth = 1); // uses m_boundComputePipeline->LocalGroupSize() + gte::ComputeGroupCount3D() (ComputeDispatch.h), then calls Dispatch()

       void Draw(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(), const Mat4& viewProjMatrix = Mat4::Identity(), VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE); // thin forward to renderer.Submit()
       // No separate DrawIndexed(): confirmed (Renderer.cpp's Submit() implementation, ~line 281-287,
       // and FrameRecorder.cpp ~line 134) that Submit()/FrameRecorder already branch on
       // Mesh::HasIndexBuffer() internally and issue the correct indexed/non-indexed draw command
       // either way - Draw() above is the ONLY method needed; it transparently covers both cases via
       // the SAME single Mesh parameter, exactly like Submit() itself already does today.

   private:
       VkCommandBuffer m_cmd = VK_NULL_HANDLE;
       Renderer* m_renderer = nullptr;
       DrawStats* m_passDrawStats = nullptr; // for BeginGraphPassRecording's recordDraw-shaped callback, if Draw() needs its own bracket separate from ctx.recordDraw
       const ComputePipeline* m_boundComputePipeline = nullptr;
       VkDescriptorSet m_boundDescriptorSet = VK_NULL_HANDLE;
       const void* m_pushConstantData = nullptr;
       std::uint32_t m_pushConstantSize = 0;
   };
   ```
   Exact member list/constructor parameters are a starting sketch — refine once you are looking at
   `RenderGraph::BuildPassContext()`'s real body and can see exactly which pieces
   (`ctx.recordDraw`/`ctx.recordIndirectDraw` vs. a fresh `DrawStats&`) are actually available and
   idiomatic to thread through. `Dispatch()`'s internal call must still go through
   `renderer.BeginGraphPassRecording(m_cmd, /* a recordDraw-shaped callback */)` →
   `renderer.Dispatch(...)` → `renderer.EndGraphPassRecording()` exactly as today's manual 3-line
   pattern does — `CommandBuffer` is what HIDES that bracket from the pass author, it does not
   remove the bracket from existence.
2. **Extract the push-constant size-mismatch check as a pure, Tier-1-testable function** — e.g.
   `bool PushConstantSizeMatches(std::uint32_t suppliedSize, std::uint32_t reflectedSize) noexcept`
   living in a small header (or directly in `CommandBuffer.h` as a free function) — trivial logic
   (`reflectedSize == 0 || suppliedSize == reflectedSize`), but per `AGENTS.md`'s own testability
   rule, every new branch of real logic gets a test, even a one-liner; add
   `tests/Renderer/RenderGraph/CommandBufferTests.cpp` (a BRAND-NEW file — confirmed no such file
   exists yet; also add its path to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list, a plain,
   manually-maintained list, NOT a glob — see `PHASE0`'s own cross-cutting rule on this, a new test
   file not registered there silently never compiles or runs)
   covering: reflected size 0 (manual-path pipeline, always matches), reflected size matching
   supplied size, reflected size NOT matching supplied size (must report false, so the real
   `SetPushConstants<T>()` assert fires).
3. **Thread a `CommandBuffer` through `PassContext`.** Add one new method to `PassContext`
   (`RenderGraph.h`, at the bottom, after `recordIndirectDraw`):
   ```cpp
   CommandBuffer Cmd() const noexcept; // builds a fresh CommandBuffer from this PassContext's own cmd/renderer/drawStats fields
   ```
   `RenderGraph::BuildPassContext()` needs to also forward its own `Renderer* m_renderer` member
   into the constructed `PassContext` (a new, small, non-owning `Renderer* renderer = nullptr;`
   field on `PassContext` itself, mirroring `textures`/`buffers`/`volumeTextures`'s own existing
   "plain non-owning pointer, set once by `BuildPassContext()`" shape exactly) so `Cmd()` has
   something to build a `CommandBuffer` from. This is the ONE new field `PassContext` gains this
   phase — everything else (`cmd`, `recordDraw`, `recordIndirectDraw`, the four `resolve*()`
   methods) is completely untouched.
4. **Do not migrate any real pass yet.** Confirm, by a full compile check, that every existing
   real pass body (AtmosphereLutRenderer, CullingPipelines, GpuSkinningPipelines,
   ComputeBlurValidation, GBufferValidation, etc.) still compiles completely unmodified — this
   phase is additive-only, proven by the fact that NOTHING outside `RenderGraph.h`/`.cpp` and the
   two new `CommandBuffer`/test files changes at all.
5. **Manual/Tier-2 smoke test.** Since a real `CommandBuffer::Dispatch()` needs a live
   `VkCommandBuffer`/`VkDevice`, write one small, throwaway test pass (NOT committed — a local,
   temporary addition to any already-Editor-only debug pass, e.g. a one-off extra dispatch inside
   `ComputeBlurValidation`'s own `execute` callback, reverted before finishing this phase) proving
   `ctx.Cmd().BindComputePipeline(...).SetPushConstants(...).DispatchOverSize(...)` produces the
   exact same visible result as the pre-existing manual
   `renderer.BeginGraphPassRecording(...)/Dispatch(...)/EndGraphPassRecording()` 3-line sequence,
   captured via `gte_send_request` (`/get_swapchain` or `/get_game_view`) before/after. Revert the
   throwaway change once confirmed — PHASE4 is where `ComputeBlurValidation` (or similar) gets its
   REAL, permanent, committed migration.
6. Write `PHASE3_COMPLETION_REPORT.md` (document the exact final `CommandBuffer` API surface and
   every design fork resolved along the way — Step 2.2's dispatch-accumulation shape, Step 2.3's
   draw-shape decision, the `PushConstantSizeMatches()` extraction), commit.

### Acceptance bar for this phase

- `gte::rg::CommandBuffer` exists, compiles, and its pure `PushConstantSizeMatches()` logic has a
  passing Tier-1 test.
- `PassContext::Cmd()` is callable from any real pass's `execute` callback and produces a working
  `CommandBuffer`, manually verified live (Step 5) against at least one real dispatch, with a
  byte-identical captured frame before/after.
- Zero existing pass body changed; zero existing `PassContext`/`RenderGraph` field removed or
  renamed.
