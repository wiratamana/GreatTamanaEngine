# PHASE2 — Execute Layer: Record Real MRT Vulkan Calls

Parent: `PHASE0_MASTER_STRATEGY.md` — **read that file first**, and read
`PHASE1_SETUP_LAYER_ORDERED_COLOR_ATTACHMENTS.md`'s own completion report
before starting (it must exist and record how `colorClearValue` was handled —
this phase depends on that decision).

**⚠️ HIGH BLAST RADIUS WARNING**: this phase edits
`RenderGraph::ExecuteCompiledGraph()`, the ONE function every single pass in
the entire engine — Mesh, TexturedMesh, sky background, scene grid, mesh
preview, every Atmosphere LUT pass, GPU skinning, Present, and
`ComputeBlurValidation` — flows through, every frame, in both the
synchronous and pipelined execution regimes. A subtle mistake here does not
just fail to add MRT support — it can silently break EVERY existing
single-attachment pass in the engine (wrong viewport, wrong render area,
double vkCmdBeginRendering, wrong clear behavior, etc.). Work carefully,
mirror the existing code's exact structure/comments/style, and re-read your
own diff against the "before" shape quoted in Step 2 line-by-line before
considering this phase done. Per `PHASE0`'s own Note on heavy phases, this
phase's diff is expected to receive an EXTRA, dedicated double-check pass
before the wider campaign double-check reviews everything else.

**Use `ask_questions` whenever you hit a genuine ambiguity or a design choice
this document (or `PHASE0_MASTER_STRATEGY.md`) doesn't already pin down.** If
you delegate any further sub-task, that delegation prompt must repeat this
same instruction.

## Step 1: The Goal (Where are we going?)

Make `RenderGraph::ExecuteCompiledGraph()`'s pass-recording loop build a real
`vkCmdBeginRendering` call with `colorAttachmentCount` set to however many
color attachments THIS pass actually declared (via `PHASE1`'s new
`pass.colorAttachments`), instead of only ever finding and using the single
LAST `ColorAttachmentWrite` usage — while every existing 0-attachment
(compute-only) and 1-attachment (every real pass today) case behaves
IDENTICALLY to before, bit-for-bit, with no observable behavior change.

## Step 2: The Situation (Where are we now?)

The exact current code this phase replaces
(`src/Renderer/RenderGraph/RenderGraph.cpp`, inside the
`for (const PassHandle& passHandle : compiled.executionOrder)` loop) —
**cross-checked directly against the real file as of this writing; both
quoted line ranges below match the real file exactly, not approximately**:

```cpp
// lines 332-354 - the attachment-scan (soon to be REPLACED)
bool hasColorWrite = false;
TextureHandle colorHandle;
bool hasDepthWrite = false;
TextureHandle depthHandle;
for (const ResourceUsage& usage : pass.writes) {
    if (usage.kind != ResourceKind::Texture) {
        continue;
    }
    if (IsColorAttachmentWriteAccess(usage.access)) {
        colorHandle = usage.texture;
        hasColorWrite = true;
    } else if (TargetsDepthState(usage.access)) {
        depthHandle = usage.texture;
        hasDepthWrite = true;
    }
}
```

```cpp
// lines 407-483 - the single-attachment vkCmdBeginRendering build (soon to be REPLACED)
bool didBeginRendering = false;
if (hasColorWrite) {
    const PhysicalTexture& colorTex = physicalTextures[colorHandle.index];

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorTex.target.imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (pass.colorClearValue.has_value()) {
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        const std::array<float, 4>& c = *pass.colorClearValue;
        colorAttachment.clearValue.color = { { c[0], c[1], c[2], c[3] } };
    } else {
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    }

    /* ... depth attachment build, unchanged by this phase ... */

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, colorTex.target.extent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = hasDepthAttachment ? &depthAttachment : nullptr;

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{};
    /* sized from colorTex.target.extent - unchanged in shape */
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    /* sized from colorTex.target.extent - unchanged in shape */
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    ctx.colorAttachmentExtent = colorTex.target.extent;
    didBeginRendering = true;
}
```

`hasDepthWrite`/`depthHandle`/the depth-attachment build
(`TargetsDepthState()`/`pass.depthClearValue`) are **completely unaffected by
this phase** — still exactly one optional depth attachment, found the exact
same way. Everything downstream of `didBeginRendering`
(`pass.execute(ctx)`/`vkCmdEndRendering`/GPU timing/draw-stats/debug-texture
registration loop, lines 485-542) is likewise unaffected — it already reads
generically from `physicalTextures`, keyed by handle, with no attachment-
count assumption. Note also (pre-existing, unaffected either way, just so
nobody is surprised by it while re-reading this function): the ENTIRE
`vkCmdBeginRendering` bracket — color AND depth — is gated on `hasColorWrite`
alone; a pass that writes ONLY a depth attachment (no color write at all)
gets no rendering bracket today, and still won't after this phase. That is a
pre-existing MVP limitation with no real consumer yet (no shadow-mapping pass
exists in this engine — see `PHASE0`'s Non-Goals), not something this phase
introduces or is expected to fix.

**A second load-bearing fact this phase's rewrite depends on, confirmed
directly against the real source (not assumed)**: every `TextureHandle` that
ends up in `pass.colorAttachments` is ALSO, unconditionally, pushed onto
`pass.writes` as a `ColorAttachmentWrite` usage by
`RenderGraphBuilder::PassBuilder::WriteColorAttachment()` (PHASE1) — and
`WriteColorAttachment()` is the ONLY call site anywhere in this codebase that
ever constructs a `ColorAttachmentWrite` usage (confirmed via
`search_in_dir` for `ColorAttachmentWrite` across all of `src/`). This means
the existing, UNCHANGED generic loop just above this phase's own code
(`for (const ResourceUsage& usage : pass.writes) { ApplyUsageBarrierIfNeeded(...); }`,
lines 307-309) already resolves (`EnsureTextureResolved()`) and barriers
every handle this phase's new per-attachment loop is about to index into
`physicalTextures` by — **by the time this phase's own code runs, every
`pass.colorAttachments[i].handle` is guaranteed already resolved.** This is
exactly why PHASE0 could correctly claim the barrier/dependency machinery
needs zero changes; this phase's own code (Step 3.2) adds one small,
defensive, debug-only assert confirming this invariant at the exact point it
matters, rather than trusting it silently — see below.

## Step 3: The Plan (How do we get there?)

### 3.1 — Replace the attachment-scan with a direct read of `pass.colorAttachments`

`pass.colorAttachments` (PHASE1's new field) is ALREADY the authoritative,
ordered list — there is no need to re-scan `pass.writes` at all for color
attachments anymore. Replace the scan block with:

```cpp
// Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE2 - replaces
// the old "scan pass.writes, keep only the LAST ColorAttachmentWrite" logic
// with a direct, ordered read of pass.colorAttachments (PHASE1) - attachment
// index in that vector is the shader layout(location = N) contract (see
// RenderGraphTypes.h's own ColorAttachmentDesc doc comment). Depth is
// UNCHANGED - still found via the exact same pass.writes scan as before
// (a pass has at most one depth/stencil attachment - out of scope for this
// campaign, see task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md).
bool hasDepthWrite = false;
TextureHandle depthHandle;
for (const ResourceUsage& usage : pass.writes) {
    if (usage.kind != ResourceKind::Texture) {
        continue;
    }
    if (TargetsDepthState(usage.access)) {
        depthHandle = usage.texture;
        hasDepthWrite = true;
    }
}
const bool hasColorWrite = !pass.colorAttachments.empty();
```

This equivalence (`hasColorWrite == !pass.colorAttachments.empty()` produces
the exact same true/false result the old scan produced for every pass that
exists today) rests entirely on the "load-bearing fact" called out at the
end of Step 2 above — re-confirm it holds against whatever PHASE1 actually
shipped (its completion report) before relying on it here.

(Keep `IsColorAttachmentWriteAccess()` imported/used elsewhere if anything
else in this file still needs it — search before removing any `#include`;
it is likely still needed nowhere else in this exact file, but do not delete
an include speculatively without confirming via `search_in_dir`. In practice
there is no standalone `#include` to remove either way — it comes in
transitively via `RenderGraphBarrierPlanner.h`, which this file keeps
including for `TargetsDepthState()`/`RequiredStateFor()`/etc. regardless.)

### 3.2 — Build N attachments instead of 1

Replace the single-`colorAttachment` build with a `std::vector` sized to
`pass.colorAttachments.size()`. Two decisions are **LOCKED by this document**
below — not left for the implementer to re-decide under this phase's own
"high blast radius" time pressure, since both were previously phrased as
open questions and this is exactly the kind of ambiguity a highest-risk
phase should not carry into implementation:

**Decision 1 — extract the "do all attachments share one extent" check into
a small, pure, Tier-1-testable free function.** This mirrors an existing,
explicit precedent already established in this exact subsystem:
`RenderGraphBarrierPlanner.h`'s own header comment splits its logic into "a
PURE decision half (Tier-1-testable ... none of which ever touch a live
VkDevice/VkCommandBuffer)" versus "a THIN Vulkan-call half", specifically
because `RenderGraph.cpp` needs a live `VkDevice` to exercise at all —
`TargetsDepthState()`/`IsColorAttachmentWriteAccess()` are the two existing
examples, each with its own dedicated test in
`RenderGraphBarrierPlannerTests.cpp`. The new extent-mismatch check is the
exact same shape of problem (a pure decision over plain data, buried inside
a function this repo's own Tier-1 tests structurally cannot reach) and
should get the same treatment instead of being left untestable inline.

Add, in `RenderGraphTypes.h`/`.cpp` (placed near `ColorAttachmentDesc`, since
this operates on that PHASE1 shape rather than on barrier-transition state —
`ask_questions` if a strong reason is found to prefer
`RenderGraphBarrierPlanner.h`/`.cpp` instead, alongside the other two pure
decisions):

```cpp
// Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE2 - PURE,
// Tier-1-testable (plain VkExtent2D comparison only - no VkDevice/VkImage
// involved) - given the resolved extents of a pass's declared color
// attachments, in pass.colorAttachments order, returns the 0-based index of
// the FIRST entry whose extent differs from entry 0, or std::nullopt if all
// N extents match (vacuously true for N <= 1 - the single-attachment/
// no-attachment case every existing pass in the engine hits today).
// Extracted as its own function, rather than left inline inside
// RenderGraph::ExecuteCompiledGraph(), specifically so a real Tier-1 test
// can verify this exact decision with plain data and no live VkDevice -
// mirroring TargetsDepthState()/IsColorAttachmentWriteAccess()'s own
// precedent (RenderGraphBarrierPlanner.h) for the identical reason.
std::optional<std::size_t> FindMismatchedColorAttachmentExtent(
    const std::vector<VkExtent2D>& extents) noexcept;
```

Add a Tier-1 test (in `RenderGraphTypesTests.cpp`, or a new small test file
if this repo's own convention prefers one-file-per-source-file for
`RenderGraphTypes.cpp` — check first) covering at least: zero extents, one
extent, N identical extents (expect `std::nullopt`), and N extents with a
single mismatch at a non-zero index (expect that exact index back).

**Decision 2 — the mismatch itself is a hard, unconditional error:
`throw std::runtime_error`, never a plain `assert()`.** A mismatched-extent
`VkRenderingInfo` is not a soft authoring warning — unlike, say, a
`RenderPassEvent` ordering contradiction, which this codebase's own
`RenderGraphCompiler::Compile()` deliberately treats as an `assert()` plus an
unconditional `fprintf(stderr)` diagnostic (because a wrong ordering hint is
often still schedulable safely) — a genuinely mismatched G-buffer attachment
extent is a real Vulkan-level contract violation: at best a validation-layer
failure, at worst GPU-driver-level undefined behavior or a silently corrupt
frame if it reaches `vkCmdBeginRendering` at all in a release (`NDEBUG`)
build, where a plain `assert()` compiles away to nothing. This matches this
codebase's own REAL, existing precedent for this class of problem — every
genuinely non-recoverable Vulkan/graph invariant violation already throws
unconditionally (`Pipeline.cpp`'s `vkCreateGraphicsPipelines` failure path;
`RenderGraphCompiler::Compile()`'s own dependency-cycle
`throw std::runtime_error`, which `RenderGraph::ExecuteCompiledGraph()`
already deliberately lets propagate UNCAUGHT — see this file's own comment
immediately above its `Compile()` call, which exists for exactly this
reason). Do not leave this choice open at implementation time.

```cpp
if (hasColorWrite) {
    // Multi-Render-Target (MRT) campaign, PHASE2 - one VkRenderingAttachmentInfo
    // PER declared color attachment, built in the EXACT order
    // pass.colorAttachments holds them (== shader layout(location = N)). A
    // plain std::vector, sized once per pass, is consistent with this
    // function's own existing allocation profile (e.g. `physicalTextures`/
    // `physicalBuffers` above) - no fixed-capacity/small_vector convention
    // exists elsewhere in this codebase to prefer instead (confirm via
    // search_in_dir before assuming otherwise).
    std::vector<VkRenderingAttachmentInfo> colorAttachmentInfos;
    colorAttachmentInfos.reserve(pass.colorAttachments.size());

    std::vector<VkExtent2D> resolvedExtents;
    resolvedExtents.reserve(pass.colorAttachments.size());

    for (const ColorAttachmentDesc& desc : pass.colorAttachments) {
        // This handle was ALREADY resolved above, by the exact same
        // generic `for (const ResourceUsage& usage : pass.writes)` barrier
        // loop every pass already goes through, completely unchanged by
        // this phase - WriteColorAttachment() (PHASE1) always pushes a
        // matching ColorAttachmentWrite usage onto pass.writes in lockstep
        // with pass.colorAttachments, specifically so this holds (see this
        // file's own Step 2 analysis). Asserted here defensively (cheap,
        // debug-only) so a future regression that ever breaks that lockstep
        // invariant fails LOUDLY, right here, instead of silently building
        // a VkRenderingAttachmentInfo around a VK_NULL_HANDLE imageView
        // that would otherwise only surface as a confusing validation-layer
        // error deep inside vkCmdBeginRendering.
        assert(desc.handle.index < physicalTextures.size() &&
            physicalTextures[desc.handle.index].resolved &&
            "RenderGraph::ExecuteCompiledGraph: a pass.colorAttachments entry was never "
            "resolved - WriteColorAttachment() must always also push a matching "
            "ColorAttachmentWrite onto pass.writes (see PHASE1)");
        const PhysicalTexture& colorTex = physicalTextures[desc.handle.index];
        resolvedExtents.push_back(colorTex.target.extent);

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = colorTex.target.imageView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (desc.clearColor.has_value()) {
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            const std::array<float, 4>& c = *desc.clearColor;
            colorAttachment.clearValue.color = { { c[0], c[1], c[2], c[3] } };
        } else {
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        colorAttachmentInfos.push_back(colorAttachment);
    }

    // LOCKED (see this section's own prose above): a real, unconditional
    // throw - never a plain assert() a release/NDEBUG build would silently
    // compile away. Built on the pure, Tier-1-tested decision function
    // above, so this exact check has real, VkDevice-free test coverage.
    if (const std::optional<std::size_t> mismatchIndex =
            FindMismatchedColorAttachmentExtent(resolvedExtents)) {
        const VkExtent2D& first = resolvedExtents[0];
        const VkExtent2D& bad = resolvedExtents[*mismatchIndex];
        throw std::runtime_error(
            "RenderGraph::ExecuteCompiledGraph: pass \"" +
            std::string(pass.name != nullptr ? pass.name : "<unnamed>") +
            "\" declared color attachments with mismatched extents - attachment 0 is " +
            std::to_string(first.width) + "x" + std::to_string(first.height) + ", attachment " +
            std::to_string(*mismatchIndex) + " is " + std::to_string(bad.width) + "x" +
            std::to_string(bad.height) +
            " - every color attachment on one pass must share the same extent (G-buffer-style "
            "targets are always rendered at the same resolution).");
    }
    const VkExtent2D firstExtent = resolvedExtents[0];

    /* ... depth attachment build stays EXACTLY as it is today, unchanged ... */

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, firstExtent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorAttachmentInfos.size());
    renderingInfo.pColorAttachments = colorAttachmentInfos.data();
    renderingInfo.pDepthAttachment = hasDepthAttachment ? &depthAttachment : nullptr;

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(firstExtent.width);
    viewport.height = static_cast<float>(firstExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = firstExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    ctx.colorAttachmentExtent = firstExtent;
    didBeginRendering = true;
}
```

(`throw`/`std::runtime_error`/`std::to_string`/`std::string` concatenation
need `<stdexcept>`/`<string>` — confirm `RenderGraph.cpp` compiles cleanly
with whatever it already transitively includes; add explicit includes if it
does not. `RenderGraphTypes.h` needs `<optional>`/`<cstddef>` for
`FindMismatchedColorAttachmentExtent()`'s own signature if not already
present.)

**Byte-for-byte equivalence checklist for the SINGLE-attachment case** (every
existing pass today) — confirm EACH of these explicitly while writing this
code, one by one; this is the single most important correctness property of
this phase:

- Exactly one loop iteration through `pass.colorAttachments` → exactly one
  `VkRenderingAttachmentInfo` pushed, `resolvedExtents.size() == 1`.
- `FindMismatchedColorAttachmentExtent()` always returns `std::nullopt` for a
  single-element input (nothing else to compare against) — the new `throw`
  path is provably unreachable for every pass that exists in the engine
  today; only a genuinely NEW 2+-attachment pass (none exist until PHASE4)
  can ever reach it.
- `renderingInfo.colorAttachmentCount == 1`, identical to today's hard-coded
  value.
- `firstExtent == colorTex.target.extent` (today's local variable), used
  identically for `renderArea`/`viewport`/`scissor`/`ctx.colorAttachmentExtent`.
- `colorAttachment.imageView`/`imageLayout`/`storeOp`/`loadOp`/`clearValue`
  are populated identically to today's single-attachment build (only the
  clear-color SOURCE changes, from `pass.colorClearValue` to
  `desc.clearColor` — see 3.3 below for why that is still the exact same
  value for every existing single-`WriteColorAttachment()` call site).
- The depth-attachment build and `renderingInfo.pDepthAttachment` are
  untouched, byte-for-byte, including its own gating on `hasColorWrite`.

### 3.3 — `pass.colorClearValue` disposition

Per `PHASE1`'s Step 3.1 decision (check that phase's own completion report):
if `colorClearValue` was removed there, there is nothing further to do here
beyond what Step 3.2 above already shows (reading `desc.clearColor` instead).
If `PHASE1` left `colorClearValue` in place deferring the cleanup to this
phase, remove every remaining read of it in `RenderGraph.cpp` now (the only
read is inside the code this phase already replaces) and consider removing
the now-fully-dead field from `RenderGraphTypes.h` in this same phase,
documenting the removal in `PHASE2_COMPLETION_REPORT.md`.

### 3.4 — `PassContext` needs no new fields

Per the how-to document's own item 7 (Stage 2): `ctx.resolveTexture()`
already resolves any handle by index, generically — a pass's own `execute`
callback already knows which handle maps to which shader output slot,
because it declared them in order via `WriteColorAttachment()` calls it
wrote itself. Confirmed directly by inspection of `PassContext`'s real
definition (`RenderGraph.h`, lines 89-164): every field
(`resolveReadTexture`/`resolveTexture`/`resolveBuffer`/`resolveVolumeTexture`/
`recordDraw`/`colorAttachmentExtent`) already operates per-handle or is a
single scalar this phase already populates correctly (`firstExtent`) — no
new field is needed. Do not re-derive this from scratch when implementing;
this has already been checked against the real header, not assumed.

### 3.5 — What this phase does NOT touch

- `Pipeline.h`/`.cpp`, `GpuResourceFactory` — untouched (PHASE3). No real
  pass can actually bind an N-target-compatible `Pipeline` yet after this
  phase alone — that is expected; this phase is pure plumbing, unreachable
  by any real pass declaration until PHASE3+PHASE4 land. A pass that
  declares 2+ `WriteColorAttachment()` calls TODAY (after this phase, before
  PHASE3/4) would correctly get 2 attachments in its `vkCmdBeginRendering`
  call, but would need a `Pipeline` built with a matching
  `colorAttachmentCount`/`pColorAttachmentFormats`/blend-attachment array to
  actually draw into it without a validation-layer error — which is exactly
  PHASE3's job.
- No new pass is declared anywhere yet (PHASE4).
- `RenderGraphBarrierPlanner.h`/`.cpp` — untouched; `TargetsDepthState()`/
  `IsColorAttachmentWriteAccess()` keep their exact existing meaning. The
  new `FindMismatchedColorAttachmentExtent()` pure helper (Step 3.2) is a
  NEW, separate function this phase adds to `RenderGraphTypes.h`/`.cpp` (or,
  if genuinely better justified at implementation time,
  `RenderGraphBarrierPlanner.h`/`.cpp`) — it does not modify either existing
  barrier-planner function.

### Definition of Done for this phase

- `RenderGraph::ExecuteCompiledGraph()` builds its color-attachment array
  from `pass.colorAttachments`, in order.
- A new, pure, Tier-1-testable `FindMismatchedColorAttachmentExtent()`
  function exists, with a Tier-1 test covering at least: 0/1 extents (no
  mismatch possible), N identical extents (no mismatch), and N extents with
  one real mismatch at a known index.
- A mismatched-extent pass throws `std::runtime_error` (never a plain,
  release-mode-silent `assert()`) with a message naming the pass and both
  conflicting extents/indices; a defensive debug-only `assert()` confirms
  every `pass.colorAttachments` handle was already resolved before it is
  indexed into `physicalTextures`.
- Every existing single-attachment pass in the engine renders identically
  (viewport/scissor/render area/clear behavior byte-for-byte unchanged) —
  verify this against the Byte-for-byte equivalence checklist in Step 3.2
  explicitly, AND via a live, running-engine check (`run_app_background` +
  `gte_send_request` against `/get_game_view` or `/get_swapchain`, compared
  visually against a known-good run before this phase, or against the
  Editor's own Game/Scene panels) in addition to compiling — this phase's
  correctness cannot be fully confirmed by compilation alone since nothing
  in the type system enforces "still renders the same pixels".
- A fast, targeted incremental build of `RenderGraph.cpp`/`RenderGraphTypes.cpp`
  and their direct dependents (including the new Tier-1 test file/target)
  succeeds.
- `PHASE2_COMPLETION_REPORT.md` written, explicitly confirming the
  single-attachment-case equivalence check from Step 3.2, recording the
  `colorClearValue` disposition (Step 3.3) if not already fully resolved in
  PHASE1, and noting where `FindMismatchedColorAttachmentExtent()` ended up
  living (`RenderGraphTypes.h`/`.cpp` vs `RenderGraphBarrierPlanner.h`/`.cpp`).
