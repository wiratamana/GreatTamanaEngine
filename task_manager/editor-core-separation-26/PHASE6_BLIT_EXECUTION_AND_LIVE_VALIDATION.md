# PHASE6 — `vkCmdBlitImage2` execution branch + permanent capability query + live validation

Read `PHASE0_MASTER_STRATEGY.md` in full first (especially Locked Decisions
3 and 4). Read the source document Part B.2's `vkCmdBlitImage`/"REGION
VALIDATION"/"CAUTION — depth-format blit support"/"NON-GOAL" bullets, and
Part B.3 in full. Read `PHASE5_COMPLETION_REPORT.md` first.

**This is this campaign's single highest-risk phase** — the first (and
only) phase that adds a real Vulkan call, and the first that needs a live,
running Editor + real GPU + network round-trip to verify. Per PHASE0 Rule
4, this phase is the flagged candidate for an OPTIONAL `dispatch_sub_agent`
self-double-check of its own diff before writing the completion report.

## Step 1: The Goal

1. `RenderGraph::ExecuteCompiledGraph()` gains ONE new branch, right beside
   `if (pass.execute) { pass.execute(ctx); }`, that issues a real
   `vkCmdBlitImage2` for any surviving `PassKind::Blit` pass — selecting the
   correct color/depth physical image + aspect mask per side (mirroring
   `ApplyUsageBarrierIfNeeded()`'s own existing selection logic exactly),
   using `ResolveEffectiveBlitFilter()`/`ResolveBlitRegion()`/
   `IsValidBlitRegion()` (PHASE3) for the filter and region. This branch
   needs a live `Renderer&` to call the new `SupportsDepthBlit()` query on
   (see point 2) — `RenderGraph` does NOT currently keep any `Renderer`
   reference/pointer of its own anywhere (confirmed by direct read — its
   constructor takes `Renderer& renderer` only to forward into
   `m_resourcePool`/`m_timestampPool`'s own constructors, never storing it),
   so this phase must ALSO add a small new private member to `RenderGraph`
   itself for the first time — see Step 2's own dedicated bullet on this,
   it is not optional plumbing.
2. `VulkanDevice::SupportsDepthBlit()` exists — a small, permanent, cached
   hardware-capability query (mirroring `SupportsDrawIndirectCount()`'s own
   exact shape), backed by a new `SupportsBlitSrcDst(VkPhysicalDevice,
   VkFormat)` sibling added to the already-existing
   `Vulkan/FormatCapabilities.h`/`.cpp`. Surfaced through `Renderer` the same
   way `Renderer::DepthFormat()` already forwards `VulkanDevice::
   PickDepthFormat()`. Wired into the new execution branch's own
   debug-assert, so "must not be exercised until confirmed" (source
   document's own wording) is something the engine itself checks.
3. A new, small, permanent, `RenderPassCategory::Debug`-tagged
   `BlitValidation` pass (`src/Editor/BlitValidation.h`/`.cpp`) fills its own
   persistent 512x512 "BlitValidationSource" texture with a distinctive
   clear color every frame, then blits it into its own persistent 1024x1024
   "BlitValidationOutput" texture — the exact numeric shape the source
   document's own Part B.3 acceptance criterion names. Wired through a new
   `IEditorLayer::AddBlitValidationPass()` virtual method (mirroring
   `AddBlurValidationPass()`'s exact shape, minus its OWN bespoke
   ImGui-facing feature-enable toggle — Locked Decision 3 needs none of
   that). This pass declares its own real `builder.AddRenderPass()`/
   `builder.AddBlitPass()` calls DIRECTLY inside `BlitValidation::AddPass()`
   (never through the generic `RenderPipeline::DeclareOnePhase()` flush
   loop) — per `docs/conventions/render-pass-toggle-honesty.md`'s iron
   rule, THAT (not the ImGui-display question Locked Decision 3 settles)
   is what requires this pass to still thread a real
   `rg::RenderPassToggleRegistry*` down to its own declaration site and
   consult `NoteDeclaredAndCheckEnabled()` for each of its two pass names —
   see Step 2's own dedicated bullet on this; skipping it would freshly
   reintroduce the exact "Confirmed-Lie" bug class the
   `editor-core-separation-21`/`-22` campaigns each spent a full campaign
   finding and fixing (a cosmetic, non-functional "Enabled" checkbox in the
   "Render Graph" panel for this pass's own two rows).
4. Live proof: a real, running Editor session, `GET /get_texture?texture_name=
   BlitValidationOutput` returns a real, correctly-scaled, correctly-colored
   1024x1024 PNG. `GET /get_logs?min_level=Error` shows zero new errors.
5. Depth-to-depth: attempted for real (a second, small blit exercising
   `srcIsDepth`/`dstIsDepth`) IF `SupportsDepthBlit()` reports `true` on this
   dev machine's actual GPU; otherwise honestly documented as shipped-but-
   unverified, with the query's own returned value as evidence — never
   silently assumed to work either way.

## Step 2: The Situation

Re-confirm every line number below against the ACTUAL current file before
editing (re-search fresh — PHASE1-5 may have shifted line numbers slightly
in files this phase also touches).

- **`src/Renderer/RenderGraph/RenderGraph.h`/`.cpp` — a genuinely NEW
  private member is required, not an existing one to merely "call
  through."** Confirmed by direct read of the current constructor
  (`RenderGraph.cpp`):
  ```cpp
  RenderGraph::RenderGraph(Renderer& renderer)
      : m_resourcePool(renderer)
      , m_timestampPool(QueryVulkanContextInfo(renderer).device, ...)
  {
  }
  ```
  `renderer` is forwarded into `m_resourcePool`/`m_timestampPool`'s own
  constructors and then discarded — `RenderGraph` itself stores no
  `Renderer&`/`Renderer*` of its own anywhere today, and `RenderGraph.h`'s
  private section (`m_resourcePool`, `m_debugMetadataSink`,
  `m_debugMetadataProvider`, `m_timestampPool`, the two
  `RenderGraphNameSlotTable`s, the debug-texture registries) confirms this —
  there is no `m_renderer` field to find. (A doc comment elsewhere in this
  same file, on `PassContext`, mentions `m_renderer.BeginGraphPassRecording(...)`
  as an example real call site — that comment is describing CALLER code,
  e.g. `Core`'s own `m_renderer` member used from inside a pass's `execute`
  lambda, not anything belonging to the `RenderGraph` class itself. Do not
  be misled by it.) `RenderGraphResourcePool` (a sibling class `RenderGraph`
  already owns one of) solves this identical problem today by storing a
  plain, non-owning `Renderer* m_renderer = nullptr;` (never a reference) —
  chosen specifically so the owning class stays copy/move-ASSIGNABLE (a
  reference member permanently blocks assignment, only construction).
  Mirror that exact choice here: add
  ```cpp
  // Non-owning - Renderer outlives this RenderGraph for its entire
  // lifetime (RenderGraph is a plain member of Core, constructed with
  // Core's own Renderer - see Core.h). Needed starting this phase so
  // ExecuteCompiledGraph()'s new PassKind::Blit branch can call
  // SupportsDepthBlit() - mirrors RenderGraphResourcePool::m_renderer's
  // own identical "pointer, not reference, so the owning class stays
  // assignable" shape and reasoning.
  Renderer* m_renderer = nullptr;
  ```
  as a new private member of `RenderGraph` (`RenderGraph.h`), and set it in
  the constructor body (`RenderGraph.cpp`):
  ```cpp
  RenderGraph::RenderGraph(Renderer& renderer)
      : m_resourcePool(renderer)
      , m_timestampPool(...)
  {
      m_renderer = &renderer;
  }
  ```
  The new execution branch (below) then calls `m_renderer->SupportsDepthBlit()`
  (pointer syntax — NOT `m_renderer.SupportsDepthBlit()`).
- Lines ~556-561: the generic per-usage barrier loop
  (`ApplyUsageBarrierIfNeeded()`) already runs, for EVERY pass, BEFORE the
  `pass.execute`/new-blit-branch spot below — this means
  `physicalTextures[spec.src.index]`/`physicalTextures[spec.dst.index]`
  are ALREADY resolved AND already correctly barriered (into
  `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL`/`_DST_OPTIMAL`) by the time this
  phase's own new branch runs — this phase's branch reads them, it never
  re-resolves or re-barriers anything.
- Lines ~732-734 (today):
  ```cpp
  if (pass.execute) {
      pass.execute(ctx);
  }
  ```
  becomes:
  ```cpp
  if (pass.execute) {
      pass.execute(ctx);
  } else if (pass.kind == PassKind::Blit && pass.blitCommand.has_value()) {
      const BlitSpec& spec = *pass.blitCommand;

      const PhysicalTexture& srcTex = physicalTextures[spec.src.index];
      const PhysicalTexture& dstTex = physicalTextures[spec.dst.index];

      const VkImage srcImage = spec.srcIsDepth ? srcTex.target.depthImage : srcTex.target.image;
      const VkImage dstImage = spec.dstIsDepth ? dstTex.target.depthImage : dstTex.target.image;
      const VkImageAspectFlags srcAspect = spec.srcIsDepth
          ? (VK_IMAGE_ASPECT_DEPTH_BIT | (srcTex.target.depthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
          : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT);
      const VkImageAspectFlags dstAspect = spec.dstIsDepth
          ? (VK_IMAGE_ASPECT_DEPTH_BIT | (dstTex.target.depthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
          : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT);

      const ResolvedBlitRegion srcRegion =
          ResolveBlitRegion(spec.srcRegionMin, spec.srcRegionMax, srcTex.target.extent);
      const ResolvedBlitRegion dstRegion =
          ResolveBlitRegion(spec.dstRegionMin, spec.dstRegionMax, dstTex.target.extent);
      assert(IsValidBlitRegion(srcRegion, srcTex.target.extent) &&
          "RenderGraph::ExecuteCompiledGraph: blit pass declared an invalid SRC region");
      assert(IsValidBlitRegion(dstRegion, dstTex.target.extent) &&
          "RenderGraph::ExecuteCompiledGraph: blit pass declared an invalid DST region");

      // Source document's own "CAUTION" bullet - a real, engine-checked
      // precondition (Locked Decision 4), never a documented-only trust.
      // NOTE the pointer syntax - m_renderer is a NEW Renderer* member this
      // phase adds (see this file's own dedicated bullet above), not a
      // pre-existing Renderer&.
      assert((!spec.srcIsDepth && !spec.dstIsDepth) || m_renderer->SupportsDepthBlit()
          && "RenderGraph::ExecuteCompiledGraph: blit pass declared srcIsDepth/dstIsDepth but this "
             "device does not report VK_FORMAT_FEATURE_BLIT_SRC_BIT/_DST_BIT support for the engine's "
             "real depth format - see VulkanDevice::SupportsDepthBlit().");

      const VkFilter effectiveFilter = ResolveEffectiveBlitFilter(spec);

      VkImageBlit2 region{};
      region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
      region.srcSubresource = VkImageSubresourceLayers{ srcAspect, 0, 0, 1 };
      region.srcOffsets[0] = srcRegion.min;
      region.srcOffsets[1] = srcRegion.max;
      region.dstSubresource = VkImageSubresourceLayers{ dstAspect, 0, 0, 1 };
      region.dstOffsets[0] = dstRegion.min;
      region.dstOffsets[1] = dstRegion.max;

      VkBlitImageInfo2 blitInfo{};
      blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
      blitInfo.srcImage = srcImage;
      blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      blitInfo.dstImage = dstImage;
      blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      blitInfo.regionCount = 1;
      blitInfo.pRegions = &region;
      blitInfo.filter = effectiveFilter;

      vkCmdBlitImage2(cmd, &blitInfo);
  }
  ```
  **This engine already uses Vulkan 1.3 synchronization2/`*2` structures
  throughout** (confirmed: `RenderGraphBarrierPlanner.cpp`'s
  `EmitImageBarrier()`/`BuildImageMemoryBarrier2()` already use
  `VkImageMemoryBarrier2`/`vkCmdPipelineBarrier2`/`VkDependencyInfo`) —
  `vkCmdBlitImage2`/`VkBlitImageInfo2`/`VkImageBlit2` is the consistent,
  house-style choice, NOT the legacy `vkCmdBlitImage`/`VkImageBlit`. Confirm
  `volk` has these loaded (it should — `vkCmdPipelineBarrier2` already
  works today, and `vkCmdBlitImage2` is core in the same Vulkan 1.3
  baseline).
- `src/Renderer/Vulkan/FormatCapabilities.h`/`.cpp` — add a new sibling
  function immediately after `SupportsStorageImageUsage()` (confirmed:
  today this file has exactly that one function, a two-line body):
  ```cpp
  // Queries whether `format` supports BOTH VK_FORMAT_FEATURE_BLIT_SRC_BIT
  // and VK_FORMAT_FEATURE_BLIT_DST_BIT (i.e. vkCmdBlitImage2 is legal
  // against this format as EITHER side) on `physicalDevice` - mirrors
  // SupportsStorageImageUsage()'s own exact shape, for a different feature
  // bit pair. This exists because depth-format blit support is NOT
  // guaranteed by the Vulkan spec and is commonly UNSUPPORTED on real GPU
  // drivers even where the equivalent color-format support is universal -
  // see VulkanDevice::SupportsDepthBlit(), the ONE real caller.
  bool SupportsBlitSrcDst(VkPhysicalDevice physicalDevice, VkFormat format);
  ```
  Body (`.cpp`): identical shape to `SupportsStorageImageUsage()`, checking
  `(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0
  && (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) !=
  0`.
- `src/Renderer/Vulkan/VulkanDevice.h`/`.cpp`:
  - Add `bool m_supportsDepthBlit = false;` as a new private member,
    immediately after `m_supportsDrawIndirectCount` (the existing capability
    bool).
  - Add a public getter: `bool SupportsDepthBlit() const noexcept { return
    m_supportsDepthBlit; }`, mirroring `SupportsDrawIndirectCount()`'s own
    doc comment shape exactly (queried ONCE, in the constructor, never
    re-checked afterward).
  - **Confirm the constructor's actual current body before editing it —
    it is NOT one single spot for both existing capability probes.**
    Direct read of the current constructor:
    ```cpp
    VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface)
    {
        PickPhysicalDevice(instance, surface);
        CreateLogicalDevice();
        QueryTimestampCapability();
    }
    ```
    `QueryTimestampCapability()` runs LAST, after `CreateLogicalDevice()`
    returns. The draw-indirect-count probe is a DIFFERENT case — it runs
    INSIDE `CreateLogicalDevice()` itself
    (`m_supportsDrawIndirectCount = QueryDrawIndirectCountSupport(m_physicalDevice);`,
    called BEFORE that function's own `vkCreateDevice()` call, since the
    result decides whether to actually request the feature) — it does NOT
    run "after `CreateLogicalDevice()`" the way `QueryTimestampCapability()`
    does. Add the new depth-blit query as a plain FOURTH constructor
    statement, right after the existing `QueryTimestampCapability();` call
    (mirroring that one's placement, not the draw-indirect-count probe's):
    ```cpp
    VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface)
    {
        PickPhysicalDevice(instance, surface);
        CreateLogicalDevice();
        QueryTimestampCapability();
        m_supportsDepthBlit = SupportsBlitSrcDst(m_physicalDevice, PickDepthFormat());
    }
    ```
    `PickDepthFormat()` only needs `m_physicalDevice` (no `VkDevice`), so it
    is already safe to call at this point (needs `#include
    "FormatCapabilities.h"` in `VulkanDevice.cpp` if not already present —
    check first, it is not there today).
  - **DO NOT FORGET — the move constructor and move-assignment operator.**
    Confirmed by direct read: `VulkanDevice`'s move constructor and
    `operator=(VulkanDevice&&)` each hand-list EVERY member individually
    (never `= default`), and both already explicitly carry
    `m_timestampCapability`/`m_supportsDrawIndirectCount` across a move.
    `m_supportsDepthBlit` MUST be added to both, in the same place, the
    same way — e.g. `, m_supportsDepthBlit(other.m_supportsDepthBlit)` as
    the move constructor's own last member-initializer, and
    `m_supportsDepthBlit = other.m_supportsDepthBlit;` as the move-
    assignment operator's own last statement inside its `if (this !=
    &other)` block. Skipping this is a real, silent correctness gap, not
    cosmetic: `Renderer::m_device` is constructed in place today (never
    actually moved — confirmed by direct read of `Renderer.cpp`'s own
    constructor, which does `m_device(m_instance.Native(),
    m_surface.Native())`, never a move), so this gap would not misfire
    against any call site that exists right now — but leaving it
    unmirrored would silently reset `SupportsDepthBlit()` back to `false`
    for any future code path that DOES move a `VulkanDevice` (exactly the
    class of latent bug this codebase's own "no `default:` case, ever"/
    exhaustive-switch discipline exists to prevent for enums — the same
    discipline applies here to a hand-maintained member list).
- `src/Renderer/Renderer.h`/`.cpp` — add a thin forwarding method, mirroring
  `Renderer::DepthFormat()`'s own exact shape (confirmed: `Renderer` stores
  its `VulkanDevice` as a plain member named `m_device`, e.g.
  `m_depthFormat(m_device.PickDepthFormat())` in `Renderer`'s own
  constructor, and `Renderer::SupportsDrawIndirectCount()`'s own real body
  is `return m_device.SupportsDrawIndirectCount();` — copy that exact
  pattern):
  `bool SupportsDepthBlit() const noexcept { return
  m_device.SupportsDepthBlit(); }`.
- `src/Editor/EditorLayer.h` — add a new pure-virtual method, mirroring
  `AddBlurValidationPass()`'s own exact shape (`rg::RenderGraphBuilder&
  builder, Renderer& renderer, ..., rg::RenderPassToggleRegistry*
  toggleRegistry = nullptr`), MINUS the Scene-View-texture-read parameters
  that method needs and this one doesn't (`sceneViewHandle`/`sceneExtent`)
  — but KEEPING the trailing `toggleRegistry` parameter. Locked Decision 3
  only decided this pass needs no bespoke ImGui-facing feature-enable
  toggle/display apparatus (unlike `ctx.showBlurredSceneOutput`'s own
  panel checkbox) — it does NOT exempt this pass from
  `docs/conventions/render-pass-toggle-honesty.md`'s iron rule, which is a
  SEPARATE, independent, mandatory requirement for ANY pass declared via a
  direct `builder.AddRenderPass()`/`builder.AddBlitPass()` call (which is
  exactly what `BlitValidation::AddPass()` does, bypassing the generic
  `RenderPipeline::DeclareOnePhase()` flush loop entirely) — see that
  convention doc's own explicit words: "A pass with its own separate, real,
  bespoke feature toggle... still needs its own registry consult on top of
  that bespoke toggle — the two are independent, additional layers of
  granularity, not substitutes for each other." Skipping this for a pass
  with NO bespoke toggle at all is, if anything, an even clearer case for
  needing it: the registry consult would be the ONLY thing standing between
  this pass's own two rows in the "Render Graph" panel and a purely
  cosmetic, non-functional "Enabled" checkbox — exactly the "Confirmed-Lie"
  bug class `editor-core-separation-21`/`-22` each spent a full campaign
  finding and fixing, and which `RenderPassHonestyGuard`
  (`src/Editor/RenderPassHonestyGuard.h/.cpp`) will actually catch and log
  (`GTE_LOG_ERROR("RenderPassHonesty", ...)`) the first time anyone
  disables either row via the panel/`GET /render_graph/set_pass_enabled` if
  this is skipped:
  ```cpp
  // editor-core-separation-26 campaign, PHASE6 (Locked Decision 3) - a
  // small, permanent, Debug-category live proof that AddBlitPass() works,
  // verified purely via GET /get_texture (no ImGui display/bespoke
  // feature-enable toggle of any kind needed, unlike
  // AddBlurValidationPass()/AddGBufferValidationPass() above) - it always
  // runs when an Editor layer is present, so it needs no gate of its own.
  // `toggleRegistry` is still required, though (docs/conventions/
  // render-pass-toggle-honesty.md's iron rule) - this pass's own
  // "BlitValidationSourceFill"/"BlitValidationBlit" passes are declared via
  // a DIRECT builder.AddRenderPass()/AddBlitPass() call inside
  // BlitValidation::AddPass(), bypassing the generic
  // RenderPipeline::DeclareOnePhase() flush loop that would otherwise gate
  // them for free - without an explicit consult here, their own rows in
  // the "Render Graph" panel would be purely cosmetic checkboxes.
  virtual std::optional<rg::TextureHandle> AddBlitValidationPass(
      rg::RenderGraphBuilder& builder, Renderer& renderer,
      rg::RenderPassToggleRegistry* toggleRegistry = nullptr) = 0;
  ```
- `src/Editor/NullEditorLayer.cpp` — add the no-op stub, mirroring
  `AddBlurValidationPass()`'s own stub exactly:
  ```cpp
  std::optional<rg::TextureHandle> AddBlitValidationPass(
      rg::RenderGraphBuilder& /*builder*/, Renderer& /*renderer*/,
      rg::RenderPassToggleRegistry* /*toggleRegistry*/) override
  {
      return std::nullopt; // Headless/Player build - no Frame Debugger, no Debug-category passes at all.
  }
  ```
- `src/Editor/ImGuiEditorLayer.cpp` — add the real implementation right
  beside `AddBlurValidationPass()` (line ~469), and a new
  `BlitValidation m_blitValidation;` member right beside
  `ComputeBlurValidation m_blurValidation;` (line ~1094):
  ```cpp
  std::optional<rg::TextureHandle> AddBlitValidationPass(
      rg::RenderGraphBuilder& builder, Renderer& renderer,
      rg::RenderPassToggleRegistry* toggleRegistry) override
  {
      return m_blitValidation.AddPass(builder, renderer, toggleRegistry);
  }
  ```
- `src/Core/Core.cpp` — inside the SAME offscreen `Execute()` build lambda
  that already calls `AddBlurValidationPass()`/`AddGBufferValidationPass()`
  (lines ~1362-1398, the `std::vector<rg::TextureHandle> outputs = ...;`
  ... `return outputs;` block), add, immediately before `return outputs;`
  (`b` is confirmed, by direct read, to be this lambda's own builder
  parameter name — the same one `AddBlurValidationPass(b, m_renderer, ...)`
  already uses a few lines above; `m_renderPassToggleRegistry` is
  confirmed, by direct read of `Core.h`, to be `Core`'s own real
  `rg::RenderPassToggleRegistry` member, the exact same one
  `&m_renderPassToggleRegistry` is already passed as at the
  `AddBlurValidationPass`/`AddGBufferValidationPass` call sites immediately
  above this new code):
  ```cpp
  if (m_editorLayer != nullptr) {
      if (const std::optional<rg::TextureHandle> blitValidationHandle =
              m_editorLayer->AddBlitValidationPass(b, m_renderer, &m_renderPassToggleRegistry)) {
          outputs.push_back(*blitValidationHandle);
      }
  }
  ```
  (Confirm the builder parameter's real name in this exact lambda scope —
  it is `b` as of the `AddReplayPasses(b, ...)` call a few lines above this
  spot; re-verify before copying.) **This `outputs.push_back()` is not
  optional/cosmetic** — `BlitValidationOutput`'s `TextureHandle` has zero
  in-frame readers; without reaching this Execute() call's own
  `finalOutputs` root set, `RenderGraphCompiler::Compile()` culls the whole
  `BlitValidationBlit` pass every single frame, and neither
  `GET /get_texture` nor the Frame Debugger would ever show real content.
- `src/Editor/BlitValidation.h`/`.cpp` (NEW files — the one part of this
  whole campaign that needed a real, brand-new source file; add BOTH to
  the root `CMakeLists.txt`'s `gte_editor` explicit source list, mirroring
  `ComputeBlurValidation.h`/`.cpp`'s own existing entry there, confirmed at
  lines ~1142-1143 of that file): a small class, mirroring
  `ComputeBlurValidation`'s shape but far simpler — no compute pipeline, no
  descriptor set, no resize logic (both textures are FIXED size, forever).
  Confirmed against `ComputeBlurValidation.cpp`'s own real, already-
  compiling usage: `Renderer::CreateRenderTexture(width, height, format,
  colorDebugName, depthDebugName, allowStorageImageAccess)` is the real
  parameter order, and `RenderTexture::Target()` returns the `RenderTarget`
  `RenderGraphBuilder::ImportTexture(name, target, currentLayout)` expects
  directly — both used correctly below.
  ```cpp
  // BlitValidation.h
  #pragma once
  #include "../Renderer/RenderGraph/RenderGraphBuilder.h"
  #include "../Renderer/RenderTexture.h"
  #include <optional>

  namespace gte {
  namespace rg { class RenderPassToggleRegistry; }
  class Renderer;

  class BlitValidation {
  public:
      BlitValidation() = default;
      ~BlitValidation() = default;
      BlitValidation(const BlitValidation&) = delete;
      BlitValidation& operator=(const BlitValidation&) = delete;
      BlitValidation(BlitValidation&&) = delete;
      BlitValidation& operator=(BlitValidation&&) = delete;

      // `toggleRegistry` (default nullptr, mirroring ComputeBlurValidation/
      // GBufferValidation's own established precedent) - see this class's
      // own .cpp for why this is required even though this pass has no
      // bespoke ImGui-facing feature toggle of its own (docs/conventions/
      // render-pass-toggle-honesty.md's iron rule).
      rg::TextureHandle AddPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
          rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

  private:
      void EnsureInitialized(Renderer& renderer);

      std::optional<RenderTexture> m_source; // Fixed 512x512, forever.
      std::optional<RenderTexture> m_output;  // Fixed 1024x1024, forever.
  };

  } // namespace gte
  ```
  ```cpp
  // BlitValidation.cpp
  #include "BlitValidation.h"
  #include "../Renderer/Renderer.h"
  #include "../Renderer/RenderGraph/RenderGraph.h"
  #include "../Renderer/RenderGraph/RenderPassToggleGuard.h"
  #include <array>

  namespace gte {

  void BlitValidation::EnsureInitialized(Renderer& renderer)
  {
      if (m_source.has_value()) {
          return;
      }
      // R8G8B8A8_UNORM, never the swapchain's own negotiated format -
      // mirrors ComputeBlurValidation::EnsureInitialized()'s own identical
      // reasoning (this texture is never bound to the same Pipeline as the
      // swapchain/Game/Scene views).
      m_source.emplace(renderer.CreateRenderTexture(512, 512, VK_FORMAT_R8G8B8A8_UNORM,
          "BlitValidationSource", "BlitValidationSourceDepth", /*allowStorageImageAccess=*/false));
      m_output.emplace(renderer.CreateRenderTexture(1024, 1024, VK_FORMAT_R8G8B8A8_UNORM,
          "BlitValidationOutput", "BlitValidationOutputDepth", /*allowStorageImageAccess=*/false));
  }

  rg::TextureHandle BlitValidation::AddPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
      rg::RenderPassToggleRegistry* toggleRegistry)
  {
      EnsureInitialized(renderer);

      const rg::TextureHandle sourceHandle =
          builder.ImportTexture("BlitValidationSource", m_source->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
      const rg::TextureHandle outputHandle =
          builder.ImportTexture("BlitValidationOutput", m_output->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

      // docs/conventions/render-pass-toggle-honesty.md's iron rule, using
      // this codebase's own MANDATED helper for it
      // (src/Renderer/RenderGraph/RenderPassToggleGuard.h's
      // ShouldDeclareBuiltInPassThisFrame() - the exact function
      // editor-core-separation-22's own changelog names as "the mandated
      // pattern for gating ANY side effect... BEFORE that side effect
      // happens") - both passes below are declared via a DIRECT builder
      // call, bypassing the generic RenderPipeline::DeclareOnePhase() flush
      // loop that would otherwise gate them for free. Two INDEPENDENT calls
      // (never one shared consult) mirror GBufferValidation::AddPass()'s
      // own established "independent per-half gating" precedent - each of
      // this pass's two rows in the "Render Graph" panel must independently
      // mean something. `ShouldDeclareBuiltInPassThisFrame()` itself already
      // returns true unconditionally when `toggleRegistry` is nullptr, so no
      // separate null check is needed here.
      if (rg::ShouldDeclareBuiltInPassThisFrame(toggleRegistry, "BlitValidationSourceFill")) {
          // A tiny, distinctive, clear-only Graphics pass - real content for
          // the blit below to genuinely copy/scale, never a degenerate
          // all-black texture that would make a "did the blit actually run"
          // screenshot ambiguous.
          builder.AddRenderPass(
              "BlitValidationSourceFill", rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::Debug,
              [sourceHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                  pass.WriteColorAttachment(sourceHandle, std::array<float, 4>{ 0.85f, 0.15f, 0.55f, 1.0f });
              },
              [](rg::PassContext&) { /* clear-only - no draws issued */ },
              rg::RenderPassDrawKind::DrawQuad, rg::RenderPassEvent::AfterEverything);
      }

      if (rg::ShouldDeclareBuiltInPassThisFrame(toggleRegistry, "BlitValidationBlit")) {
          rg::BlitSpec spec;
          spec.src = sourceHandle;
          spec.dst = outputHandle;
          builder.AddBlitPass("BlitValidationBlit", spec, rg::RenderPassEvent::AfterEverything,
              rg::ViewScope::Shared, rg::RenderPassCategory::Debug);
      }

      return outputHandle;
  }

  } // namespace gte
  ```
  Both passes are tagged the SAME `RenderPassEvent::AfterEverything` tier
  deliberately — `RenderGraphCompiler::Compile()`'s real RAW-edge detection
  (the fill pass writes `sourceHandle`, the blit pass reads it) resolves the
  correct order regardless of tier via declaration order as the tie-break
  (source document / render-pass-4 campaign's own already-proven "a real
  dependency always wins" rule) — confirm this holds by reading the actual
  live `GET /render_graph` execution order during Step 4 below, do not just
  assume it. `ShouldDeclareBuiltInPassThisFrame()`'s own real header
  (confirmed by direct read) is `src/Renderer/RenderGraph/
  RenderPassToggleGuard.h`, which itself already `#include`s
  `RenderPassToggleRegistry.h` transitively — no separate include needed.
- `CMakeLists.txt` — this phase is the ONE exception to this campaign's own
  "no new source file" expectation (PHASE0 Step 2/Rule 7) — add
  `src/Editor/BlitValidation.h` and `src/Editor/BlitValidation.cpp` to the
  `gte_editor` explicit source list, immediately beside
  `ComputeBlurValidation.h`/`.cpp`'s own existing entries.

## Step 3: The Plan

1. `RenderGraph.h`/`.cpp` — add the new `Renderer* m_renderer = nullptr;`
   member and wire it from the constructor (see Step 2's own dedicated
   bullet — this is real, new plumbing, not a pre-existing member to merely
   confirm).
2. `FormatCapabilities.h`/`.cpp` — add `SupportsBlitSrcDst()`.
3. `VulkanDevice.h`/`.cpp` — add `m_supportsDepthBlit` + `SupportsDepthBlit()`
   + the new fourth constructor statement (right after
   `QueryTimestampCapability();` — NOT "wherever the draw-indirect-count
   probe runs," which is a different, earlier location inside
   `CreateLogicalDevice()`) — AND also add `m_supportsDepthBlit` to the
   move constructor's member-initializer list and the move-assignment
   operator's body (see Step 2's own dedicated "DO NOT FORGET" bullet).
4. `Renderer.h`/`.cpp` — add the forwarding `SupportsDepthBlit()`.
5. `RenderGraph.cpp` — add the `vkCmdBlitImage2` execution branch (using
   `m_renderer->SupportsDepthBlit()`, pointer syntax, per step 1 above).
6. `EditorLayer.h` / `NullEditorLayer.cpp` / `ImGuiEditorLayer.cpp` — add
   `AddBlitValidationPass()` (interface + both implementations), WITH the
   trailing `rg::RenderPassToggleRegistry* toggleRegistry = nullptr`
   parameter (needed for step 7's own registry consult, even though this
   pass has no bespoke ImGui-facing feature toggle of its own).
7. `BlitValidation.h`/`.cpp` (new files) + `CMakeLists.txt` entry — include
   the two independent `NoteDeclaredAndCheckEnabled()` consults (one per
   pass name) per `docs/conventions/render-pass-toggle-honesty.md`'s iron
   rule.
8. `Core.cpp` — wire the call site (passing `&m_renderPassToggleRegistry`)
   + `outputs.push_back()`.
9. Tests: this phase is primarily live/visual (Part B.3's own acceptance
   bar is a screenshot, not a new Tier-1 test) — but if, while implementing
   the execution branch, you find any FURTHER pure decision worth
   extracting (mirroring PHASE3's own Locked-Decision-1 precedent), do so
   and test it the same way; do not force a Tier-1 test onto the
   `vkCmdBlitImage2` call itself (a real Vulkan call is, by this codebase's
   own convention, only ever verified live).

## Step 4: Verification (live, HTTP-driven — this phase's real acceptance)

1. `ask_questions` first for any genuine ambiguity — in particular, confirm
   `Renderer`'s exact member name for its owned `VulkanDevice` before
   writing `Renderer::SupportsDepthBlit()`'s body, and confirm
   `RenderTexture`'s exact constructor/`Target()`/format-choice API against
   `ComputeBlurValidation.cpp`'s own real, already-compiling usage rather
   than trusting this document's paraphrase alone. Also confirm
   `RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()`'s exact
   signature/header path against `GBufferValidation.cpp`'s own real,
   already-compiling usage.
2. Incremental build: `cmake --build build`.
3. `run_app_background` the built `GreatTamanaEditor.exe`. Wait for it to be
   ready (poll `GET /get_logs` or `GET /get_swapchain` until a real
   response comes back, rather than a fixed sleep).
4. `gte_send_request("/list_textures")` — confirm `"BlitValidationSource"`
   and `"BlitValidationOutput"` both appear.
5. `gte_send_request("/get_texture?texture_name=BlitValidationOutput")` —
   confirm a real PNG comes back; `load_image` it and visually confirm it
   is a solid, correctly-colored, 1024x1024 fill (no stretch/garbage/black
   frame). Also fetch `"BlitValidationSource"` the same way for a
   side-by-side sanity check (should look like the SAME color, just
   512x512).
6. `gte_send_request("/get_logs?min_level=Error")` — confirm `count: 0` (or
   only pre-existing, unrelated errors — cross-check against a baseline if
   any exist).
7. `gte_send_request("/render_graph")` — confirm `"BlitValidationSourceFill"`
   and `"BlitValidationBlit"` both appear, with `"BlitValidationBlit"`'s own
   `"kind"` field reading `"Blit"` (proving `ToString(PassKind)`'s PHASE3 fix
   is live end-to-end) and its `"draw_kind"` reading `"Blit"` too. Also
   confirm the "Render Graph" panel's own live pass table (or
   `GET /render_graph/passes`, whichever the panel's own data source is)
   shows both names with their "Enabled" checkbox genuinely wired — a
   quick, optional but recommended extra check: toggle
   `"BlitValidationBlit"` off via `GET /render_graph/set_pass_enabled` and
   confirm it genuinely stops appearing in the very next
   `GET /render_graph`/Frame Debugger capture (proving the new registry
   consult is real, not merely present in source), then re-enable it before
   moving on.
8. If `SupportsDepthBlit()` was `true` on this machine — repeat steps 4-6 for
   the depth companion textures/channel (`GET /get_texture?texture_name=
   BlitValidationOutput&channel=depth`, per the existing `/get_texture`
   query-param convention already confirmed in `NetworkServer.cpp`).
9. `stop_app_background` the Editor process — every time, no exception.
10. Optionally, `dispatch_sub_agent` to independently re-verify this phase's
    own diff + live evidence before writing the completion report (PHASE0
    Rule 4) — must also use `ask_questions`, must NOT create its own report
    file.
11. Write `PHASE6_COMPLETION_REPORT.md` into this same folder — include the
    actual screenshot evidence description (what `load_image` showed), the
    actual `SupportsDepthBlit()` result and GPU name on this dev machine,
    and an explicit statement of whether the depth-to-depth path was
    exercised live or shipped-but-unverified.
12. `git_add` + `git_commit` (code + new files + report, one commit).
