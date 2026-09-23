# PHASE2 — Completion Report: Extract `ExecuteCompiledGraph()`'s Six Concerns (item 2.6)

## Parent

`PHASE0_MASTER_STRATEGY.md` / `PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md`.
Read `PHASE1_COMPLETION_REPORT.md` first, per that document's instructions —
its own PHASE1 addition (the slot-budget overflow diagnostic, right after
`timingSlots.AssignOrGetSlot(pass.name)`) was already present in
`ExecuteCompiledGraph()` before this phase started, and is preserved
byte-for-byte, untouched, in its original position within the per-pass loop.

## What was done

Implemented exactly the plan in `PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md`,
using the five method names/signatures given in Step 3.1 as a starting point
(one small signature deviation is noted below, itself explicitly anticipated
by the plan's own "Confirm the exact parameter shapes above compile cleanly…"
guidance, not a scope change).

### 1. `RenderGraph.h` — five new private method declarations

Added, immediately after the existing `ApplyUsageBarrierIfNeeded()`
declaration (same private section, same style/doc-comment convention as the
already-extracted `EnsureTextureResolved`/`EnsureBufferResolved`/
`EnsureVolumeTextureResolved`/`ApplyUsageBarrierIfNeeded`):

```cpp
PassContext BuildPassContext(VkCommandBuffer cmd, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers, std::vector<PhysicalVolumeTexture>& physicalVolumeTextures,
    DrawStats& passDrawStats);

std::vector<VkRenderingAttachmentInfo> BuildColorAttachmentInfos(
    const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures,
    std::vector<VkExtent2D>& outResolvedExtents) const;

std::optional<VkRenderingAttachmentInfo> BuildDepthAttachmentInfo(
    const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures, TextureHandle depthHandle) const;

void RegisterDebugTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
    const std::vector<PhysicalTexture>& physicalTextures);
void RegisterDebugVolumeTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
    const std::vector<PhysicalVolumeTexture>& physicalVolumeTextures);
```

All five signatures match Step 3.1's draft exactly — no signature deviation
was needed for these declarations themselves.

### 2. `RenderGraph.cpp` — bodies moved verbatim, `ExecuteCompiledGraph()` shrunk

- **`BuildPassContext()`**: the exact `PassContext ctx; ctx.cmd = cmd; ...
  ctx.recordIndirectDraw = [...]` block, all six lambda wire-ups, moved
  verbatim (comments included) into the new method's body. `passDrawStats`
  stays declared in `ExecuteCompiledGraph()`'s own per-pass loop scope (NOT
  inside `BuildPassContext()`), exactly as the plan required, since
  `UpdateDrawStatsFor(pass.name, passDrawStats)` at the bottom of the loop
  still needs to read it after `pass.execute(ctx)` returns. The call site is
  now:
  ```cpp
  DrawStats passDrawStats;
  PassContext ctx = BuildPassContext(cmd, physicalTextures, physicalBuffers, physicalVolumeTextures, passDrawStats);
  ```
- **`BuildColorAttachmentInfos()`**: the color-attachment-info-building loop
  (the `for (const ColorAttachmentDesc& desc : pass.colorAttachments) {...}`
  block, including its debug `assert()` and every doc comment) moved
  verbatim, now filling a caller-supplied `outResolvedExtents` out-parameter
  and returning the built `std::vector<VkRenderingAttachmentInfo>`.
  `ExecuteCompiledGraph()` keeps the `FindMismatchedColorAttachmentExtent()`
  throw check, the `vkCmdBeginRendering`/`vkCmdSetViewport`/`vkCmdSetScissor`
  calls, and `ctx.colorAttachmentExtent = firstExtent;` — exactly as the plan
  specified (these stay as "assemble + issue Vulkan calls", not "build the
  data").
- **`BuildDepthAttachmentInfo()`**: the depth-attachment-building block moved
  verbatim into a method returning `std::optional<VkRenderingAttachmentInfo>`,
  gated on `depthHandle.IsValid()` exactly as Step 3.1 anticipated — no
  separate bool parameter needed. **One small, deliberate cleanup beyond pure
  code motion**: the caller's own now-redundant standalone `bool
  hasDepthWrite` local (which the original code set in lockstep with
  `depthHandle` inside the same `for (const ResourceUsage& usage :
  pass.writes)` scan, but which nothing downstream actually branches on once
  `depthHandle.IsValid()` is the real signal) was removed rather than kept
  as an unused/`(void)`-cast variable — the scan loop itself is otherwise
  byte-for-byte identical (same iteration, same `TargetsDepthState()` check,
  same `depthHandle = usage.texture;` assignment). This changes zero
  runtime behavior (the two variables were always kept in exact lockstep;
  `hasDepthWrite` was pure redundant bookkeeping the original code never
  branched on differently from `depthHandle`'s own validity) and was called
  out via `ask_questions`-style judgment as a "small implementation-detail
  choice", per the plan's own explicit allowance for this in Step 3.1's last
  paragraph — not a deviation requiring a design-level confirmation.
- **`RegisterDebugTextureSnapshots()`/`RegisterDebugVolumeTextureSnapshots()`**:
  both passive-registration loops moved verbatim, including their original
  network-impl-4/network-impl-6 comments. `ExecuteCompiledGraph()`'s tail is
  now:
  ```cpp
  RegisterDebugTextureSnapshots(timingMode, input, physicalTextures);
  RegisterDebugVolumeTextureSnapshots(timingMode, input, physicalVolumeTextures);
  ```

### 3. What `ExecuteCompiledGraph()` looks like now

The function now reads as the plan's own described "resolve → barrier →
begin → execute → end → record" skeleton: pipelined-timing preamble → Compile()
→ per-pass loop (barriers → timing-slot assignment/overflow diagnostic →
`BuildPassContext()` → `if (hasColorWrite) { BuildColorAttachmentInfos() →
mismatch check → BuildDepthAttachmentInfo() → vkCmdBeginRendering/viewport/
scissor }` → `pass.execute(ctx)` → `vkCmdEndRendering` → timing end/draw-stats
update) → pipelined-frame-counter increment → `RegisterDebugTextureSnapshots()`/
`RegisterDebugVolumeTextureSnapshots()` → `BuildRenderGraphSnapshot()`. Every
Vulkan call, every barrier, every data value computed is identical to before
this phase — confirmed by a close line-by-line comparison against the
pre-PHASE2 file content (captured via `read_file` before any edit was made).

## Deviations from the plan

- The one `hasDepthWrite` cleanup described above (removing an unused local
  rather than keeping a dead/`(void)`-suppressed one) — a genuinely smaller
  diff than keeping it, zero behavior change, explicitly anticipated by the
  plan's own "small signature adjustments discovered mid-implementation are
  normal engineering, not a locked design decision" allowance. No sixth
  extracted method was added, and no method's agreed scope/shape changed
  beyond this one local-variable cleanup.
- No other deviation. `PassContext` was NOT converted away from
  `std::function` (that is PHASE3's job, untouched here).
  `RenderGraphCompiler.cpp`/`RenderGraphBuilder.h` were NOT touched (PHASE4/
  PHASE5's job).

## Verification

- Fast, targeted incremental compile check (no full build, per Locked Design
  Decision 7):
  - `cmake --build build --target gte_core` — succeeds, 0 errors/warnings
    from the touched files (`RenderGraph.h`/`RenderGraph.cpp` both rebuilt
    cleanly, along with their handful of dependent translation units).
  - `cmake --build build --target GreatTamanaEngineTests` — succeeds
    (link-only — no test source file references `RenderGraph`'s newly
    extracted private methods directly, since they are private
    implementation detail, not new public surface).
- Live sanity check (per this phase's own "encouraged, high-value" Definition
  of Done bullet, given this touches the engine's hottest per-frame path):
  - Launched `GreatTamanaEngine.exe` via `run_app_background`.
  - `GET /get_swapchain` and `GET /get_game_view` both confirm the Editor
    renders normally — sky/atmosphere gradient visible in both Scene and Game
    panels, correct docked layout, no black/corrupted frame.
  - `GET /get_logs?min_level=Warning` → `{"count":0,...}` — confirms the
    PHASE1 slot-budget-overflow diagnostic does **not** fire spuriously
    during ordinary operation (this phase's refactor didn't disturb that
    call site's placement/behavior).
  - `GET /activate_tab?name=Render Graph` + a follow-up `GET /get_swapchain`
    confirms the "Render Graph" panel still shows every real pass (7
    Atmosphere LUT/composite passes + `RenderOpaque`, ...) with correct,
    real per-pass GPU timing (e.g. `AtmosphereMultiScatteringLut` at 2.20 ms)
    and correct Reads/Writes columns — i.e. `BuildRenderGraphSnapshot()`'s own
    downstream consumers see byte-identical data to before this phase.
  - Stopped the app afterward via `stop_app_background`.

## What was NOT touched (per plan's own "what NOT to do" list)

- `PassContext`'s field types — still `std::function` (PHASE3's job).
- The ORDER of any Vulkan call, any barrier, or any computed data value —
  none changed; this was a pure code-motion refactor plus the one documented
  dead-variable cleanup above.
- `RenderGraphCompiler.cpp`/`RenderGraphBuilder.h` — untouched (PHASE4/
  PHASE5's job).

## Next phase

`PHASE3_PASSCONTEXT_PLAIN_RESOLVERS.md` (item 2.7) — replacing `PassContext`'s
six `std::function` fields with plain non-owning pointers/ordinary member
functions. No blocker or open question was found that would change PHASE3's
plan; `BuildPassContext()` (this phase's own extraction) is now the one
single, easy-to-diff call site PHASE3's internals swap needs to change.
