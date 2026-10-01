# PHASE3 — Engine-Owned CommandBuffer + Type-Safe Push Constants — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Task doc:**
`PHASE3_ENGINE_COMMAND_BUFFER_AND_TYPE_SAFE_PUSH_CONSTANTS.md`.

## Summary

A brand-new, engine-owned `gte::rg::CommandBuffer` (`src/Renderer/RenderGraph/CommandBuffer.h/.cpp`)
now exists, obtainable from any real pass's `execute` callback via a new `PassContext::Cmd()` method
(`RenderGraph.h`). This is **pure, additive infrastructure** — exactly as scoped: **zero real pass
body was migrated or modified by this phase**, zero existing `PassContext`/`RenderGraph` field was
removed or renamed, and every pre-existing manual
`renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw); renderer.Dispatch(...);
renderer.EndGraphPassRecording();` call site across the engine (CullingPipelines,
GpuSkinningPipelines, all 6 AtmosphereLutRenderer passes, ComputeBlurValidation, GBufferValidation,
etc.) keeps compiling and behaving completely unmodified.

Everything in "Step 3 — The Plan" and the "Acceptance bar" was completed. One design fork from
Step 2.2 was resolved without needing `ask_questions` (see "Design decisions resolved" below) because
re-reading `Renderer::Dispatch()`'s/`Renderer::Submit()`'s real implementations in `Renderer.cpp`
(exactly as the task doc itself suggested doing before escalating) made the correct shape
unambiguous.

## What changed

### `src/Renderer/RenderGraph/CommandBuffer.h`/`.cpp` (new)

`gte::rg::CommandBuffer` — constructed from a `VkCommandBuffer`, a non-owning `Renderer*`, and a
non-owning `DrawStats*` (the same `drawStats` pointer `PassContext::recordDraw`/`recordIndirectDraw`
already carry — no new field needed for this). Public surface, matching the task doc's own sketch
exactly:

- `Native()` — raw `VkCommandBuffer` escape hatch.
- `BindComputePipeline(const ComputePipeline&)` / `BindDescriptorSet(VkDescriptorSet)` — pure,
  non-GPU-touching state accumulators.
- `SetPushConstants(const void*, std::uint32_t)` + templated `SetPushConstants<T>(const T&)` —
  accumulates push-constant bytes; debug-asserts the supplied size against the currently-bound
  `ComputePipeline::PushConstantSize()` via the new pure `PushConstantSizeMatches()` free function
  (R4).
- `Dispatch(groupX, groupY = 1, groupZ = 1)` — issues the real
  `Renderer::BeginGraphPassRecording()` → `Renderer::Dispatch()` → `Renderer::EndGraphPassRecording()`
  bracket, using whatever was most recently bound/set on this instance.
- `DispatchOverSize(width, height, depth = 1)` — computes `groupX/Y/Z` from the bound pipeline's own
  `LocalGroupSize()` (PHASE2) via `ComputeDispatch.h`'s existing `ComputeGroupCount3D()`, then calls
  `Dispatch()`.
- `Draw(const Pipeline&, const Mesh&, const Mat4& modelMatrix = Mat4::Identity(), const Mat4&
  viewProjMatrix = Mat4::Identity(), VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE)` — a
  single, thin forwarder to `Renderer::Submit()`, wrapped in its own
  `BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket too (so a pass author never has to
  call either `Renderer` method directly through `CommandBuffer`, for either compute or graphics).
  No separate `DrawIndexed()` — confirmed directly against `Renderer::Submit()`/
  `FrameRecorder::RecordFrame()`'s real implementation that both already branch on
  `Mesh::HasIndexBuffer()` internally, exactly as the task doc's own Step 2.3 already confirmed.

`PushConstantSizeMatches(suppliedSize, reflectedSize)` is a pure, `constexpr`, Tier-1-testable free
function living in `CommandBuffer.h` (`reflectedSize == 0` — a manually-built `ComputePipeline` with
no reflected metadata — always reports a match, regardless of `suppliedSize`).

### `src/Renderer/RenderGraph/RenderGraph.h`/`.cpp`

- New `#include "CommandBuffer.h"` at the top (same include tier as the other `RenderGraph*.h`
  headers already listed there — confirmed zero circular-include risk: `CommandBuffer.h` only
  forward-declares `Renderer`/`ComputePipeline`/`Pipeline`/`Mesh` and includes the lightweight,
  Vulkan-free `Math/Mat4.h` + `DrawStats.h`; `Renderer.h` never includes `RenderGraph.h` itself, only
  `.cpp` files do).
- `PassContext` gains exactly **one** new field, `Renderer* renderer = nullptr;` (mirrors
  `textures`/`buffers`/`volumeTextures`'s existing "plain non-owning pointer, set once by
  `BuildPassContext()`" shape precisely), and exactly **one** new method,
  `CommandBuffer Cmd() const noexcept { return CommandBuffer(cmd, renderer, recordDraw.drawStats); }`
  — reusing `recordDraw.drawStats` rather than adding a second `DrawStats*` field, since that pointer
  is already exactly what's needed and already set correctly by `BuildPassContext()`.
- `RenderGraph::BuildPassContext()` (`RenderGraph.cpp`) gains one new line,
  `ctx.renderer = m_renderer;` — `RenderGraph` already held this exact non-owning `Renderer*` as its
  own `m_renderer` member (added by the unrelated `editor-core-separation-26` campaign, PHASE6, for
  the `Blit` pass kind), so this is zero new plumbing.

### `CMakeLists.txt` / `tests/CMakeLists.txt`

- `CommandBuffer.h`/`.cpp` added to `gte_core`'s source list (`add_library(gte_core STATIC ...)`),
  right after `RenderGraphTimestampPool.cpp` and before `RenderGraph.h`.
- New `tests/Renderer/RenderGraph/CommandBufferTests.cpp` added to `tests/CMakeLists.txt`'s
  `GTE_TEST_SOURCES` list (a brand-new test file — per `PHASE0`'s own cross-cutting rule, this one
  genuinely needed a new list entry, unlike PHASE2/8/9's "extend an already-registered file" case).

### `tests/Renderer/RenderGraph/CommandBufferTests.cpp` (new)

Three Tier-1 tests for `PushConstantSizeMatches()`:
- Reflected size `0` always matches, for several different supplied sizes (manual-path pipeline case).
- Matching supplied/reflected sizes report `true`.
- Mismatched supplied/reflected sizes (including supplied `0` against a real non-zero reflected size)
  report `false` — this is the real bug class R4 exists to catch.

Everything else `CommandBuffer` does needs a live `VkCommandBuffer`/`Renderer`/`VkDevice` and was only
verified live (Tier 2) — see "Manual/Tier-2 smoke test" below.

## Design decisions resolved (per the task doc's own guidance — no `ask_questions` needed)

1. **Dispatch-accumulation state machine (Step 2.2).** `BindComputePipeline()`/
   `BindDescriptorSet()`/`SetPushConstants()` only accumulate state on the `CommandBuffer` instance —
   they never touch the GPU. Only `Dispatch()`/`DispatchOverSize()` itself issues the real
   `BeginGraphPassRecording()`/`Renderer::Dispatch()`/`EndGraphPassRecording()` bracket, using
   whatever was most recently bound/set. This was confirmed directly from `Renderer::Dispatch()`'s own
   real implementation (`Renderer.cpp` ~line 377-402): that single call already binds the pipeline +
   optionally binds one descriptor set + optionally pushes constants + dispatches, all at once — the
   task doc's own Step 2.2 already anticipated this exact resolution and explicitly said to confirm it
   this way before escalating, so no `ask_questions` call was needed.
2. **Draw() shape (Step 2.3).** Exactly one `Draw()` method, no `DrawIndexed()` — confirmed directly
   against `Renderer::Submit()` (`Renderer.cpp` ~line 269-293) and `FrameRecorder::IssueDrawCommand()`,
   both of which already branch on `Mesh::HasIndexBuffer()` internally. Implemented exactly as the
   task doc's own Step 2.3 already settled.
3. **Where the recordDrawStats-shaped callback for `BeginGraphPassRecording()` comes from.** Rather
   than reusing `PassContext::RecordDrawFn` directly (impossible: `CommandBuffer.h` is included
   *before* `PassContext` is fully defined in `RenderGraph.h`, so the type doesn't exist yet at that
   point), `CommandBuffer` has its own small private
   `MakeRecordDrawStatsCallback() const` method building an equivalent `std::function` closure over its
   own `m_passDrawStats` pointer, mirroring `RecordDrawFn::operator()`'s exact body
   (`AccumulateDrawStats()` guarded by a null check). Shared by both `Dispatch()` and `Draw()`, since
   both open their own bracket.
4. **Which `DrawStats*` to thread into `CommandBuffer`.** Rather than adding a second field to
   `PassContext` for this, `Cmd()` reuses the `DrawStats*` already stored on `recordDraw.drawStats` —
   `BuildPassContext()` already sets `ctx.recordDraw.drawStats = &passDrawStats;`, so this is
   zero-cost reuse, not a new pointer to keep in sync.
5. **`SetPushConstants<T>()`'s data lifetime contract.** Documented explicitly (header comment): `data`
   must stay alive until the matching `Dispatch()`/`DispatchOverSize()` call actually happens — the
   exact same contract `Renderer::Dispatch()`'s own `pushConstants` parameter already has today. No
   copy is taken (would be a silent behavior/perf change the task doc never asked for).

## Manual/Tier-2 smoke test (per the Acceptance Bar's own requirement)

Since a real `CommandBuffer::Dispatch()` needs a live `VkCommandBuffer`/`VkDevice`, this was verified
against a real, running Editor rather than a Tier-1 test:

1. **Baseline capture.** Built the tree as-is (manual 3-line `ComputeBlurValidation.cpp` dispatch
   unchanged), launched `GreatTamanaEditor.exe` (`run_app_background`), activated the "Scene" tab
   (`GET /activate_tab?name=Scene`), enabled the blur pass (`GET
   /render_graph/set_blur_enabled?enabled=true`), and captured `GET
   /get_texture?texture_name=BlurredSceneOutput` (9201 bytes) and `GET /get_swapchain` (113123 bytes).
2. **Throwaway migration.** Temporarily replaced `ComputeBlurValidation.cpp`'s own manual
   `renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw); renderer.Dispatch(...);
   renderer.EndGraphPassRecording();` sequence with:
   ```cpp
   rg::CommandBuffer cmd = ctx.Cmd();
   cmd.BindComputePipeline(*m_pipeline);
   cmd.BindDescriptorSet(m_descriptorSet.Native());
   cmd.SetPushConstants(pushConstants, sizeof(pushConstants));
   cmd.DispatchOverSize(sceneExtent.width, sceneExtent.height);
   ```
   Rebuilt (`cmake --build build --target GreatTamanaEditor`), launched the Editor again, repeated the
   exact same "Scene" tab + blur-enable + capture sequence.
3. **Result: byte-identical.** `GET /get_texture?texture_name=BlurredSceneOutput` returned the exact
   same **9201 bytes** (visually identical blurred sky gradient), and `GET /get_swapchain` returned the
   exact same **113123 bytes** — both captures matched the baseline exactly, confirming
   `CommandBuffer`'s `DispatchOverSize()` path produces a byte-for-byte identical result to the manual
   `Renderer::Dispatch()` call sequence it replaces.
4. **Logs clean.** `GET /get_logs?min_level=Warning` showed only pre-existing, unrelated warnings
   (plugin render-feature priority ties, GPU-timing-slot-budget notes — the same ones PHASE2's own
   completion report already documented) — nothing new, nothing related to `CommandBuffer` or
   `ComputeBlurValidation`.
5. **Reverted.** The throwaway `ComputeBlurValidation.cpp` change was fully reverted back to its
   original manual 3-line form, then rebuilt once more to confirm the tree returns to its original,
   correct state. Confirmed **zero net diff**: `git diff --stat -- src/Editor/ComputeBlurValidation.cpp`
   produced no output at all (the file is byte-for-byte identical to its committed `HEAD` version) —
   `git status` transiently flags it as "modified" due to a harmless stat-cache artifact from the
   edit-then-revert round-trip, not a real content change.

PHASE4 onward (not this phase) is where `ComputeBlurValidation`/`CullingPipelines`/etc. get their real,
permanent, committed migration onto `CommandBuffer`.

## Compile check and targeted test run

- `cmake -S . -B build` (incremental reconfigure) — succeeded, no new dependency downloads.
- `cmake --build build` (incremental, full target set) — succeeded end to end (45/45 steps on the
  first pass that added the new files; a subsequent 8-step incremental rebuild after reverting the
  throwaway `ComputeBlurValidation.cpp` instrumentation also succeeded cleanly), including `gte_core`,
  `gte_editor`, `GreatTamanaEditor`, both Project Assembly demo `.dll`s, and
  `GreatTamanaEngineTests`. Zero warnings/errors from any touched or new file.
- `ctest -R "PushConstantSizeMatchesTests|ShaderReflection" --output-on-failure` (from `build/`) — all
  8 tests pass (3 new `PushConstantSizeMatchesTests` + the 5 pre-existing `ShaderReflectionTests` from
  PHASE1/PHASE2, confirming nothing in the shared reflection path regressed).

Per this campaign's own process rule (Note 4/5), no full clean build or full `ctest` regression pass
was run in this phase — that is reserved for PHASE10.

## Acceptance bar — final check

- [x] `gte::rg::CommandBuffer` exists, compiles, and its pure `PushConstantSizeMatches()` logic has a
      passing Tier-1 test (3 tests, all passing).
- [x] `PassContext::Cmd()` is callable from any real pass's `execute` callback and produces a working
      `CommandBuffer`, manually verified live against a real dispatch (`ComputeBlurValidation`'s own
      blur pass), with a byte-identical captured frame before/after (both the named-texture capture and
      the full swapchain capture matched byte-for-byte).
- [x] Zero existing pass body changed; zero existing `PassContext`/`RenderGraph` field removed or
      renamed (confirmed: the only `PassContext` changes are the one new `renderer` field and the one
      new `Cmd()` method; the only `RenderGraph`/`RenderGraph.cpp` changes are the new include and one
      new line in `BuildPassContext()`).

## Files changed/added this phase

- `src/Renderer/RenderGraph/CommandBuffer.h` (new)
- `src/Renderer/RenderGraph/CommandBuffer.cpp` (new)
- `src/Renderer/RenderGraph/RenderGraph.h` — new include, new `PassContext::renderer` field, new
  `PassContext::Cmd()` method.
- `src/Renderer/RenderGraph/RenderGraph.cpp` — `BuildPassContext()` now also sets `ctx.renderer`.
- `CMakeLists.txt` — `CommandBuffer.h`/`.cpp` added to `gte_core`'s source list.
- `tests/CMakeLists.txt` — new `Renderer/RenderGraph/CommandBufferTests.cpp` entry.
- `tests/Renderer/RenderGraph/CommandBufferTests.cpp` (new)
- `src/Editor/ComputeBlurValidation.cpp` — touched only for the temporary Tier-2 manual verification
  described above, then fully reverted (zero net diff, confirmed via `git diff --stat`).

PHASE4 can now migrate `CullingPipelines`/`GpuSkinningPipelines` onto `CreateComputePipeline(path)`
(PHASE2) + `ctx.Cmd().BindComputePipeline(...)...DispatchOverSize(...)` (this phase), proving the whole
migration pattern once before PHASE5/6/7 repeat it at scale.
