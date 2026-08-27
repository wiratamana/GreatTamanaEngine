# COMPUTE_PHASE4_COMPLETION_REPORT.md

Session report for **Phase 4 — Dispatch GPU workers** (the "Dispatch
Execution" phase) of the compute-shader campaign described in
`COMPUTE_SHADER_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`COMPUTE_PHASE4_DISPATCH_EXECUTION_STRATEGY_v2.md` — including its own
Step 6 "V2 Revision Notes" (no `PassContext::recordDispatch` callback, no
`PassGpuStats::drawStats` fusion, and an explicit assert-then-no-op failure
mode for a dispatch issued outside a render-graph pass recording). Nothing
beyond that document's own "Step 3: The Plan" was implemented, per its own
"Step 4: What We Will NOT Do".

Work was done on the pre-existing branch `feature/compute-shader-impl`,
picking up immediately after Phase 3 (`COMPUTE_PHASE3_COMPLETION_REPORT.md`).

## What shipped

Every change is purely additive to already-shipped `Renderer` code — no
existing call site's observable behavior changed, and no render-graph
integration work was touched at all (that's Phase 6's job).

### New file: pure dispatch math

- **`src/Renderer/ComputeDispatch.h`** — a single new header, deliberately
  Vulkan-header-FREE and pure (mirrors `DrawStats.h`'s/`GpuTiming.h`'s own
  "always-compiled, pure logic, Tier-1-testable" precedent exactly):
  - `Extent3D` — a plain 3-`std::uint32_t` struct (width/height/depth), the
    Vulkan-free sibling of `VkExtent3D`, used only by the 3D overload below
    so this header never has to include `<volk.h>`.
  - `ComputeGroupCount(totalItems, localGroupSize)` — the one and only
    place in the engine that performs the "how many work groups do I need"
    ceiling-division arithmetic (`(totalItems + localGroupSize - 1) /
    localGroupSize`), with an explicit, documented defensive floor
    (`localGroupSize == 0` asserts in debug builds and returns `0` in
    release, never a divide-by-zero).
  - `ComputeGroupCount3D(totalItems, localGroupSize)` — the 2D/3D sibling,
    applying the exact same formula independently per axis, for a future
    shader dispatched over e.g. an image's width/height rather than a flat
    1D buffer element count (Phase 7's own blur workload will need this).

### `Renderer::Dispatch()` — the compute sibling of `Renderer::Submit()`

- **`Renderer::Dispatch(const ComputePipeline& pipeline, VkDescriptorSet
  descriptorSet, const void* pushConstants, std::uint32_t
  pushConstantBytes, std::uint32_t groupCountX, std::uint32_t groupCountY =
  1, std::uint32_t groupCountZ = 1)`** (new method, `Renderer.h`/`.cpp`) —
  issues a real `vkCmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE)` →
  (optionally) `vkCmdBindDescriptorSets` (skipped when `descriptorSet ==
  VK_NULL_HANDLE`) → (optionally) `vkCmdPushConstants` with
  `VK_SHADER_STAGE_COMPUTE_BIT` (skipped when `pushConstantBytes == 0`) →
  `vkCmdDispatch(groupCountX, groupCountY, groupCountZ)` sequence, directly
  against whichever render-graph pass's command buffer is currently being
  recorded (`m_currentGraphPassCmd`, the exact same member
  `BeginGraphPassRecording()`/`EndGraphPassRecording()` already maintain
  for `Submit()`).
- **No legacy/queued fallback path exists, by design** — unlike `Submit()`,
  which falls back to `FrameRecorder`'s queued draw list when no
  render-graph pass is currently being recorded (a pre-existing,
  backward-compatibility concern `Submit()` has always had to honor),
  `Dispatch()` has no pre-existing non-graph compute call site to preserve
  compatibility with. A `Dispatch()` call outside an active
  `BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket therefore
  `assert()`s loudly in debug builds and is a safe, silent no-op in
  release — mirroring this engine's existing "should never happen, but
  stay safe if it somehow does" convention (see `AGENTS.md`).
- **No `PassContext::recordDispatch` callback was added** — per the
  strategy document's own Step 6 finding (checked directly against the
  real, shipped `Renderer::Submit()`/`BeginGraphPassRecording()`/
  `RenderPasses.cpp` code): a graphics pass's `execute` callback never
  calls `ctx.recordDraw` itself today — it calls
  `renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw)` once, then
  calls whatever draw-issuing code it likes, which calls
  `Renderer::Submit()` (which invokes the stored callback automatically).
  A future compute pass's `execute` callback follows the exact same shape:
  call `BeginGraphPassRecording()` once, call `Renderer::Dispatch()` zero
  or more times, call `EndGraphPassRecording()`. This removes an entire
  category of new `PassContext`/`RenderGraphBuilder` surface area this
  phase would otherwise have needed to add.
- **`Dispatch()` deliberately never touches `PassGpuStats::drawStats`** —
  per the strategy document's own Step 6 finding, there is no meaningful
  compute equivalent of a "triangle count" today, and GPU TIME for a
  compute pass is already fully automatic regardless of pass content:
  `RenderGraph::ExecuteCompiledGraph()`'s `WriteBegin()`/`WriteEnd()` calls
  already bracket EVERY pass unconditionally, with no branch on
  graphics-vs-compute content. A pure-compute pass's eventual
  `RenderGraphSnapshot` row will show its default, zeroed `drawStats`
  alongside its real GPU timing — correct and expected, not a gap.

## Testing

- **`tests/Renderer/ComputeDispatchTests.cpp`** (new) — pure, Tier-1
  coverage of `ComputeGroupCount()`/`ComputeGroupCount3D()`, written FIRST
  per the strategy document's own "Step 5: Their Role" instruction, before
  touching `Renderer::Dispatch()` at all:
  - Exact multiples divide evenly (128/64 → 2, 64/64 → 1).
  - **The single most important case in this file**: a non-exact multiple
    rounds UP, never truncates (100 items at group size 64 → group count
    2, never the truncating-division 1 that would silently drop the last
    36 items).
  - A single item still needs one group; zero items need zero groups.
  - A local group size of 1 matches the item count exactly.
  - A large item count (1,000,000 at group size 256) computes correctly
    without overflow for realistic values.
  - The 3D overload applies the ceiling-division formula independently
    per axis, both for a non-exact multiple (130×70×1 at 8×8×1 → 17×9×1)
    and an exact one (64×32×4 at 16×16×4 → 4×2×1).
  - `Renderer::Dispatch()` itself is Tier 2 (GPU-touching, needs a real
    `VkCommandBuffer`/`VkPipeline`/render-graph pass in flight) by nature,
    falling into the same accepted "no automated coverage yet, manually
    verified" bucket as `Renderer::Submit()`/`ComputePipeline`/
    `GpuResourceFactory` themselves (see `TESTING.md`'s own note on this).
    No new Tier-1-testable pure logic beyond the dispatch math itself was
    introduced this phase.

## Build system changes

- Root `CMakeLists.txt`: added `src/Renderer/ComputeDispatch.h` to
  `gte_core`'s source list (right after `Renderer/ComputeDescriptorSet.h`).
- `tests/CMakeLists.txt`: added `Renderer/ComputeDispatchTests.cpp` to
  `GTE_TEST_SOURCES` (right after `Renderer/GpuTimingTests.cpp`), plus a
  matching header-comment entry describing its coverage, following this
  file's own existing per-test-file documentation convention.

## Verification performed

- Reconfigured with CMake (Ninja generator, reusing the existing `build/`
  directory) — no network access needed, everything was already fetched.
- Built `GreatTamanaEngineTests.exe` from a clean-of-these-changes
  incremental build — compiled with zero warnings/errors introduced by the
  new/changed files.
- Ran the **entire** existing test suite: **633 of 634 tests passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  a pre-existing machine-gated smoke test unrelated to this change —
  expected and documented in `TESTING.md`). **Zero regressions** — 9 new
  tests (`ComputeDispatchTests.*`) added on top of Phase 3's own 625
  passing + 1 skipped baseline.
- Built the full project (`GreatTamanaEngine.exe` **and**
  `GreatTamanaEngineTests.exe`) — both link successfully; shaders staged
  correctly next to the executable.
- Launched the real `GreatTamanaEngine.exe` and confirmed it stayed
  running (no crash/exception at startup, confirmed via `tasklist` a few
  seconds after launch) before stopping it — worth confirming directly
  since `Renderer.h`/`Renderer.cpp` themselves changed (a new method plus a
  new header include), even though `Renderer`'s own constructor/member
  layout did not — the same "worth confirming directly, not just via the
  test suite" discipline Phases 1-3 each established (see
  `COMPUTE_PHASE1_COMPLETION_REPORT.md`/`COMPUTE_PHASE2_COMPLETION_REPORT.md`/
  `COMPUTE_PHASE3_COMPLETION_REPORT.md`'s own "Verification performed"
  sections).
- No validation-layer run was possible on this development machine — as
  already noted in `COMPUTE_PHASE2_COMPLETION_REPORT.md`/
  `COMPUTE_PHASE3_COMPLETION_REPORT.md`, `VK_LAYER_KHRONOS_validation` is
  not installed here (a pre-existing environment limitation, not
  something this phase introduced or can control). This phase adds no new
  call site that actually issues a `vkCmdDispatch` yet (nothing in this
  codebase calls `Renderer::Dispatch()` today — that begins with Phase 6's
  `AddComputePass()`/Phase 7's real blur workload), so there is nothing
  live to validate beyond compiling/linking correctly, which the above
  confirms.

## Acceptance criteria check (against the strategy doc's own "Step 3: The Plan")

- ✅ `ComputeGroupCount()`/`ComputeGroupCount3D()` built in a new,
  Vulkan-header-free `src/Renderer/ComputeDispatch.h`, exactly as
  specified — correct ceiling-division arithmetic, with the
  non-exact-multiple case as the file's own most important regression
  test.
- ✅ Every GLSL shader's own local work-group size stays a per-shader,
  hand-maintained C++ constant living NEXT TO the pass that dispatches it
  (documented in `ComputeDispatch.h`'s own header comment and in
  `Renderer::Dispatch()`'s doc comment) — nothing centralized, nothing
  auto-derived via reflection, matching this campaign's own refusal.
- ✅ `Renderer::Dispatch()` built, binding pipeline/descriptor
  set/push constants/dispatch directly against the current render-graph
  pass's command buffer — the compute sibling of `Renderer::Submit()`,
  same shape.
- ✅ The v2 "Step 6" amendments were followed from day one rather than
  needing a later correction: no `PassContext::recordDispatch()` callback,
  no `PassGpuStats::drawStats` touching, and an explicit
  assert-then-no-op failure mode for a dispatch issued outside an active
  pass recording.
- ✅ `ComputeGroupCount()`'s tests were written first, before touching
  `Renderer::Dispatch()` at all, per the strategy document's own "Step 5:
  Their Role" instruction.
- ⏸ **Not yet possible to validate `Renderer::Dispatch()` against a real
  `vkCmdDispatch`/`Passthrough.comp`-style shader** — Phase 2's own
  throwaway validation shader was already built, exercised, and DELETED
  before Phase 2 was considered complete (see
  `COMPUTE_PHASE2_COMPLETION_REPORT.md`'s "Throwaway validation performed"
  section) specifically so it wouldn't linger as dead weight across
  phases. Re-validating `Dispatch()` end-to-end against a real compute
  shader is deferred to Phase 6/7's own real workloads (the strategy
  document's own Step 5 anticipates this: "Validate `Renderer::Dispatch()`
  against the same throwaway `Passthrough.comp` shader Phase 2
  introduced" — that shader no longer exists by design, so this
  validation will happen naturally the first time Phase 6/7 wires up a
  real dispatching pass instead).

## What was deliberately NOT done (per the strategy doc's own "Step 4")

- No `vkCmdDispatchIndirect` (indirect dispatch driven by GPU-computed
  counts) — every dispatch in this campaign has a CPU-known group count at
  record time. Noted as future work alongside indirect DRAW (already
  covered by the companion GPU-driven-rendering document), should a
  future workload need the GPU itself to decide how much compute work to
  issue.
- No automatic/adaptive work-group-size tuning based on queried hardware
  characteristics (subgroup size, etc.) — every compute shader's local
  size remains a fixed, hand-chosen constant.
- No generalized "compute uniform buffer" system beyond the existing
  128-byte push-constant convention already used for graphics —
  `Dispatch()`'s `pushConstantBytes`/`pushConstants` parameters are a
  plain, per-shader-documented convention (mirroring `ComputePipeline`'s
  own constructor), not a shared engine-wide struct.
- No `gte_add_shader()`/`CompileShaders.cmake` changes for a `DEFINES`
  mechanism to share a local-group-size constant verbatim between GLSL and
  C++ — the strategy document's own Step 3 explicitly named this a
  "stretch, more robust option... evaluate during implementation, not
  mandatory for a first landing," and this phase's own scope (pure
  dispatch math + the `Renderer::Dispatch()` call site) had no concrete
  shader yet to make that trade-off against. Left as a documented,
  deferred follow-up for whoever builds Phase 6/7's first real compute
  shader — the plain "restate the number as a comment-linked constant"
  approach is what those phases should use unless this stretch goal is
  revisited first.

## Handoff notes for whoever picks up Phase 5

- Phase 5 (`COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md`) is the next
  unit of work — extending `RenderGraphTypes.h`'s `ResourceAccess` enum
  with `ComputeShaderRead`/`ComputeShaderWrite` (shared, single-source-of-
  truth additions coordinated with the companion GPU-driven-rendering
  document's own Phase B), extending `RequiredStateFor()`'s texture-side
  handling for `VK_IMAGE_LAYOUT_GENERAL` storage-image barriers, and adding
  the buffer-only-write-can-be-silently-culled regression test that
  document's own Step 6 calls for.
- `Renderer::Dispatch()`'s signature is already final and stable for Phase
  6/7 to build against — it takes a plain `VkDescriptorSet` (mirroring how
  `Renderer::Submit()` already takes a plain `VkDescriptorSet
  materialDescriptorSet` for the graphics side), so a `ComputeDescriptorSet`
  built via Phase 3's own infrastructure slots in directly via its
  `Native()` accessor with no further `Dispatch()` changes needed.
- When Phase 6 builds `AddComputePass()`/`PassBuilder::WriteTexture()`,
  the actual compute-issuing code inside a pass's `execute` callback
  should look exactly like a graphics pass's `execute` callback already
  does today (see `RenderPasses.cpp`'s `AddGameViewPass()` for the
  precedent): `renderer.BeginGraphPassRecording(ctx.cmd, ...)` →
  (rewrite the pass's own `ComputeDescriptorSet` via
  `ctx.resolveBuffer()`/`ctx.resolveTexture()`, once those exist) →
  `renderer.Dispatch(...)` → `renderer.EndGraphPassRecording()`.
- When Phase 6/7 pick a local work-group size for a real shader (e.g.
  Phase 7's box-blur), declare it as a `constexpr std::uint32_t
  kBoxBlurLocalSizeX = ...;` living right next to that pass's own setup
  code, with a comment pointing at the matching `.comp` file's own
  `layout(local_size_x = ...)` line — never add a second, general-purpose
  "shared compute constants" file for this.
- Re-read this phase's own `Renderer::Dispatch()` doc comment (in
  `Renderer.h`) before wiring Phase 6's per-frame descriptor rewrite logic
  — it already states the exact "no legacy fallback, assert-then-no-op
  outside a pass recording" contract Phase 6's own compute-pass authors
  must respect.
