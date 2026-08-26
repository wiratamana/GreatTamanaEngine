# RENDERGRAPH_PHASE5_COMPLETION_REPORT.md

Session report for **Phase 5 — Automatic GPU Safety Rules**, the fifth
implementation chunk of the Render Graph campaign described in
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md` — nothing beyond that
document's own "Step 3: The Plan" was implemented, per its own "Step 4:
What We Will NOT Do".

## What shipped

Two new, additively-compiled files plus one new test file — nothing else in
the engine was touched, and nothing outside `src/Renderer/RenderGraph/`
includes any of it yet:

- **`src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h`/`.cpp`** — the
  small, data-driven barrier planner that will eventually replace
  `FrameRecorder::RecordFrame()`'s hand-written, fixed-shape barrier code:
  - **`ResourceState`** — a plain, comparable POD (`layout`/`stageMask`/
    `accessMask`, `operator== = default`) describing a resource's known
    GPU-visible state at one point in time. `layout` is documented as
    meaningless for a buffer transition (buffers have no image layout
    concept) — callers building a buffer barrier simply leave it at its
    default, matching the strategy document's own "buffers use a dummy/
    ignored value here" note.
  - **`RequiredStateFor(ResourceAccess access, bool isDepthResource)`** —
    the pure decision half: an exhaustive `switch` over every
    `ResourceAccess` enumerator (Phase 1), deliberately with **no
    `default:` case** (mirroring `IsWriteAccess()`/`ToString()`'s own rule
    in `RenderGraphTypes.h/.cpp` — a future enumerator added without
    updating this function fails to compile here until it is), producing
    the exact `{layout, stageMask, accessMask}` triple each access kind
    requires. `isDepthResource` is used as a debug-build-only sanity
    assertion (a `ColorAttachmentWrite` request against a depth resource,
    or a `DepthStencilAttachmentReadWrite` request against a non-depth
    one, is always a caller mistake, since these two access kinds are
    already mutually exclusive by name) rather than changing either
    branch's own resulting state — both asserts compile out entirely in a
    release (`NDEBUG`) build.
  - **`RequiresBarrier(previous, next)`** — pure value-equality check: `false`
    for two identical states (the "back-to-back reads with an identical
    layout/stage/access need no barrier between them" optimization the
    strategy document explicitly calls out), `true` for any difference in
    layout, stage mask, or access mask alone.
  - **`BuildImageMemoryBarrier2()`/`BuildBufferMemoryBarrier2()`** — a
    small, deliberate addition beyond the strategy document's own literal
    Step 3.1 sketch (which only names `EmitImageBarrier()`/
    `EmitBufferBarrier()` as the "thin Vulkan-call half"): these two
    functions are the PURE half of populating a `VkImageMemoryBarrier2`/
    `VkBufferMemoryBarrier2` from two already-decided `ResourceState`s —
    filling in a plain POD struct's fields requires no live `VkDevice`/
    `VkCommandBuffer` at all, so splitting "decide the field values" (pure,
    Tier-1-testable) from "actually issue the call"
    (`vkCmdPipelineBarrier2`, Tier 2) let this phase's most important
    regression tests (see below) assert on exact barrier field values with
    zero live device involved, exactly matching the strategy document's own
    "Crucially, split this into a PURE decision half... and a THIN
    Vulkan-call half" instruction — this is a direct, faithful
    interpretation of that instruction, not a scope expansion.
  - **`EmitImageBarrier()`/`EmitBufferBarrier()`** — the genuinely thin
    Vulkan-call half: build via the two functions above, wrap in a
    `VkDependencyInfo`, call `vkCmdPipelineBarrier2` once. Contain no
    decision logic of their own, mirroring `GpuTimingService`'s own
    "thin" recording methods.
  - **No per-resource state TRACKING across a whole compiled pass list** —
    that is explicitly Phase 6's job (the execution engine, walking
    `CompiledGraph::executionOrder` and maintaining one "current state" per
    resource); this file only provides the two pure per-transition
    decisions plus the thin Vulkan-call wrappers Phase 6 will drive in a
    loop. Called out explicitly in this file's own header comment so
    nobody mistakes this phase for having implemented that tracking loop.
  - **MRT (multi-color-attachment) support is out of scope, per the
    campaign's own V2 Revision Note 1/3** — this file has no
    attachment-COUNT concept at all (it operates purely per-resource), so
    it is naturally unaffected either way; the "single color attachment"
    constraint lives entirely in Phase 6's future `PassContext`/
    `RenderTarget` resolution, not here.
- **`tests/Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp`** — 18
  new Tier-1 tests (16 ordinary + 2 death tests), entirely hand-constructed,
  zero live device, following the strategy document's own Step 3.4 coverage
  list:
  - `RequiredStateFor()` — one test per `ResourceAccess` enumerator (5
    tests), including a direct regression check for
    `DepthStencilAttachmentReadWrite` against `FrameRecorder.cpp`'s own
    hardcoded `toDepthAttachment` barrier fields.
  - `RequiresBarrier()` — identical states need no barrier; states
    differing in layout alone, stage mask alone, or access mask alone all
    need one (4 tests).
  - **The three-pass hand-simulated ping-pong sequence** (A writes color ->
    B reads as shader-read -> C writes color again), starting from a
    resource already in the `ColorAttachmentWrite` state (mirroring an
    imported resource seeded from a previous frame's leftover state, per
    Phase 2's `ImportTexture()`) — confirms exactly 2 barriers are emitted
    (not 3, not 1: entering A needs none since the resource is already in
    the right state; the A→B and B→C transitions each need one), with a
    field-level check on both emitted barriers' access masks.
  - `BuildImageMemoryBarrier2()`/`BuildBufferMemoryBarrier2()` — plain
    field-population coverage (2 tests), confirming every field (including
    `srcQueueFamilyIndex`/`dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED`,
    per this phase's "no queue-family-ownership-transfer support" scope
    fence) is populated correctly from the given image/buffer handle and
    the two `ResourceState`s.
  - **The regression suite** (4 tests) — the single most important tests in
    this phase, per the strategy document's own "Step 5: Their Role":
    field-for-field matches against `FrameRecorder.cpp`'s own hardcoded
    `toColorAttachment`/`toDepthAttachment` barriers, plus the two "full
    resource journey" shapes (`UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL ->
    PRESENT_SRC_KHR` for `Present()`, `... -> SHADER_READ_ONLY_OPTIMAL` for
    `RenderOffscreen()`) — every `srcStageMask`/`dstStageMask`/
    `srcAccessMask`/`dstAccessMask`/`oldLayout`/`newLayout` value checked
    directly against what `FrameRecorder.cpp` hardcodes today. See
    "A design decision worth flagging" below for how the `PRESENT_SRC_KHR`
    final state — which has no corresponding `ResourceAccess` enumerator —
    was represented in this test.
  - `isDepthResource` assertion guard (2 `NDEBUG`-guarded death tests,
    mirroring `RenderGraphBuilderTests.cpp`'s own established pattern) —
    `ColorAttachmentWrite` rejects `isDepthResource == true`;
    `DepthStencilAttachmentReadWrite` rejects `isDepthResource == false`.

## Build system changes

- Root `CMakeLists.txt`: added
  `src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h`/`.cpp` to
  `gte_core`'s source list, right after the existing
  `RenderGraphResourcePool.h`/`.cpp` entry.
- `tests/CMakeLists.txt`: added
  `Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp` to
  `GTE_TEST_SOURCES` (right after `RenderGraphCompilerTests.cpp`), plus a
  matching entry in the file's own Tier-1 taxonomy comment block.

## Verification performed

- Reconfigured with CMake (reusing the existing `build/` Ninja
  configuration) — no network access needed, everything was already
  fetched.
- Built `GreatTamanaEngineTests` from the existing incremental build —
  compiled with zero warnings/errors introduced by the new files.
- Ran the **new** Render Graph tests in isolation
  (`--gtest_filter=*RenderGraph*`) — all **83** pass (the 65 pre-existing
  Phase 1/2/3 tests, unchanged, plus the 18 new Phase 5 tests described
  above — Phase 4's `RenderGraphResourcePool` has no automated tests of its
  own, per its own completion report).
- Ran the **entire** test suite — **604 tests total**, **603 passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  the same pre-existing machine-gated smoke test noted in every prior
  phase's report, unrelated to this change). **Zero regressions.**
- Built the real `GreatTamanaEngine.exe` target too — succeeded cleanly,
  confirming the new files don't break the shipping executable's build.

## A design decision worth flagging for whoever reads this next

**The `PRESENT_SRC_KHR` "about to be presented" final state has no
corresponding `ResourceAccess` enumerator, and is deliberately NOT one.**
`FrameRecorder.cpp`'s own `Present()` path transitions a resource's final
barrier to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` — but "about to be handed to
the presentation engine" is not something any pass *reads* or *writes*
through the render graph's own vocabulary (`ResourceAccess`, Phase 1); it's
a special, fixed hand-off state that only applies to the ONE imported
resource that happens to be the actual swapchain image, decided by Phase
6's future execution harness (which alone knows whether a given imported
`TextureHandle` is the swapchain or an ordinary `RenderTexture`), not by
anything a pass author declares. Rather than speculatively adding a sixth
`ResourceAccess` enumerator for this one case (which Phase 1 already
closed out as a deliberately fixed, MVP-scoped list), the regression test
for the Present path hand-builds this fixed
`ResourceState{PRESENT_SRC_KHR, BOTTOM_OF_PIPE, NONE}` triple directly,
exactly as Phase 6 will need to for the real swapchain resource. This is
documented directly in the test file's own comments; Phase 6 should
introduce this same fixed constant (or an equivalent well-known helper) at
the point it actually needs to hand a resource back to
`vkQueuePresentKHR`, rather than reaching for a Phase-1-enumerator change
this phase deliberately avoided.

## Acceptance criteria check (against the strategy document's own Step 3.4)

- ✅ `RequiredStateFor(ColorAttachmentWrite, false)` produces
  `{COLOR_ATTACHMENT_OPTIMAL, COLOR_ATTACHMENT_OUTPUT, COLOR_ATTACHMENT_WRITE}`.
- ✅ `RequiredStateFor(DepthStencilAttachmentReadWrite, true)` produces the
  exact depth/early+late-fragment-test triple matching
  `FrameRecorder.cpp`'s existing `toDepthAttachment` barrier fields.
- ✅ `RequiredStateFor(ShaderRead, false)` produces
  `{SHADER_READ_ONLY_OPTIMAL, FRAGMENT_SHADER, SHADER_READ}`.
- ✅ `RequiresBarrier()` returns `false` for two identical states and `true`
  for any single-field difference (layout, stage, or access mask alone).
- ✅ A hand-simulated three-pass sequence confirms the exact expected
  barrier count (2, not 3, not 1) with correct `srcAccessMask`/
  `dstAccessMask` values on each emitted barrier.
- ✅ A resource whose declared access sequence exactly matches
  `FrameRecorder.cpp`'s existing `Present()`/`RenderOffscreen()` shapes
  produces field-for-field identical `srcStageMask`/`dstStageMask`/
  `srcAccessMask`/`dstAccessMask`/`oldLayout`/`newLayout` values — the
  single most important regression test in this phase.
- ⚠️ The debug-build format-matching ASSERTION (`FrameRecorder::RecordFrame()`'s
  `target.format == expectedFormat`/`target.depthFormat ==
  expectedDepthFormat`) is explicitly **not** re-implemented or tested
  here — it belongs to Phase 6's future execution harness, the first place
  that will actually resolve a pass's attachment(s) against a live
  `RenderTarget`/`Pipeline`. This file operates purely on already-decided
  `ResourceState` values and never touches a `RenderTarget`/`Pipeline`
  format at all, so there is nothing to assert here. This is documented as
  an explicit, deliberate manual-verification note (per the strategy
  document's own "or an explicit, documented manual-verification note if
  it genuinely can't be Tier-1" allowance) in this test file's own header
  comment — **whoever implements Phase 6 must re-introduce this exact
  assertion discipline** (see `AGENTS.md`, "Render Target Format Matching")
  at the point its execution harness resolves a pass's `RenderTarget`,
  rather than assuming it was silently preserved by this phase.

## What was deliberately NOT done (per the strategy document's own Step 4)

- No cross-resource barrier BATCHING (multiple resources' barriers combined
  into one `vkCmdPipelineBarrier2` call via a combined
  `pImageMemoryBarriers` array spanning several resources) — one call per
  resource-transition, matching `FrameRecorder.cpp`'s own existing
  per-pass (not per-resource) batching granularity. Deferred to Phase 9.
- No queue-family-ownership-transfer barriers —
  `srcQueueFamilyIndex`/`dstQueueFamilyIndex` are always
  `VK_QUEUE_FAMILY_IGNORED`, verified directly by
  `BuildImageMemoryBarrier2PopulatesEveryFieldFromTheGivenStatesAndImage`/
  `BuildBufferMemoryBarrier2PopulatesEveryFieldFromTheGivenStatesAndBuffer`.
  Multi-queue support is Phase 9 backlog.
- No buffer-barrier access kinds beyond what Phase 1's `ResourceAccess`
  already defines (no storage-buffer read-write) — a compute pass's
  storage-buffer access pattern is Phase 9 scope.
- No per-resource state TRACKING loop (walking a compiled pass list,
  maintaining one "current state" per resource, deciding when to actually
  call `EmitImageBarrier()`/`EmitBufferBarrier()`) — that is Phase 6's job,
  not this file's. This file only supplies the two pure per-transition
  decisions (`RequiredStateFor()`/`RequiresBarrier()`) Phase 6 will call in
  its own loop.
- No `RenderTarget`/`Pipeline` involvement anywhere — this file is pure
  `ResourceState`/`ResourceAccess` decision logic plus thin
  `VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2` construction, exactly as
  scoped.
- No MRT (multi-color-attachment) support — see "What shipped" above; this
  file has no attachment-count concept at all, by construction.
- No `ResourceAccess` enumerator was added for the swapchain
  "about-to-present" state — see "A design decision worth flagging" above.

## Handoff notes for whoever picks up Phase 6

- Phase 6 (`RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md`) is the next
  unit of work in this campaign — tying Phases 1-5 together into real
  Vulkan recording (`RenderGraph::Compile()`/`Execute()`), with per-pass
  GPU timing/`DrawStats`. Per `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s
  own V2 Revision Note 2, `RenderGraph::Execute()` is called **twice** per
  frame (once for the synchronous offscreen regime covering Game+Scene
  together, once for the async/pipelined swapchain regime covering Present
  alone) — not once, and not three times.
- Phase 6 owns the per-resource state-tracking loop this phase's own header
  comment explicitly does NOT implement: a
  `std::vector<ResourceState> m_currentTextureStates` (parallel to Phase
  3's `textureLifetimes`), seeded at the start of a frame from either an
  imported resource's caller-supplied `TextureImportInfo::currentLayout`
  (transient resources start at `{VK_IMAGE_LAYOUT_UNDEFINED, TOP_OF_PIPE,
  NONE}` — a transient resource's very first use this frame is necessarily
  also its first-ever use since being claimed from Phase 4's pool). As
  `CompiledGraph::executionOrder` is walked pass by pass, every declared
  read/write calls `RequiredStateFor()` for its target state, calls
  `RequiresBarrier()` against the tracked current state, calls
  `EmitImageBarrier()`/`EmitBufferBarrier()` only if a barrier is actually
  needed, then overwrites the tracked state.
- Right after `vkCmdBeginRendering` for a given pass, Phase 6's execution
  harness must call `vkCmdSetViewport`/`vkCmdSetScissor` sized to that
  pass's own resolved color attachment's extent — mirroring
  `FrameRecorder::RecordFrame()`'s own existing behavior — BEFORE invoking
  that pass's `execute` callback, per this campaign's own V2 Revision
  Note 2. This phase's own barrier-planner code doesn't implement this (it
  belongs to Phase 6's execution loop) but is called out here as a direct
  consequence of retiring `FrameRecorder::RecordFrame()`'s own combined
  begin-rendering-and-viewport-setup block.
- See "A design decision worth flagging" above before assuming a
  `ResourceAccess::PresentSrc`-shaped enumerator exists anywhere — it does
  not, and Phase 6 should hand-build the fixed
  `ResourceState{PRESENT_SRC_KHR, BOTTOM_OF_PIPE, NONE}` triple itself for
  the one real swapchain resource, exactly as this phase's own regression
  test already does.
- See the acceptance-criteria section above (the ⚠️ item) before assuming
  `FrameRecorder::RecordFrame()`'s debug-build format-matching assertion
  was carried forward anywhere in this phase — it was not, and Phase 6 must
  re-introduce it at the point its execution harness resolves a pass's
  `RenderTarget`/`Pipeline`.
- Do not add cross-resource barrier batching, queue-family-transfer
  support, or MRT-shaped fields to this file without first re-reading this
  phase's own "What We Will NOT Do" section and
  `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision Notes 1/3 —
  all three are explicitly Phase 9 backlog.
