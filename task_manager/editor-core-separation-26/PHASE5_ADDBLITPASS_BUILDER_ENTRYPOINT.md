# PHASE5 — `RenderGraphBuilder::AddBlitPass()`

Read `PHASE0_MASTER_STRATEGY.md` in full first. Read the source document
Part B.2's `AddBlitPass()` bullet and its exact signature code block (lines
208-244). Read `PHASE4_COMPLETION_REPORT.md` first — confirm the companion
audit landed and `FrameDebuggerData.cpp` is safe for a real `PassKind::Blit`
pass now.

**This phase makes `PassKind::Blit` PRODUCIBLE for the first time in this
campaign.** It is still pure data, though — no Vulkan call is added here
(that is PHASE6). Every test in this phase is Tier-1/GPU-free.

## Step 1: The Goal

`RenderGraphBuilder::AddBlitPass(name, spec, renderPassEvent, viewScope,
category, tags)` exists as a real, official, first-class pass-declaration
entry point. Calling it: declares `ReadTexture(spec.src,
ResourceAccess::TransferSrc, spec.srcIsDepth)` and
`WriteTexture(spec.dst, ResourceAccess::TransferDst, spec.dstIsDepth)`
internally (using PHASE2's new parameter); stores `spec` on
`PassRecord::blitCommand`; stamps `pass.kind = PassKind::Blit`; and forwards
`drawKind = RenderPassDrawKind::Blit` (ALWAYS this fixed value — not a
caller-supplied parameter) to the installed debug-metadata sink, exactly
like `AddRenderPass()` already does for its own `category`/`drawKind`/`tags`
parameters. No `setup`/`execute` callback is needed or accepted — the whole
content of this pass IS the blit (PHASE6 supplies its actual Vulkan-call
body directly inside `RenderGraph::ExecuteCompiledGraph()`, never via a
pass-author-supplied callback).

## Step 2: The Situation

**IMPORTANT CROSS-CAMPAIGN CORRECTION — read this before writing any code:**
the source document's own Part B.2 code sample says this method "stamps
BOTH `kind = PassKind::Blit` AND `drawKind = RenderPassDrawKind::Blit`" as
if `PassRecord::drawKind` is a directly-assignable field. **It is not, as of
`editor-core-separation-25` (BIG STEP 1, already shipped on this branch)** —
`category`/`drawKind`/`tags` were REMOVED from `PassRecord` entirely and
replaced with a sink-forwarding mechanism (`RenderGraphDebugMetadataSink.h`).
`PassRecord::kind` (still a real, direct field) and `drawKind` (now
sink-only) must be handled DIFFERENTLY — re-read
`RenderGraphBuilder::AddRenderPass()`'s CURRENT real implementation (see
below) as your actual template, not the source document's pre-BIG-STEP-1
code sample.

Confirmed by direct read (re-confirm current line numbers before editing):

- `src/Renderer/RenderGraph/RenderGraphBuilder.h`, lines 525-545 —
  `AddRenderPass()`'s real, current body:
  ```cpp
  template <typename SetupFn, typename ExecuteFn>
  void AddRenderPass(const char* name, PassKind kind, ViewScope viewScope, RenderPassCategory category,
      SetupFn&& setup, ExecuteFn&& execute, RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh,
      RenderPassEvent renderPassEvent = RenderPassEvent::Opaques, RenderPassTagMask tags = 0)
  {
      if (kind == PassKind::Compute) {
          AddComputePass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
      } else {
          AddPass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
      }
      m_passes.back().renderPassEvent = renderPassEvent;
      if (m_debugMetadataSink != nullptr) {
          m_debugMetadataSink->OnPassDeclared(m_passes.size() - 1, category, drawKind, tags);
      }
  }
  ```
  `AddBlitPass()` is NOT built on top of `AddPass()`/`AddComputePass()`
  (neither accepts a `setup`/`execute` pair shaped like a blit needs — a
  blit has no callback at all), so it must construct its own `PassRecord`
  directly, mirroring `AddPass()`'s OWN internal shape instead (`RenderGraphBuilder.h`
  lines 408-422): `m_passes.push_back(PassRecord{}); PassRecord& pass =
  m_passes.back(); pass.name = name;` — then use a local
  `PassBuilder passBuilder(pass);` to call `ReadTexture()`/`WriteTexture()`
  on it (the exact same `PassBuilder` type `AddPass()`'s own `setup`
  callback receives — nothing new to construct).
- `RenderGraphBuilder::PassBuilder::ReadTexture()`/`WriteTexture()` — both
  are already real, non-template, plain member functions (declared
  `RenderGraphBuilder.h` lines 213/271, defined `RenderGraphBuilder.cpp`
  lines 6-9/56-59 as of PHASE2) — directly callable from inside
  `AddBlitPass()`'s own body via a local `PassBuilder`.
- `PassRecord::blitCommand` (`RenderGraphTypes.h`, added by PHASE3) —
  `std::optional<BlitSpec>` — set via `pass.blitCommand = spec;`.
- `IPassDebugMetadataSink::OnPassDeclared(declarationIndexThisFrame,
  category, drawKind, tags)` — the exact signature `AddRenderPass()` already
  calls; `AddBlitPass()` calls it identically, with `drawKind` HARDCODED to
  `RenderPassDrawKind::Blit` (never a parameter of `AddBlitPass()` itself —
  the source document's own signature deliberately has no `drawKind`
  parameter at all, since a blit pass's draw-kind is always, definitionally,
  `Blit`).
- Exact signature to add (`RenderGraphBuilder.h`, public section, placed
  immediately after `AddRenderPass()`'s own two overloads — mirrors
  `KeepVolumeTextureOutput()`'s own placement logic of "immediately after
  the thing it is conceptually a sibling of"):
  ```cpp
  void AddBlitPass(const char* name, const BlitSpec& spec,
      RenderPassEvent renderPassEvent = RenderPassEvent::Opaques,
      ViewScope viewScope = ViewScope::Shared,
      RenderPassCategory category = RenderPassCategory::General,
      RenderPassTagMask tags = 0);
  ```
  Copy the source document's own doc comment on WHY `renderPassEvent` has no
  default-free special treatment here (lines 230-238 of the source
  document — "a blit with no real in-frame reader... has no data dependency
  to order it by, so it MUST have an explicit way to be placed at a real
  RenderPassEvent tier") verbatim into this declaration's own comment.
  This is a NON-TEMPLATE method (unlike `AddPass()`/`AddComputePass()`/
  `AddRenderPass()`, which are templates because they accept `setup`/
  `execute` callbacks) — its body belongs in `RenderGraphBuilder.cpp`, NOT
  inline in the header.
- `RenderGraphBuilder.cpp` — add the implementation (this is the ONE new
  function in this whole campaign that constructs a `PassRecord` directly
  rather than going through `AddPass()`/`AddComputePass()`):
  ```cpp
  void RenderGraphBuilder::AddBlitPass(const char* name, const BlitSpec& spec,
      RenderPassEvent renderPassEvent, ViewScope viewScope, RenderPassCategory category, RenderPassTagMask tags)
  {
      assert(name != nullptr && name[0] != '\0' &&
          "RenderGraphBuilder::AddBlitPass requires a non-empty, static-storage-duration pass name");

      m_passes.push_back(PassRecord{});
      PassRecord& pass = m_passes.back();
      pass.name = name;
      pass.kind = PassKind::Blit;
      pass.viewScope = viewScope;
      pass.renderPassEvent = renderPassEvent;
      pass.blitCommand = spec;

      PassBuilder passBuilder(pass);
      passBuilder.ReadTexture(spec.src, ResourceAccess::TransferSrc, spec.srcIsDepth);
      passBuilder.WriteTexture(spec.dst, ResourceAccess::TransferDst, spec.dstIsDepth);

      if (m_debugMetadataSink != nullptr) {
          m_debugMetadataSink->OnPassDeclared(m_passes.size() - 1, category, RenderPassDrawKind::Blit, tags);
      }
  }
  ```
  Double-check `PassBuilder`'s constructor is accessible here (it is public
  — `explicit PassBuilder(PassRecord& pass) noexcept`, `RenderGraphBuilder.h`
  line 208 — and `AddBlitPass()` is itself a member of `RenderGraphBuilder`,
  same access as `AddPass()`'s own template body already has).
- Test precedent: `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`
  lines 916-1007 — `FakeMetadataSink` (a minimal, test-local, dual-interface
  fake implementing BOTH `IPassDebugMetadataSink`/`IPassDebugMetadataProvider`)
  and `AddRenderPassCallsSinkExactlyOnceWithCorrectDeclarationIndex` are the
  EXACT templates this phase's own sink-wiring test copies. Reuse the
  EXISTING `FakeMetadataSink` class in this file — do not redeclare a second
  one.

## Step 3: The Plan

1. Add `AddBlitPass()`'s declaration to `RenderGraphBuilder.h`.
2. Add its implementation to `RenderGraphBuilder.cpp`, exactly as shown
   above.
3. Tests (`RenderGraphBuilderTests.cpp`) — add, at minimum:
   - `AddBlitPassProducesCorrectlyPopulatedPassRecord` — build a
     `RenderGraphBuilder`, declare two dummy textures via `CreateTexture()`
     (or `ImportTexture()` — either works, `AddBlitPass()` does not care),
     construct a `BlitSpec{ .src = srcHandle, .dst = dstHandle }` (leave
     filter/depth/region fields at their defaults), call
     `builder.AddBlitPass("TestBlit", spec)`, then `Finish()` and inspect
     `input.passes[0]`: `kind == PassKind::Blit`; `blitCommand.has_value()`;
     `blitCommand->src == srcHandle && blitCommand->dst == dstHandle`;
     `reads.size() == 1 && reads[0].texture == srcHandle && reads[0].access
     == ResourceAccess::TransferSrc`; `writes.size() == 1 &&
     writes[0].texture == dstHandle && writes[0].access ==
     ResourceAccess::TransferDst`.
   - `AddBlitPassWithDstIsDepthTrueProducesDepthFlaggedWrite` — same shape,
     `BlitSpec::dstIsDepth = true` — assert
     `input.passes[0].writes[0].isDepthResource == true`.
   - `AddBlitPassWithSrcIsDepthTrueProducesDepthFlaggedRead` — the read-side
     counterpart, `BlitSpec::srcIsDepth = true` — assert
     `input.passes[0].reads[0].isDepthResource == true`.
   - `AddBlitPassCallsSinkWithBlitDrawKindAndSuppliedCategoryAndTags` —
     install the existing `FakeMetadataSink`, call `AddBlitPass()` with a
     non-default `category` (e.g. `RenderPassCategory::Debug`) and `tags`
     (e.g. `RenderPassTagMask{ 0x4u }`), then `sink.QueryPassDebugMetadata(0,
     ...)` — assert `drawKind == RenderPassDrawKind::Blit` (this is the ONE
     test proving the source document's own "stamps... `drawKind =
     RenderPassDrawKind::Blit`" requirement, adapted for the real,
     post-BIG-STEP-1 sink-based mechanism), `category ==
     RenderPassCategory::Debug`, `tags == RenderPassTagMask{ 0x4u }`.
   - `AddBlitPassCallerSuppliedRenderPassEventReachesPassRecordUnchanged` —
     call `AddBlitPass()` with an explicit non-default `renderPassEvent`
     (e.g. `RenderPassEvent::AfterEverything`) — assert
     `input.passes[0].renderPassEvent == RenderPassEvent::AfterEverything`.
   - `AddBlitPassDefaultsViewScopeToSharedAndCategoryToGeneral` — call
     `AddBlitPass()` supplying ONLY `name`/`spec` (every trailing parameter
     left at its own default) — assert `input.passes[0].viewScope ==
     ViewScope::Shared` (direct field) AND, via `FakeMetadataSink`, that the
     sink received `RenderPassCategory::General`/`RenderPassTagMask{0}`.
4. Confirm every pre-existing `AddRenderPass()`/`AddPass()`/
   `AddComputePass()` test in this file still passes unmodified — this
   phase adds a new method, it does not touch any existing one.

## Step 4: Verification

1. `ask_questions` first for any genuine ambiguity.
2. Incremental build: `cmake --build build`.
3. Targeted test run:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug -R RenderGraphBuilderTest --output-on-failure`
4. Write `PHASE5_COMPLETION_REPORT.md` into this same folder — explicitly
   confirm no Vulkan call was added anywhere in this phase (grep this
   phase's own diff for `vkCmd`/`VkCommandBuffer` — it should find nothing
   new).
5. `git_add` + `git_commit`.
