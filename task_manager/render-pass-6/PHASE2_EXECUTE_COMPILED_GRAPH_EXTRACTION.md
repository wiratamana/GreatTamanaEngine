# PHASE2 — Extract `ExecuteCompiledGraph()`'s Six Concerns (item 2.6)

## Parent

`PHASE0_MASTER_STRATEGY.md` (read it first). Also read
`PHASE1_COMPLETION_REPORT.md` before starting — PHASE1 touched
`RenderGraph.cpp`'s `ExecuteCompiledGraph()` in one small, localized spot
(the overflow-diagnostic call right after `AssignOrGetSlot()`); this phase's
extraction must account for that addition already being present.

## Step 1: The Goal (Where are we going?)

`RenderGraph::ExecuteCompiledGraph()` is a single ~430-line function
interleaving six distinct concerns. Extract five of them into small, named,
private `RenderGraph` methods with **zero behavior change** — same Vulkan
calls, same order, same data, just organized into readable, independently
greppable units:

1. `BuildPassContext(...)` — the six `PassContext` lambda wire-ups.
2. `BuildColorAttachmentInfos(pass, physicalTextures) -> std::vector<VkRenderingAttachmentInfo>`.
3. `BuildDepthAttachmentInfo(pass, physicalTextures) -> std::optional<VkRenderingAttachmentInfo>`.
4. `RegisterDebugTextureSnapshots(...)`.
5. `RegisterDebugVolumeTextureSnapshots(...)`.

After this phase, `ExecuteCompiledGraph()` itself reads as a top-level
"resolve → barrier → begin → execute → end → record" skeleton, with each
extracted piece independently readable/greppable, and — per the source
document's own framing — each one now a real Tier-1 test opportunity (the
attachment-building pieces in particular need only plain `PassRecord`/
`PhysicalTexture` data, mirroring `FindMismatchedColorAttachmentExtent()`'s
own already-proven precedent in this exact file).

## Step 2: The Situation (Where are we now?)

`RenderGraph::ExecuteCompiledGraph()` (`src/Renderer/RenderGraph/
RenderGraph.cpp`, lines 242-670) interleaves, in one function body:

1. **Pipelined GPU-timing readback preamble** (lines 268-283) — already
   reasonably self-contained, reads back `m_pipelinedHasWritten`/
   `m_timestampPool`.
2. **Per-pass barrier application** (lines 306-311) — a tight loop calling
   the already-extracted `ApplyUsageBarrierIfNeeded()`.
3. **GPU timestamp bracketing** (lines 313-324, and 569-580) —
   `timingSlots.AssignOrGetSlot()` + `m_timestampPool.WriteBegin()`/
   `WriteEnd()`.
4. **MRT color/depth attachment construction + viewport/scissor +
   `vkCmdBeginRendering`/`EndRendering`** (lines 326-559) — the single
   largest, most deeply-nested block: builds `hasDepthWrite`/`depthHandle`/
   `hasColorWrite` (lines 340-358), constructs a `PassContext` with six
   inline lambdas (lines 360-414), then a big `if (hasColorWrite) { ... }`
   block (lines 416-559) building `colorAttachmentInfos`, checking
   `FindMismatchedColorAttachmentExtent()`, building the depth attachment,
   and issuing `vkCmdBeginRendering`/`vkCmdSetViewport`/`vkCmdSetScissor`.
5. **Draw-stat accumulation** (lines 406-414, 582-586) — `passDrawStats`
   plus the `recordDraw`/`recordIndirectDraw` lambdas and the final
   `UpdateDrawStatsFor()` call.
6. **Two debug-texture-registry registration loops** (lines 593-648) — one
   for `m_debugTextures`, one for `m_debugVolumeTextures`, both run once per
   `ExecuteCompiledGraph()` call, AFTER the whole per-pass loop — plus the
   final `BuildRenderGraphSnapshot()` call (lines 663-669, now also carrying
   PHASE1's new `timingSlotBudgetExhausted` argument).

`EnsureTextureResolved`/`EnsureBufferResolved`/`EnsureVolumeTextureResolved`/
`ApplyUsageBarrierIfNeeded` are ALREADY extracted as private methods
(`RenderGraph.h` lines 427-437) — this phase follows that exact same
existing style/precedent, it does not invent a new one.

`PassContext`'s six fields are STILL `std::function` at the start of this
phase (PHASE3 changes their internals, not this phase) — `BuildPassContext()`
extracted here still constructs the exact same six lambdas, just inside its
own named method instead of inline.

## Step 3: The Plan (How do we get there?)

### 3.1 — Declare the five new private methods in `RenderGraph.h`

Add, alongside the existing `EnsureTextureResolved`/`ApplyUsageBarrierIfNeeded`
declarations (`RenderGraph.h`, private section, around lines 427-451):

```cpp
// render-pass-6 campaign, PHASE2 (item 2.6) - extracted, zero-behavior-
// change decomposition of ExecuteCompiledGraph()'s own six interleaved
// concerns - see PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md. Builds the
// six resolver/record callbacks a pass's own `execute` callback uses -
// identical construction to what used to be written inline in
// ExecuteCompiledGraph()'s per-pass loop body.
PassContext BuildPassContext(VkCommandBuffer cmd, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers, std::vector<PhysicalVolumeTexture>& physicalVolumeTextures,
    DrawStats& passDrawStats);

// One VkRenderingAttachmentInfo per pass.colorAttachments entry, in that
// exact order (== shader layout(location = N) out) - identical logic to
// what used to be written inline inside ExecuteCompiledGraph()'s
// `if (hasColorWrite) { ... }` block's own color-attachment loop. Also
// fills `outResolvedExtents` with each attachment's resolved VkExtent2D (in
// the same order), for FindMismatchedColorAttachmentExtent()'s own
// existing pure decision function to consume - the caller (
// ExecuteCompiledGraph()) is still the one that calls
// FindMismatchedColorAttachmentExtent() and throws on mismatch, unchanged.
std::vector<VkRenderingAttachmentInfo> BuildColorAttachmentInfos(
    const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures,
    std::vector<VkExtent2D>& outResolvedExtents) const;

// The depth/stencil attachment for this pass, if it declared one - mirrors
// BuildColorAttachmentInfos() above, just for the single depth attachment a
// pass may have. `depthHandle` alone is sufficient to signal "no depth
// write this call": a default-constructed TextureHandle (what the caller's
// own `TextureHandle depthHandle;` local already is, unless the writes scan
// below finds a real DepthStencilAttachmentReadWrite usage) has
// `index == kInvalidIndex`, so `depthHandle.IsValid()` is exactly the
// `hasDepthWrite` signal this method needs - no separate bool parameter
// required. Returns std::nullopt whenever `!depthHandle.IsValid()` -
// identical logic/identical produced VkRenderingAttachmentInfo fields to
// what used to be written inline.
std::optional<VkRenderingAttachmentInfo> BuildDepthAttachmentInfo(
    const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures, TextureHandle depthHandle) const;

// The two passive-registration loops that used to run inline at the bottom
// of ExecuteCompiledGraph(), extracted verbatim (same fields, same skip
// conditions, same Upsert() calls) - see network-impl-4/network-impl-6's
// own original comments, preserved at the new call sites.
void RegisterDebugTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
    const std::vector<PhysicalTexture>& physicalTextures);
void RegisterDebugVolumeTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
    const std::vector<PhysicalVolumeTexture>& physicalVolumeTextures);
```

Confirm the exact parameter shapes above compile cleanly against what each
extracted block actually needs (e.g. `BuildPassContext()` may need
`isPipelined`/other locals depending on final signature needs found during
implementation) — treat the signatures above as a strong starting point, not
gospel; if a genuinely better shape emerges while implementing, use
`ask_questions` if it changes anything this document locked, otherwise just
proceed (small signature adjustments discovered mid-implementation are
normal engineering, not a "locked design decision" in the PHASE0 sense).

### 3.2 — Move the bodies, verbatim, into `RenderGraph.cpp`

For each of the five methods: copy the EXACT existing code (same Vulkan
struct field assignments, same order, same comments preserved where they
still apply) out of `ExecuteCompiledGraph()`'s body into the new method's own
body, and replace the original inline code with a call to the new method.

- `BuildPassContext()`: move lines 360-414 (the `PassContext ctx; ctx.cmd =
  cmd; ctx.resolveReadTexture = [...]; ...; ctx.recordIndirectDraw = [...];`
  block) verbatim. `ExecuteCompiledGraph()`'s per-pass loop becomes:
  ```cpp
  DrawStats passDrawStats;
  PassContext ctx = BuildPassContext(cmd, physicalTextures, physicalBuffers, physicalVolumeTextures, passDrawStats);
  ```
  (Note `passDrawStats` must be declared in `ExecuteCompiledGraph()`'s own
  scope, passed by reference, since `UpdateDrawStatsFor(pass.name,
  passDrawStats)` at the bottom of the loop still needs to read it after
  `pass.execute(ctx)` returns.)
- `BuildColorAttachmentInfos()`/`BuildDepthAttachmentInfo()`: move the
  color-attachment-info-building loop (lines 427-481) and the depth-
  attachment-building block (lines 505-522) respectively. The caller
  (`ExecuteCompiledGraph()`) keeps the `FindMismatchedColorAttachmentExtent()`
  check, the `vkCmdBeginRendering`/`vkCmdSetViewport`/`vkCmdSetScissor` calls,
  and the `ctx.colorAttachmentExtent = firstExtent;` assignment — those stay
  in `ExecuteCompiledGraph()` itself (they are the "assemble + issue Vulkan
  calls" half, not "build the data" half) unless, while implementing, moving
  the whole `if (hasColorWrite) {...}` block into one more method turns out
  cleaner — if so, use `ask_questions` before doing anything not already
  named in this document's own five-method list (PHASE0/this document's
  five names are the agreed scope; a sixth extracted method is a scope
  change that should be confirmed, not silently added).
- `RegisterDebugTextureSnapshots()`/`RegisterDebugVolumeTextureSnapshots()`:
  move lines 593-618 and lines 620-648 respectively, verbatim including
  their existing comments. `ExecuteCompiledGraph()`'s tail becomes:
  ```cpp
  RegisterDebugTextureSnapshots(timingMode, input, physicalTextures);
  RegisterDebugVolumeTextureSnapshots(timingMode, input, physicalVolumeTextures);
  ```

### 3.3 — Preserve every comment that still applies

Every extracted block carries multi-paragraph doc comments explaining WHY
the code does what it does (e.g. the `resolveReadTexture`/`resolveTexture`
alias comment, the `FindMismatchedColorAttachmentExtent()` "LOCKED" comment,
the network-impl-4/network-impl-6 registration-loop comments). Move these
comments along with their code into the new method's own doc comment or
inline body — do not delete institutional knowledge during a pure
extraction. Add one new, short comment at each new method's declaration
site (`RenderGraph.h`) crediting this phase: "render-pass-6 campaign,
PHASE2 (item 2.6) - extracted out of ExecuteCompiledGraph() for readability,
zero behavior change - see PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md."

### 3.4 — Tier-1 test opportunity (encouraged, matching the source
document's own framing — "each extraction opens a Tier-1 test opportunity")

`BuildColorAttachmentInfos()`/`BuildDepthAttachmentInfo()` operate on plain
`PassRecord`/`PhysicalTexture` data with no live `VkDevice` needed for the
DECISION half (which attachments, what load/store ops, what clear values) —
though the actual `VkRenderingAttachmentInfo`/`VkImageView` values they
produce are Vulkan-handle-shaped. If, while implementing, a genuinely pure
sub-piece of either method's logic can be split out and Tier-1-tested
(mirroring `FindMismatchedColorAttachmentExtent()`'s own precedent exactly),
do so — but this is NOT a hard requirement for this phase (unlike PHASE1's
mandatory test additions, since `RenderGraph`/`PhysicalTexture` are Tier-2,
GPU-adjacent types per `AGENTS.md`'s own accepted "Tier 2 has no automated
test coverage yet, and that's fine" rule). Do not force a test that requires
a live `VkDevice` just to hit a nominal "add a test" checkbox.

### 3.5 — What NOT to do in this phase

- Do NOT change `PassContext`'s field types (still `std::function` at the
  end of this phase) — that is PHASE3's job.
- Do NOT change the ORDER any Vulkan call happens in, the barrier logic, or
  any data value computed — this is a pure code-motion refactor. If a
  diff shows anything beyond "code moved to a new method + a call site
  replacing it", stop and re-check against the original.
- Do NOT touch `RenderGraphCompiler.cpp`/`RenderGraphBuilder.h` — those are
  PHASE4/PHASE5's job.

## Definition of Done for this phase

- `RenderGraph::ExecuteCompiledGraph()` is visibly shorter, reads as a
  linear "resolve → barrier → begin → execute → end → record" skeleton, and
  the five extracted methods each carry their original code + comments
  verbatim.
- A close read (or a diff against the pre-PHASE2 file) confirms **zero
  behavior change**: identical Vulkan calls, identical order, identical
  data.
- A fast, targeted incremental compile check passes.
- A live sanity check (via `run_app_background` + `gte_send_request` against
  `GET /get_game_view`/`GET /get_swapchain`) shows rendering is visually
  unchanged — encouraged, not a hard gate per PHASE0's "only PHASE7 does
  full build/ctest" rule, but a cheap, high-value check given this phase
  touches the engine's hottest per-frame path.
- `PHASE2_COMPLETION_REPORT.md` is written, listing the five methods' final
  signatures (if they differ from Step 3.1's draft) and any deviation from
  this plan.
- Changes are committed via `git_add`/`git_commit`.

**Remember**: use `ask_questions` for any genuine ambiguity — in particular,
if implementation reveals that a sixth extracted method (or a different
grouping than the five named above) is genuinely cleaner, confirm before
deviating from this document's agreed scope.
