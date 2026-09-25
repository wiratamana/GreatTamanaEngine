# editor-core-separation-9 — PHASE2: Operation Registry & Adapter v3

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it FIRST, in full, especially
Step 2.3 (both Corrections), Step 2.5 (handle translation), Step 2.6
(compositor wiring), and Locked Architecture Decisions #9–#12. Also read
`PHASE1_COMPLETION_REPORT.md` before starting.

**Use `ask_questions`** whenever a real design ambiguity comes up that
`PHASE0_MASTER_STRATEGY.md` or this file does not already resolve. If you
delegate any further work, that work must also be told to use
`ask_questions`.

This is the single largest, most load-bearing phase in this campaign — it
is allowed to take longer than the others. Do not rush the descriptor-set/
pipeline-ownership migration (Step 3.2 below); a mistake there breaks `_v2`
in production, not just `_v3`. Do not rush Step 3.4 either — this phase's
own required reading of the REAL render-graph execution code (`RenderGraph.cpp`,
`RenderGraphTypes.h`) surfaced two genuine, silent, load-bearing hazards
(explicit `RenderPassEvent` tagging, and imported-texture samplers) that a
naive reading of the Design Doc's own pseudocode would NOT catch — see Step
2.4/2.5 below before writing any adapter code.

## Step 1: The Goal

Ship a REAL `PluginRenderOperationRegistry` (host-owned, growable,
string-keyed) and a REAL `PluginRenderPassBuilderAdapter_v3` wired into
`RenderFeatureCompositor`, such that:

1. A `_v3` demo plugin can reimplement `_v2`'s exact 3 existing effects
   (`gte.builtin.solid_fill`/`gte.builtin.radial_vignette`/
   `gte.builtin.color_grade`) via generic `Dispatch(opId, ...)` calls, and
   produce PIXEL-IDENTICAL output to the existing `_v2` demo plugin — proving
   the registry is a faithful, complete replacement for the fixed-method
   dispatch, not just a superficially similar one.
2. A genuinely NEW operation, `gte.builtin.box_blur`, is added to the
   registry, reusing the ALREADY-EXISTING `src/Shaders/BoxBlur.comp` shader
   verbatim, with **zero change to `IPluginRenderPassBuilder_v3`'s own C++
   interface** — the concrete, load-bearing proof of the Design Doc's R13
   central claim. (PHASE2 registers this operation but is NOT required to
   exercise/dispatch it — see Non-Goals; PHASE3 is the first real consumer.)
3. `_v2`'s own production behavior is BYTE-FOR-BYTE UNCHANGED throughout —
   this phase's own pipeline-ownership migration (Step 3.2) is a pure
   refactor, re-verified against `_v2`'s existing demo plugin(s) before this
   phase is considered done.
4. Every `_v3` pass this phase's adapter declares schedules, barriers, and
   samples correctly against the REAL render graph — not merely "compiles
   and doesn't crash" (see Step 2.4/2.5/3.4 for the two concrete correctness
   hazards this goal specifically guards against).

## Step 2: The Situation

Read in full before starting (all already read and cited during this
campaign's planning — re-read them yourself before writing code, do not
rely solely on this summary):

- `src/Core/Plugins/RenderFeatureCompositor.h/.cpp` — the ENTIRE file, both
  halves (ops dispatch AND blend dispatch). Pay special attention to
  `EnsureOpsInitialized()`/`EnsureBlendPipelineInitialized()` (where
  `m_opsPipeline`/`m_blendPipeline` are lazily constructed exactly once),
  `ContributeRenderGraphPasses()` (the per-entry loop this phase's own
  `moduleV3` branch slots into — note its own TWO existing calls to
  `EnsureOpsInitialized(m_renderer)`/`EnsureBlendPipelineInitialized(m_renderer)`
  near its top, BEFORE the seed dispatch — see Step 3.2 for why these two
  call sites must be edited, not merely left alone), and `DispatchOps()`/
  `DispatchBlend()`'s own `builder.AddRenderPass(..., rg::RenderPassDrawKind::DrawMesh,
  rg::RenderPassEvent::AfterEverything)` trailing arguments — **note that
  BOTH existing dispatch helpers explicitly pass `RenderPassEvent::AfterEverything`,
  never the default** (see Step 2.4 below for exactly why this is
  load-bearing, not decorative).
- `src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.h/.cpp` — the exact
  shape/lifetime convention `PluginRenderPassBuilderAdapter_v3` mirrors
  (constructed fresh per plugin, per view, per frame; never held longer).
- `src/Core/Core.cpp`'s `Core::RegisterBuiltinCapabilityOrchestrators()`
  (confirmed exact name/location — this is where
  `std::make_unique<RenderFeatureCompositor>(*this, m_renderer)` is actually
  called today) and `src/Core/Core.h`'s member list (`Renderer m_renderer;`
  followed, much later, by
  `std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>> m_capabilityOrchestrators;`)
  — see Step 3.2 for exactly where the new registry member goes and why its
  declaration position in `Core.h` matters.
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` — `PassBuilder`'s full
  method list (`ReadTexture`/`WriteTexture`/`ReadBuffer`/`WriteBuffer`/
  `WriteColorAttachment`), `CreateTexture`/`CreateBuffer`, and BOTH
  `AddRenderPass()` overloads' full parameter lists, in particular their
  trailing `RenderPassEvent renderPassEvent = RenderPassEvent::Opaques`
  default — this default is WRONG for every pass this phase's adapter
  declares (see Step 2.4).
- `src/Renderer/RenderGraph/RenderGraphTypes.h` — `RenderPassEvent`'s own doc
  comment (confirms it is REAL, load-bearing ordering input as of the
  `render-pass-4` campaign, not merely descriptive) and `PassContext`'s
  nested `ResolvedTexture{ view, sampler }` shape.
- `src/Renderer/RenderGraph/RenderGraph.cpp` — `EnsureTextureResolved()`:
  confirms, in the real, current code, that an IMPORTED texture always
  resolves with `tex.sampler = VK_NULL_HANDLE` (`TextureImportInfo` carries
  no sampler of its own), while a transient/pooled (`CreateTexture()`-minted)
  texture resolves with a REAL sampler (`tex.sampler = renderTexture.Sampler();`).
  See Step 2.5 below for why this is directly load-bearing for `_v3`.
- `src/Core/Core.h`'s `Core::PluginRenderFeatureTargetInfo` struct — carries
  BOTH `rg::TextureHandle target` AND `VkSampler sampler`, with a doc comment
  explicitly explaining why the sampler field exists separately: *"an
  imported TextureHandle's own `PassContext::resolveTexture()` never carries
  a sampler"*. This is the SAME fact Step 2.5 below is about — the engine's
  own code already documents this hazard once; `_v3`'s adapter must not
  reintroduce it.
- `src/Renderer/ComputeDescriptorSet.h` + `src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h`
  — both in full (already quoted in `PHASE0_MASTER_STRATEGY.md` Step 2.2,
  re-read the real files for exact method signatures before writing code).
  Note `DescriptorSetLayoutBuilder::AddStorageBuffer`/`AddStorageImage`/
  `AddCombinedImageSampler`'s `stageFlags` parameter defaults to
  `VK_SHADER_STAGE_COMPUTE_BIT` — correct for every op THIS phase registers
  (all `Compute`-kind), but a FUTURE `DrawFullscreenTriangle`-kind op (PHASE3's
  `gte.builtin.blit_fullscreen`) must pass `VK_SHADER_STAGE_FRAGMENT_BIT`
  explicitly instead of relying on this default — flagged here so PHASE3
  does not silently inherit the wrong stage flag.
- `src/Shaders/BoxBlur.comp` + `src/Editor/ComputeBlurValidation.h/.cpp` —
  the EXACT existing binding convention (`binding=0` `sampler2D`
  read-only, `binding=1` `rgba8 image2D` write-only, `(width,height)`
  `uint32` push-constant pair, `local_size_x/y=16`) this phase's registry
  entry for `gte.builtin.box_blur` must match byte-for-byte. Also re-read
  `ComputeBlurValidation.cpp`'s own `AddPass()` — its trailing
  `rg::RenderPassEvent::AfterTransparents` argument comes with a long,
  explicit doc comment explaining EXACTLY the hazard Step 2.4 below
  describes, for a different pass — this is the second, independent,
  real-code confirmation of the same rule.
- `CMakeLists.txt`'s `gte_add_shader(GreatTamanaEditor src/Shaders/...)`
  calls — confirm `BoxBlur.comp` is ALREADY registered there (it is, for
  `ComputeBlurValidation` — no new `gte_add_shader()` call is needed for
  `gte.builtin.box_blur` to reuse the same compiled `.spv`).

**Load-bearing fact confirmed during planning**: `RenderFeatureCompositor`
constructs its OWN separate `ComputePipeline` for `RenderFeatureOps.comp`
(`m_opsPipeline`) — this is a DIFFERENT `ComputePipeline` instance than
whatever `ComputeBlurValidation.cpp` builds for `BoxBlur.comp`. This phase's
registry does NOT reuse `ComputeBlurValidation`'s own pipeline instance
(that class owns its own lifetime independently, for its own unrelated
Editor-debug-toggle purpose) — it builds its OWN, separate `ComputePipeline`
from the SAME already-compiled `shaders/BoxBlur.comp.spv` file, exactly like
`RenderFeatureCompositor` already builds its own separate pipeline from
`shaders/RenderFeatureOps.comp.spv` rather than sharing one with anything
else. Confirm this is still true by reading `ComputeBlurValidation.cpp`'s
own pipeline-construction call before writing the registry's own.

### 2.4 — Why every `_v3`-declared pass MUST be explicitly tagged `RenderPassEvent::AfterEverything`

This is NOT optional/cosmetic. `render-pass-4`'s campaign (see `AGENTS.md`'s
"Render Pass System" section, and `RenderGraphTypes.h`'s own
`RenderPassEvent` doc comment) made `RenderPassEvent` REAL, load-bearing
ordering input: `RenderGraphCompiler::Compile()` stable-sorts every declared
pass by `(RenderPassEvent, declaration index)` into an "effective order",
then walks passes IN THAT ORDER to build its RAW/WAW dependency edges. A
pass whose `RenderPassEvent` places it EARLIER in effective order than the
real writer of a handle it reads will have its read resolved against the
WRONG (or no) writer — a genuine, silent GPU hazard, not merely a lint
issue (this is the exact bug class `ComputeBlurValidation.cpp`'s own
`AfterTransparents` tag, cited above, exists to prevent for a different
pass).

Every `_v3` plugin pass this adapter declares runs in EXACTLY the same
structural position in the frame that `_v2`'s `DispatchOps()`/`DispatchBlend()`
already run in (between the per-view "Seed" dispatch and that plugin's own
blend dispatch, all of which are explicitly tagged `RenderPassEvent::AfterEverything`
today) — a `_v3` pass reading `"SceneColor"` (== `resolved->target`, the
already-composited scene) has a REAL data dependency on whatever pass last
wrote that handle (typically the Atmosphere composite pass, tagged
`AfterTransparents`), and the Seed dispatch reading the SAME handle is
ALREADY tagged `AfterEverything` specifically so it is walked AFTER that
real writer. If a `_v3` pass were left at `AddRenderPass()`'s own default
(`RenderPassEvent::Opaques`), it would be walked BEFORE that real writer in
effective order — silently producing a graph with no dependency edge at
all between them, i.e. a plugin's compute pass could run before the scene
it reads is actually finished compositing.

**Fix, locked for this phase**: `PluginRenderPassBuilderAdapter_v3`'s own
`AddGraphicsPass()`/`AddComputePass()` implementations (Step 3.4) must
themselves call the real `rg::RenderGraphBuilder::AddRenderPass()` overload
with an EXPLICIT trailing `rg::RenderPassEvent::AfterEverything` argument —
hardcoded inside the adapter, never exposed as a plugin-facing parameter
(this is a purely internal-engine scheduling concern a plugin author should
never need to know exists, mirroring how the `opCode`-stamping mechanism in
Step 3.4 point 8 is also silently handled by the adapter, never surfaced to
the plugin).

### 2.5 — Why `ctx.resolveTexture(handle).sampler` cannot be trusted for `"SceneColor"` (or for `GetPrivateOutputTarget()`)

Confirmed directly in `RenderGraph.cpp`'s `EnsureTextureResolved()`: an
IMPORTED `TextureHandle` (created via `RenderGraphBuilder::ImportTexture()`)
ALWAYS resolves with `sampler == VK_NULL_HANDLE` — `TextureImportInfo` has
no sampler field to carry one. Only a TRANSIENT/pooled handle (minted by
`RenderGraphBuilder::CreateTexture()`) resolves with a real sampler (the
underlying pooled `RenderTexture`'s own `Sampler()`). This is precisely why
`Core::PluginRenderFeatureTargetInfo` carries its OWN separate `VkSampler
sampler` field alongside `rg::TextureHandle target` — its own doc comment
says so explicitly: *"an imported TextureHandle's own `PassContext::resolveTexture()`
never carries a sampler"*.

Both `"SceneColor"` (`TryGetNamedTexture`, resolving to `resolved->target`)
and a plugin's own `GetPrivateOutputTarget()` (resolving to `privateTarget`)
are ALWAYS imported handles in this campaign (see `ContributeRenderGraphPasses()`'s
own `frame.builder.ImportTexture(...)` calls for both). If `_v3`'s `Dispatch()`
naively built a `ComputeDescriptorWrite::CombinedImageSampler(binding,
ctx.resolveTexture(handle).view, ctx.resolveTexture(handle).sampler)` for
either of these, the resulting descriptor write would carry
`sampler == VK_NULL_HANDLE` — an invalid `VkDescriptorImageInfo` for a
`COMBINED_IMAGE_SAMPLER` descriptor (a validation-layer error at best, driver-
dependent undefined behavior at worst). This would silently break the very
first real consumer of a `CombinedImageSampler` slot against `"SceneColor"` —
PHASE3's own `gte.builtin.box_blur`-based blur demo, whose binding 0 is
exactly this kind of slot.

**Fix, locked for this phase**: see Step 3.4's handle-translation-table
design — every table entry carries an optional "known external sampler"
override (`VkSampler`, default `VK_NULL_HANDLE`), used by `Dispatch()`/
`DrawFullscreenTriangle()` in preference to `ctx.resolveTexture(handle).sampler`
whenever it is set. The cached `"SceneColor"` entry sets this override from
`resolved->sampler` (which must therefore be threaded into the adapter's own
constructor as a new parameter, alongside `resolved->target` — see Step 3.3).
`CreateTexture()`-minted entries leave the override at `VK_NULL_HANDLE` and
correctly fall back to the real, ctx-resolved sampler (which IS valid for a
pooled/transient resource). Binding `GetPrivateOutputTarget()`'s own handle
as a `CombinedImageSampler` slot is refused (loud warning, `false` returned)
— reading a plugin's own in-progress private output back within the same
frame is not a supported use case this campaign, and no override sampler is
tracked for it.

### 2.6 — A THIRD hazard, found during this review pass: a `_v3` operation's descriptor set must be PERSISTENT (per literal pass declaration), never allocated fresh every frame or shared across two different bound resources within one frame

This is not in the Design Doc's own pseudocode and is not one of Corrections
#1/#2 above — it is a real, load-bearing Vulkan/pool-lifetime hazard found by
directly reading `GpuResourceFactory.cpp`'s own compute descriptor pool setup,
and it MUST be designed around before writing `Dispatch()`/`DrawFullscreenTriangle()`
(Step 3.4), not discovered by a crash during this phase's own live smoke test.

**Fact 1 (pool exhaustion):** `GpuResourceFactory.cpp`'s compute descriptor
pool (`m_computeDescriptorPool`) is created with a HARD-CAPPED
`kMaxComputeDescriptorSets = 256` (`VkDescriptorPoolCreateInfo::maxSets`), and
`GpuResourceFactory::AllocateComputeDescriptorSet()`'s own doc comment states
plainly: "individual sets allocated from it are never freed... only the whole
pool at once, in `Destroy()`". Every EXISTING real call site in this engine
(`RenderFeatureCompositor::EnsurePrivateTargetState()`/`EnsureBlendStageDescriptorOnly()`,
`AtmosphereLutRenderer`'s own several `EnsureXInitialized()` methods,
`ComputeBlurValidation::EnsureInitialized()`) allocates EXACTLY ONCE, lazily,
into a member that PERSISTS for the remaining lifetime of its owning object,
then calls `.Rewrite()` on that SAME set every frame thereafter — NONE of them
ever call `AllocateComputeDescriptorSet()` more than once per owner.

**The hazard:** if `Dispatch()`'s own descriptor-set cache is scoped to the
`PluginRenderPassBuilderAdapter_v3` INSTANCE (as an earlier draft of this
section proposed), and a fresh adapter instance is constructed every single
frame (per Step 3.3/3.4 — confirmed, intentional, mirrors `_v2`'s own adapter
lifetime), then EVERY frame in which ANY loaded `_v3` plugin dispatches ANY
operation would call `AllocateComputeDescriptorSet()` AGAIN, permanently
consuming one more of the pool's fixed 256 slots. At a real 60 FPS with even
one active `_v3` plugin dispatching a single op every frame, this pool is
exhausted in a small number of SECONDS, at which point
`vkAllocateDescriptorSets` fails and `GpuResourceFactory::AllocateComputeDescriptorSet()`
throws `std::runtime_error` — a guaranteed, near-immediate engine crash that
WOULD surface during this very phase's own required live smoke test (Step 3.5
below runs the engine with a `_v3` demo plugin loaded for long enough to
capture a screenshot), not a theoretical/future concern.

**A second, independent hazard (same-frame descriptor-content collision):**
even ignoring pool exhaustion, a `VkDescriptorSet`'s CONTENT is read by the
GPU at the time it actually EXECUTES a `vkCmdBindDescriptorSets`/dispatch, not
at the time that command was RECORDED — `vkUpdateDescriptorSets()` (what
`ComputeDescriptorSet::Rewrite()` wraps) is a HOST-side write with no
command-buffer-relative ordering of its own. This engine records an entire
frame's passes into ONE `VkCommandBuffer`, submitted once at the end (confirmed
by `RenderGraph.cpp`/`Renderer.cpp` — every pass's `execute` callback receives
the SAME `ctx.cmd`). If the SAME literal `VkDescriptorSet` were `Rewrite()`-d
TWICE in one frame with two DIFFERENT sets of bound resources (e.g. a plugin
calling `Dispatch("gte.builtin.box_blur", ...)` twice in one frame against two
different texture pairs — a perfectly reasonable thing for a "generic,
growable operation" to be used for), only the LAST `Rewrite()`'s content would
actually be visible to the GPU for BOTH recorded dispatches once the command
buffer is submitted — silently corrupting the FIRST dispatch's real inputs,
not merely wasting a pool slot. (`_v2`'s existing `RenderFeatureOps.comp`
descriptor set never hits this today only because it has exactly ONE binding,
`privateTarget`, which is IDENTICAL across every op a given plugin might call —
re-`Rewrite()`-ing it with the same content twice is wasteful but harmless;
`_v3`'s generic, multi-binding ops like `gte.builtin.box_blur` have no such
accidental protection.)

**Fix, locked for this phase (REPLACES the "cache on the adapter instance,
keyed by opId" idea from an earlier draft of Step 3.4 — do not implement that
version):** `RenderFeatureCompositor` gains ONE more persistent member,
mirroring `m_privateTargetStates`'s own exact "lazily created once, `Rewrite()`-only
thereafter, never recreated per frame" lifetime discipline:

```cpp
// Keyed by "<Plugin>_<View>_<PassDebugName>" (a plain std::string key - no
// RenderFeatureNamePool interning needed here, since this map is never
// consulted by anything that needs a stable const char*, unlike
// PassRecord::name). ONE entry per literal AddGraphicsPass()/AddComputePass()
// debugName a _v3 plugin ever declares, for its own lifetime - bounded by
// (loaded _v3 plugin count) x (that plugin's own pass count) x (2 views),
// exactly like m_privateTargetStates/m_blendStageStates are already bounded.
std::unordered_map<std::string, ComputeDescriptorSet> m_v3OpDescriptorSets;

// Lazily allocates (ONCE, ever, per distinct `key`) or returns the existing
// ComputeDescriptorSet for `key` - never reallocates. Called by
// PluginRenderPassBuilderAdapter_v3::Dispatch()/DrawFullscreenTriangle()
// (Step 3.4) via a new `RenderFeatureCompositor& m_compositor` reference the
// adapter now also holds (mirroring PluginRenderPassBuilderAdapter_v2's own
// existing `m_compositor` member/pattern exactly).
ComputeDescriptorSet& EnsureV3OpDescriptorSet(const std::string& key, VkDescriptorSetLayout layout);
```

Because the cache key includes the pass's own literal `debugName` (which a
plugin author already must give a stable, distinct value per real pass
declaration, mirroring every internal engine pass's own unique-name
convention), a SINGLE plugin dispatching the SAME `opId` twice in one frame
from two DIFFERENT `AddComputePass()`/`AddGraphicsPass()` declarations
(two different `debugName`s) gets two DIFFERENT, independent, persistent
descriptor sets — completely closing the same-frame collision hazard above
alongside the pool-exhaustion hazard, with no additional bookkeeping. (A
plugin author calling `Dispatch()` more than once inside the SAME pass's
`execute` callback with different bindings against the same `opId` is still
sharing one descriptor set within that one call — document this as a real,
narrow constraint in the adapter's own header comment: "call `Dispatch()`
at most once per real bound-resource combination per declared pass"; no
real use case in this campaign's own PHASE2/PHASE3 scope ever needs more than
that.)

## Step 3: The Plan

### 3.1 — `src/Core/Plugins/PluginRenderOperationRegistry.h/.cpp` (NEW)

```cpp
// src/Core/Plugins/PluginRenderOperationRegistry.h (sketch - full doc
// comments must match this codebase's own density, see RenderFeatureCompositor.h
// for the bar to hit)
namespace gte {

enum class PluginRenderOpKind : std::uint8_t { Compute, DrawFullscreenTriangle };

struct PluginRenderOpSlot {
    VkDescriptorType type;   // VK_DESCRIPTOR_TYPE_STORAGE_IMAGE / COMBINED_IMAGE_SAMPLER / STORAGE_BUFFER
    bool isBuffer = false;   // true only for VK_DESCRIPTOR_TYPE_STORAGE_BUFFER slots
};

struct PluginRenderOpInfo {
    std::string id;
    PluginRenderOpKind kind = PluginRenderOpKind::Compute;
    std::vector<PluginRenderOpSlot> slots; // ORDERED - index == descriptor binding number
    std::uint32_t opCode = 0; // meaningful only for entries sharing the shared "uber ops" pipeline
    std::size_t maxParamBytes = 0; // the EXACT byte size this op's own VkPushConstantRange
                                    // was built with - Dispatch()/DrawFullscreenTriangle()
                                    // must reject a caller-supplied paramSize that does not
                                    // equal this value exactly (see Step 3.4) - never just
                                    // check it against the global 128-byte ceiling alone.
    // Compute-kind entries only:
    const ComputePipeline* computePipeline = nullptr;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    // DrawFullscreenTriangle-kind entries only (PHASE3 is the first real consumer):
    const Pipeline* graphicsPipeline = nullptr;
};

class PluginRenderOperationRegistry {
public:
    explicit PluginRenderOperationRegistry(Renderer& renderer);

    // Lazily builds every BUILT-IN operation's pipeline/layout on first call
    // (mirrors RenderFeatureCompositor::EnsureOpsInitialized()'s own former
    // lazy-init convention) - idempotent, safe to call every frame, safe to
    // call from BOTH a _v2-only frame (via DispatchOps()/DispatchBlend(),
    // Step 3.2) and a _v3-only frame (see Step 3.3 - the per-view Seed
    // dispatch always runs before any per-entry loop iteration, _v2 or _v3,
    // so this is ALWAYS called at least once before any _v3 Dispatch() call
    // could possibly need it - no separate call site is needed inside the
    // _v3 adapter itself).
    void EnsureBuiltinsRegistered();

    const PluginRenderOpInfo* Find(const std::string& id) const noexcept; // nullptr if unknown

    // Read-only access for RenderFeatureCompositor's OWN _v2 DispatchOps()/
    // DispatchBlend() to source the shared "uber ops"/blend pipeline through
    // (Step 3.2 migration) - never used by any plugin-facing code directly.
    const ComputePipeline& OpsPipeline() const noexcept;
    VkDescriptorSetLayout OpsDescriptorSetLayout() const noexcept;
    const ComputePipeline& BlendPipeline() const noexcept;
    VkDescriptorSetLayout BlendDescriptorSetLayout() const noexcept;

private:
    void RegisterUberOp(const char* id, std::uint32_t opCode);
    void RegisterBoxBlur();
    // Shared helper both of the above call at the very end, once their own
    // PluginRenderOpInfo is otherwise fully filled in - a debug-only assert
    // that slots.size() <= 8 (the fixed scratch-array size
    // IPluginCommandRecorder::BindTexture()/BindBuffer() bind into, Step 3.4)
    // - defensive, since nothing registered THIS phase gets remotely close to
    // 8 slots, but a future op accidentally declaring more must fail loudly
    // at registration time, not silently overflow a fixed-size array later.
    void InsertOp(PluginRenderOpInfo info);

    Renderer& m_renderer;
    bool m_builtinsRegistered = false;

    // Shared "uber ops" pipeline (RenderFeatureOps.comp) - ONE pipeline,
    // THREE registry entries (solid_fill/radial_vignette/color_grade) all
    // pointing at it, differing only by `opCode`. Migrated here FROM
    // RenderFeatureCompositor (Step 3.2) - the exact same construction code,
    // moved, not rewritten.
    std::optional<ComputePipeline> m_opsPipeline;
    VkDescriptorSetLayout m_opsDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_blendPipeline;
    VkDescriptorSetLayout m_blendDescriptorSetLayout = VK_NULL_HANDLE;

    // gte.builtin.box_blur's OWN, separate pipeline/layout.
    std::optional<ComputePipeline> m_boxBlurPipeline;
    VkDescriptorSetLayout m_boxBlurDescriptorSetLayout = VK_NULL_HANDLE;

    std::unordered_map<std::string, PluginRenderOpInfo> m_ops;
};

} // namespace gte
```

`RegisterUberOp("gte.builtin.solid_fill", /*opCode=*/0)`,
`RegisterUberOp("gte.builtin.radial_vignette", /*opCode=*/1)`,
`RegisterUberOp("gte.builtin.color_grade", /*opCode=*/2)` — each inserts a
`PluginRenderOpInfo` with `kind=Compute`, `slots={ {STORAGE_IMAGE, false} }`
(ONE slot, binding 0 — matches `RenderFeatureOps.comp` exactly),
`maxParamBytes=sizeof(RenderFeatureOpsPushConstants)` (64 bytes, the EXACT
size `m_opsPipeline`'s own `VkPushConstantRange` was built with in
`EnsureOpsInitialized()`/its migrated equivalent — see Step 3.2),
`computePipeline=&*m_opsPipeline`, `descriptorSetLayout=m_opsDescriptorSetLayout`.

`RegisterBoxBlur()` builds `m_boxBlurPipeline`/`m_boxBlurDescriptorSetLayout`
from `shaders/BoxBlur.comp.spv` (`DescriptorSetLayoutBuilder(device).AddCombinedImageSampler(0).AddStorageImage(1).Build()`,
a `VkPushConstantRange` sized `sizeof(std::uint32_t) * 2` = 8 bytes — mirrors
`ComputeBlurValidation.cpp`'s own construction byte-for-byte), then inserts
`{ id="gte.builtin.box_blur", kind=Compute, slots={ {COMBINED_IMAGE_SAMPLER,false}, {STORAGE_IMAGE,false} }, opCode=0 (unused), maxParamBytes=8, computePipeline=&*m_boxBlurPipeline, descriptorSetLayout=m_boxBlurDescriptorSetLayout }`.

### 3.2 — Migrate `RenderFeatureCompositor`'s pipeline ownership into the registry (pure refactor — zero `_v2` behavior change)

- `Core::RegisterBuiltinCapabilityOrchestrators()` (`Core.cpp`, confirmed
  exact name/location this phase — the function that already calls
  `std::make_unique<RenderFeatureCompositor>(*this, m_renderer)`) is where
  the new registry instance is USED; the instance itself must be a `Core`
  MEMBER (never a local in that function, since `RenderFeatureCompositor`
  only holds a REFERENCE to it, and a reference to a function-local would
  dangle the instant that function returns). Add a new `Core` member,
  e.g. `PluginRenderOperationRegistry m_pluginRenderOperationRegistry;`, to
  `Core.h` — it MUST be declared AFTER `Renderer m_renderer;` and BEFORE
  `std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>> m_capabilityOrchestrators;`
  in `Core.h`'s member list (C++ constructs members in DECLARATION order,
  never initializer-list order — `PluginRenderOperationRegistry`'s
  constructor takes `Renderer&` by reference, so `m_renderer` must already
  exist; `Core::RegisterBuiltinCapabilityOrchestrators()` itself is called
  from `Core`'s own constructor BODY, i.e. after ALL members are already
  constructed, so ordering only matters between the members themselves, not
  relative to that call). Construct it in `Core`'s own constructor
  member-initializer list as `m_pluginRenderOperationRegistry(m_renderer)`.
- `RenderFeatureCompositor`'s constructor gains a new trailing parameter,
  `PluginRenderOperationRegistry& operationRegistry` — update its ONE real
  construction call site
  (`std::make_unique<RenderFeatureCompositor>(*this, m_renderer, m_pluginRenderOperationRegistry)`)
  and store the reference as a new private member,
  `PluginRenderOperationRegistry& m_operationRegistry;`.
- `RenderFeatureCompositor::EnsureOpsInitialized()`/
  `EnsureBlendPipelineInitialized()` are DELETED (both the private method
  declarations in `RenderFeatureCompositor.h` AND their two definitions in
  `RenderFeatureCompositor.cpp`); `DispatchOps()`/`DispatchBlend()` call
  `m_operationRegistry.EnsureBuiltinsRegistered()` then read
  `m_operationRegistry.OpsPipeline()`/`OpsDescriptorSetLayout()`/
  `BlendPipeline()`/`BlendDescriptorSetLayout()` instead of their own former
  `m_opsPipeline`/`m_opsDescriptorSetLayout`/`m_blendPipeline`/
  `m_blendDescriptorSetLayout` members (all four members deleted from
  `RenderFeatureCompositor.h`). Every other line of `DispatchOps()`/
  `DispatchBlend()` stays byte-for-byte identical — this is a pure
  "who owns the pipeline" move, not a rewrite of the dispatch logic itself.
- **`ContributeRenderGraphPasses()`'s own EXISTING two call sites,
  `EnsureOpsInitialized(m_renderer); EnsureBlendPipelineInitialized(m_renderer);`
  (near the top of the method, right after the `resolved->has_value()` check),
  MUST be deleted as part of this same edit** — those two methods no longer
  exist after this migration, so leaving these two lines in place is a
  guaranteed compile error, not merely a stale comment. Nothing needs to
  replace them at that call site: the per-view Seed dispatch immediately
  below (`DispatchBlend(...)` for `seedName`) ALWAYS runs, unconditionally,
  every frame this method does any work at all — and `DispatchBlend()`
  itself now calls `m_operationRegistry.EnsureBuiltinsRegistered()`
  internally — so the registry is guaranteed initialized before the
  per-entry loop below it runs, for BOTH a `_v2`-only frame AND a
  `_v3`-only frame (this is exactly why `_v3`'s own adapter/`Dispatch()`
  does not need its own separate `EnsureBuiltinsRegistered()` call anywhere
  — see Step 3.4).
- **Verification of this migration specifically**: before touching anything
  else in this phase, do this migration FIRST, in isolation, build, run the
  EXISTING `_v2` demo plugin (`plugins/demo_render_feature/`), and confirm
  its rendered output is pixel-identical to before (a quick `GET
  /get_game_view` screenshot compare, or reuse whatever pixel-check method
  `editor-core-separation-6` PHASE6 already established) BEFORE proceeding to
  3.3+. If this step is skipped and a later step breaks `_v2`, it will be
  much harder to isolate which change caused it.

### 3.3 — `RenderFeatureCompositor::Entry`/`OnPluginsLoaded()`/`ContributeRenderGraphPasses()` wiring (Locked Architecture Decision #10)

- `Entry` gains `IRenderFeatureModule_v3* moduleV3 = nullptr;` (the existing
  `IRenderFeatureModule_v2* module` field is renamed `moduleV2` for symmetry
  — update every existing reference to `entry.module`/`.module` in the same
  edit: `OnPluginsLoaded()`, `ContributeRenderGraphPasses()`).
- `OnPluginsLoaded()`: for each `IPluginModule* module` in `modules`, query
  `kIRenderFeatureModule_v3_Name` FIRST; if non-null, build an `Entry` with
  `moduleV3` set (leave `moduleV2` null) and `descriptor =
  v3Feature->GetRenderFeatureDescriptor()`; ELSE query
  `kIRenderFeatureModule_v2_Name` exactly as today. If BOTH capabilities
  resolve non-null for the SAME `IPluginModule*`, log a loud
  `GTE_LOG_WARNING` naming the plugin (via `GetModuleInfo()`) and use the
  `_v3` one, ignoring `_v2` for that module (a plugin-author error, not a
  runtime-recoverable ambiguity worth crashing over).
- `ContributeRenderGraphPasses()`'s per-entry loop: after
  `EnsurePrivateTargetState()`/`ImportTexture()` produce `privateTarget`
  exactly as today, branch:
  ```cpp
  if (entry.moduleV3 != nullptr) {
      PluginRenderPassBuilderAdapter_v3 adapter(frame.builder, privateTarget,
          m_operationRegistry, /*blackboard=*/GetOrCreateNoOpBlackboard(),
          resolved->target, resolved->sampler,
          /*compositor=*/*this, /*opDescriptorSetKeyPrefix=*/pluginName + "_" + viewName + "_");
      entry.moduleV3->AddRenderGraphPasses(adapter);
  } else {
      PluginRenderPassBuilderAdapter_v2 adapter(frame.builder, privateTarget, *this, privateName);
      entry.moduleV2->AddRenderGraphPasses(adapter);
  }
  ```
  Everything AFTER this branch (the `DispatchBlend()` call) is completely
  unchanged — both adapters fill the SAME `privateTarget` handle either way.
  **`resolved->sampler` is a NEW, required argument this phase adds** (see
  Step 2.5/3.4 — without it, `_v3`'s own `TryGetNamedTexture("SceneColor")`
  has no valid sampler to bind against). `GetOrCreateNoOpBlackboard()`
  (or an equivalent small free function) returns a reference to a single,
  static, process-wide, always-empty `IPluginBlackboard` implementation
  (`Publish()` a no-op; `Fetch()` always returns "not found") — the simplest
  correct stand-in until PHASE4 wires the real, per-frame instance; this
  choice (rather than leaving it as an open 50/50 decision) is locked here
  so this phase's own diff is unambiguous, and PHASE4 knows exactly what
  single call site to replace. **`pluginName`/`viewName` (used here to build
  `opDescriptorSetKeyPrefix`) are the SAME `entry.descriptor.name`/`viewName`
  locals this loop iteration already computes for `privateName`/`accumName`
  above — no new value needs deriving, only threading two already-computed
  strings one step further** (see Step 2.6/3.4 for why this prefix exists —
  it is what makes each of a `_v3` plugin's own literal pass declarations get
  its own permanent, never-shared, never-reallocated-per-frame descriptor set).

### 3.4 — `src/Core/Plugins/PluginRenderPassBuilderAdapter_v3.h/.cpp` (NEW)

Constructed fresh per `(plugin, view, frame)`, exactly like
`PluginRenderPassBuilderAdapter_v2` (never held past one
`ContributeRenderGraphPasses()` loop iteration). Implements
`IPluginRenderPassBuilder_v3` + (as private nested helper classes or via
multiple inheritance, implementer's choice) `IPluginPassSetupContext` +
`IPluginCommandRecorder`.

**Constructor** (Step 3.3's own call site): `rg::RenderGraphBuilder& builder,
rg::TextureHandle privateTarget, PluginRenderOperationRegistry& operationRegistry,
IPluginBlackboard& blackboard, rg::TextureHandle sceneColorTarget, VkSampler
sceneColorSampler, RenderFeatureCompositor& compositor, std::string
opDescriptorSetKeyPrefix`. `sceneColorSampler` is `resolved->sampler` — the
ONLY correct source of a real sampler for the `"SceneColor"` handle (see Step
2.5). `compositor`/`opDescriptorSetKeyPrefix` are Step 2.6's own fix — stored
as `RenderFeatureCompositor& m_compositor;`/`std::string m_opDescriptorSetKeyPrefix;`
members, used ONLY by `Dispatch()`/`DrawFullscreenTriangle()` (Step 3.4 point 6
below) to call `m_compositor.EnsureV3OpDescriptorSet(m_opDescriptorSetKeyPrefix
+ debugName, layout)` — never used anywhere else on this class.

**Handle translation table** (`PHASE0_MASTER_STRATEGY.md` Step 2.5): a
private
```cpp
struct TranslatedTexture {
    rg::TextureHandle handle;
    VkSampler externalSamplerOverride = VK_NULL_HANDLE; // non-null ONLY for the
        // cached "SceneColor" entry (see below) - Dispatch()/
        // DrawFullscreenTriangle() prefer this over ctx.resolveTexture(handle).sampler
        // whenever it is set, since an IMPORTED handle's own ctx-resolved
        // sampler is always VK_NULL_HANDLE (Step 2.5).
    bool isPrivateOutputTarget = false; // true ONLY for the cached
        // GetPrivateOutputTarget() entry - refused as a CombinedImageSampler
        // bind target (see Dispatch()/DrawFullscreenTriangle() below).
};
std::vector<TranslatedTexture> m_textures;
std::vector<rg::BufferHandle> m_buffers;
```
plus a fixed `std::uint64_t m_generation` set ONCE, at construction, from a
process-wide `static std::atomic<std::uint64_t> s_nextGeneration{ 1 };`
(`m_generation = s_nextGeneration.fetch_add(1);`) — every
`PluginTextureHandle`/`PluginBufferHandle` this adapter instance mints
carries THIS exact value in its own `.generation` field. Because this
counter increments once per ADAPTER CONSTRUCTION (never per frame, never
reset, never per-handle), two adapters built in two different frames (or
for two different plugins the same frame) NEVER share a generation value —
this is what makes the defensive "reject a stale/foreign handle" check
below actually catch something, rather than merely existing in name only.

**Central resolution helpers** — every ABI-facing method that accepts a
`PluginTextureHandle`/`PluginBufferHandle` funnels through exactly one of
these two private helpers, so the validation logic below is written and
audited exactly once:
```cpp
const rg::TextureHandle* ResolveTexture(PluginTextureHandle handle) const noexcept {
    if (handle.generation != m_generation || handle.index >= m_textures.size()) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            "rejected a PluginTextureHandle that is out-of-range or from a stale/"
            "foreign adapter instance (generation mismatch) - a plugin must never "
            "hold a handle across frames or use one it did not receive from THIS "
            "call's own IPluginPassSetupContext/IPluginCommandRecorder.");
        return nullptr;
    }
    return &m_textures[handle.index].handle;
}
// ResolveBuffer(...) mirrors this exactly, against m_buffers.
```
`ReadTexture`/`WriteTexture`/`ReadBuffer`/`WriteBuffer`/`WriteColorAttachment`/
`BindTexture`/`BindBuffer` ALL call one of these two helpers first and
simply no-op (for the setup-context methods) or refuse to store the binding
(for `BindTexture`/`BindBuffer` — leaving that slot "unbound", which
`Dispatch()`'s own step 5 below then naturally catches and refuses the whole
call) when the helper returns `nullptr`. This single, shared choke point is
what actually delivers the "reject a malformed/stale handle" behavior every
individual method's own doc comment promises — it is deliberately NOT
reimplemented ad hoc per method.

`CreateTexture(name, desc)`: translate `desc` via `ToRgTextureDesc()`
(PHASE1), call `m_builder.CreateTexture(name, rgDesc)`, `push_back` a
`TranslatedTexture{ result, VK_NULL_HANDLE, false }` into `m_textures`,
return `PluginTextureHandle{ index=m_textures.size()-1, generation=m_generation }`.
Enforce the 32-resource-per-call cap (Locked Architecture Decision #12) —
refuse (loud warning, return an invalid handle) the 33rd+ `CreateTexture`/
`CreateBuffer` call in one `AddRenderGraphPasses()` invocation. Enforce the
8192×8192 dimension cap the same way. `CreateBuffer` mirrors this exactly
for `m_buffers` (plain `rg::BufferHandle` entries — no sampler concept for
buffers).

`GetPrivateOutputTarget()`: lazily inserts (or returns the ALREADY-cached)
entry for the constructor-supplied `privateTarget` (`rg::TextureHandle`)
into `m_textures` as `TranslatedTexture{ privateTarget, VK_NULL_HANDLE, /*isPrivateOutputTarget=*/true }`
— cached so repeated calls within one `AddRenderGraphPasses()` invocation
return the IDENTICAL `PluginTextureHandle` every time (never mint a second
entry for the same underlying handle).

`TryGetNamedTexture("SceneColor", out)`: if `semanticName` is exactly
`"SceneColor"`, lazily inserts (or returns the cached) entry for the
constructor-supplied `sceneColorTarget` as
`TranslatedTexture{ sceneColorTarget, sceneColorSampler, /*isPrivateOutputTarget=*/false }`
— note the sampler override is set here, from the constructor parameter,
NOT left null. Returns `true`. Any other `semanticName` returns `false`,
`out` left untouched.

`IPluginPassSetupContext::ReadTexture(handle, access)`/`WriteTexture(...)`/
`ReadBuffer(...)`/`WriteBuffer(...)`: resolve via `ResolveTexture()`/
`ResolveBuffer()` above (nullptr → no-op, already logged), translate
`access` via `ToRgAccess()` (PHASE1), call the corresponding real
`rg::RenderGraphBuilder::PassBuilder` method. `WriteColorAttachment(handle,
hasClearColor, r,g,b,a)`: resolve the same way, call
`pass.WriteColorAttachment(*resolvedHandle, hasClearColor ?
std::optional<std::array<float,4>>{{r,g,b,a}} : std::nullopt)`.

`AddGraphicsPass`/`AddComputePass`: call
```cpp
m_builder.AddRenderPass(debugName, PassKind::Graphics /* or Compute */,
    ViewScope::Shared, RenderPassCategory::General,
    [setup, userData](rg::RenderGraphBuilder::PassBuilder& pass) {
        /* wrap pass in a small IPluginPassSetupContext-implementing adapter
           bound to *this, then call setup(ctxAdapter, userData) */
    },
    [execute, userData, this, debugName](rg::PassContext& ctx) {
        /* build a small IPluginCommandRecorder-implementing adapter bound
           to *this, ctx, AND debugName (captured here) - Dispatch()/
           DrawFullscreenTriangle() (Step 3.4 point 6, per Step 2.6's fix)
           need this pass's OWN literal debugName to build the persistent
           descriptor-set cache key `m_opDescriptorSetKeyPrefix + debugName`,
           so it must be threaded this far down, not just to setup() */
        /* then call execute(recorderAdapter, userData) */
    },
    /*drawKind=*/rg::RenderPassDrawKind::DrawMesh,
    /*renderPassEvent=*/rg::RenderPassEvent::AfterEverything);
```
**The explicit trailing `rg::RenderPassEvent::AfterEverything` argument is
REQUIRED, not optional or defaulted** — see Step 2.4 for the full
correctness reasoning; leaving this at `AddRenderPass()`'s own default
(`Opaques`) is a real, silent scheduling bug, confirmed against the real
compiler/executor code, not a hypothetical concern. The plugin-author-chosen
`debugName` (a `const char*` literal per the ABI contract) is passed
straight through — this is the value that ends up as the real
`rg::PassRecord::name`, satisfying Design Doc R23.

`IPluginCommandRecorder::BindTexture(slot, handle)`/`BindBuffer(slot,
handle)`: resolve `handle` via `ResolveTexture()`/`ResolveBuffer()` FIRST
(nullptr → already logged, simply do not store anything for `slot` — this
naturally surfaces later as "slot not covered" if `Dispatch()`/
`DrawFullscreenTriangle()` is then called); otherwise store into a small,
per-`execute`-call scratch array (at most 8 entries, mirroring the Design
Doc's own §4.4 "slot index 0-7" convention, and matching
`PluginRenderOperationRegistry::InsertOp()`'s own debug-only
`slots.size() <= 8` assert, Step 3.1) — NOT applied to any real descriptor
set yet (that happens inside `Dispatch`/`DrawFullscreenTriangle`, once the
op is known and its slot table can be validated against).

`Dispatch(opId, paramBytes, paramSize, groupsX, groupsY, groupsZ)`:
1. `m_operationRegistry.Find(opId)` → `nullptr` → loud warning naming the
   plugin + `opId`, return `false` (Design Doc R13's own "unknown opId is
   refused" contract). (Checked FIRST, before any size/group-count check,
   since a malformed/unknown `opId` makes every later check meaningless —
   there is no `op->maxParamBytes`/`op->kind` to check anything against yet.)
2. `op->kind != Compute` → loud warning ("`opId` is a
   DrawFullscreenTriangle-kind operation, call `DrawFullscreenTriangle()`
   instead"), return `false`.
3. **`paramSize != op->maxParamBytes`** → loud warning naming the plugin +
   `opId` + both the expected and actual byte counts, return `false`. This
   is DELIBERATELY an exact-equality check, not merely `paramSize > 128`
   (Locked Architecture Decision #12's own 128-byte figure is a GLOBAL
   ceiling on any one op's `maxParamBytes`, never a substitute for checking
   a specific op's own declared size) — `op->computePipeline`'s real
   `VkPushConstantRange.size` was fixed, byte-for-byte, at registration time
   to `op->maxParamBytes`; calling `vkCmdPushConstants`/`Renderer::Dispatch()`
   with any OTHER byte count against that same pipeline layout is invalid
   Vulkan usage, not merely a logical mismatch. (`op->maxParamBytes` is
   itself already guaranteed `<= 128` by construction — every `RegisterX()`
   helper in Step 3.1 hardcodes a value `<= 128` — so this one check alone
   is both necessary and sufficient; a separate, redundant ">128" check is
   not needed once this exact-equality check exists.)
4. `groupsX/Y/Z` each `> 64` → loud warning, return `false` (Locked Product
   Decision #2).
5. For each bound slot (0..`op->slots.size()-1`) not covered by a prior
   `BindTexture`/`BindBuffer` call this `execute` invocation → loud warning,
   return `false`. For each bound slot whose kind (`isBuffer`) does not match
   what was actually bound (a `BindTexture` call against a buffer-kind slot,
   or vice versa) → loud warning, return `false`. For a `COMBINED_IMAGE_SAMPLER`
   slot whose bound `TranslatedTexture::isPrivateOutputTarget` is `true` →
   loud warning ("a plugin's own private output target cannot be sampled
   back within the same frame — no external sampler is tracked for it"),
   return `false` (see Step 2.5's own closing note).
6. Resolve each bound `PluginTextureHandle`/`PluginBufferHandle` back to its
   real `rg::TextureHandle`/`rg::BufferHandle` (already validated once by
   `BindTexture`/`BindBuffer` above — this step re-reads the SAME
   `TranslatedTexture`/`rg::BufferHandle` entries, never re-validates), then
   `ctx.resolveTexture(...)`/`ctx.resolveBuffer(...)` (mirror
   `RenderFeatureCompositor::DispatchOps()`'s own exact `ctx.resolveTexture()`
   usage) to get the real `VkImageView`/`VkBuffer`. For a `COMBINED_IMAGE_SAMPLER`
   slot, the SAMPLER used is `TranslatedTexture::externalSamplerOverride`
   when non-null, ELSE `ctx.resolveTexture(handle).sampler` (correct for a
   `CreateTexture()`-minted transient handle — see Step 2.5 for exactly why
   this two-way fallback is required, not merely one or the other). Build
   the `ComputeDescriptorWrite` vector using `op->slots[i].type` to pick
   `StorageImage()`/`CombinedImageSampler()`/`StorageBuffer()`, then call
   `ComputeDescriptorSet& descriptorSet = m_compositor.EnsureV3OpDescriptorSet(
   m_opDescriptorSetKeyPrefix + debugName, op->descriptorSetLayout);` (Step
   2.6's fix — **NEVER** cache this on the adapter instance itself: the
   adapter is reconstructed fresh every frame, so an adapter-instance-scoped
   cache would call `AllocateComputeDescriptorSet()` again every single frame,
   exhausting `GpuResourceFactory`'s fixed 256-set compute descriptor pool
   within seconds of real runtime, and would also let two DIFFERENT
   Dispatch() calls against the same `opId` within one frame silently
   corrupt each other's bindings before the frame's one shared command buffer
   is even submitted — see Step 2.6 for the full mechanical reasoning behind
   both hazards). `descriptorSet.Rewrite(device, writes)` every call (cheap,
   and required regardless, since the bound resources may differ frame to
   frame even for the SAME cache key).
7. Copy `paramBytes`/`paramSize` into a local, zeroed 128-byte scratch
   buffer (never pass the caller's own pointer straight to
   `Renderer::Dispatch()` without a bounds-checked copy first).
8. `m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw); m_renderer.Dispatch(*op->computePipeline, descriptorSet.Native(), scratch, static_cast<std::uint32_t>(op->maxParamBytes), groupsX, groupsY, groupsZ); m_renderer.EndGraphPassRecording();`
   — mirrors `RenderFeatureCompositor::DispatchOps()`'s own exact recording
   pattern. **The byte count passed to `Renderer::Dispatch()` is
   `op->maxParamBytes` (the registry's own, already-validated-equal-to-`paramSize`
   value), never the raw caller-supplied `paramSize` directly** — after step
   3's exact-equality check these are numerically identical, but sourcing it
   from `op->maxParamBytes` keeps this call site self-evidently tied to the
   SAME value the pipeline layout was built with, rather than silently
   re-trusting a caller-supplied number a future edit might weaken step 3's
   check without noticing this call site still depends on it. If `op->opCode`
   is meaningful for this op (shared uber-ops family), the FIRST 4 bytes of
   the scratch buffer's own `opCodeAndPad.x` field must be overwritten with
   `float(op->opCode)` before dispatch — a plugin calling
   `gte.builtin.solid_fill` never has to know an `opCode` exists at all; the
   ADAPTER stamps it in, transparently, from the registry's own
   `PluginRenderOpInfo::opCode` field. Document this exact mechanism clearly
   in the adapter's own header comment — it is the one place
   `RenderFeatureOpsPushConstants`'s own internal shape leaks into `_v3`'s
   otherwise-fully-generic code, and that leak must be readable and obvious
   to a future maintainer, not hidden.
9. Return `true` on success.

`DrawFullscreenTriangle(opId, paramBytes, paramSize)`: identical validation
shape (steps 1–3, 5, 6, 7 above — `op->kind != DrawFullscreenTriangle` refuses
at step 2's equivalent, there is no group-count check/step-4 equivalent, and
step 6's descriptor-set resolution/caching mechanism applies UNCHANGED, keyed
the SAME way, `m_opDescriptorSetKeyPrefix + debugName` — a graphics-kind op
needs its own bound resources resolved into a descriptor set exactly as much
as a compute-kind one does; omitting this step here was an oversight in an
earlier draft of this file, not a real difference between the two methods),
issuing the real
draw via the `graphicsPipeline`-based call shape PHASE3 confirms (see that
phase's own Step 3.2). PHASE2 does NOT need a real registered
`DrawFullscreenTriangle`-kind op yet (none exists until PHASE3's
`gte.builtin.blit_fullscreen`) — implement the full method now (so PHASE3
does not need to touch this file again), but it is acceptable, and expected,
for PHASE2's own verification to never actually exercise this method (no
`DrawFullscreenTriangle`-kind entries are registered yet).

### 3.5 — Demo-plugin parity proof (this phase's own required verification)

Add (or extend, if simpler) a `_v3`-based demo plugin —
`plugins/demo_render_feature_v3/` (finalize exact naming in this phase;
PHASE3 will ADD to this same plugin, not create a second one) —
implementing `IRenderFeatureModule_v3`, reproducing `_v2`'s existing demo
plugin's exact 3 effects (same debug names, same numeric parameters) via
`Dispatch("gte.builtin.solid_fill"/"gte.builtin.radial_vignette"/"gte.builtin.color_grade",
...)`.

**Worked example — the exact push-constant struct this demo plugin must
define locally** (the plugin cannot `#include` `RenderFeatureCompositor.h`,
a `gte_core`-internal file — per `PublicSurface.md`'s ABI rule — so it must
redeclare a byte-for-byte mirror of `RenderFeatureOpsPushConstants` in its
own plugin-side source):
```cpp
// Plugin-side mirror of gte_core's own RenderFeatureOpsPushConstants
// (RenderFeatureCompositor.h) - byte-for-byte identical layout, 64 bytes
// total. The FIRST vec4's .x (opCodeAndPad[0]) is IGNORED by the adapter -
// it is silently overwritten with this op's own registered opCode before
// dispatch (Step 3.4, point 8) - a plugin author using
// gte.builtin.solid_fill/radial_vignette/color_grade never sets this field
// meaningfully and never needs to know it exists; leave it zero-initialized.
struct DemoV3OpsPushConstants {
    float opCodeAndPad[4] = {};    // ignored/overwritten by the adapter - leave zeroed
    float colorRgba[4] = {};
    float centerAndRadius[4] = {};
    float gradeParams[4] = {};
};
// sizeof(DemoV3OpsPushConstants) == 64 == the registry's own maxParamBytes
// for all 3 of these ops - passed as `paramSize` to Dispatch() exactly.
```
Fill `colorRgba`/`centerAndRadius`/`gradeParams` exactly like
`PluginRenderPassBuilderAdapter_v2.cpp`'s own `AddSolidFillPass()`/
`AddRadialVignettePass()`/`AddColorGradePass()` already do (same field
meanings, same field positions) — this is what makes the pixel-parity claim
meaningful: both adapters end up filling the IDENTICAL 64 real bytes the
SAME shared `RenderFeatureOps.comp` pipeline reads, just through two
different C++ call shapes.

Run both demo plugins (one at a time, or side-by-side at different
priorities if that is simpler to compare), `gte_send_request` `GET
/get_game_view` for each, and confirm pixel-identical (or, if a legitimate
tiny floating-point/compositing-order difference exists, explain exactly
why in the completion report — do not silently wave away a real
discrepancy).

### 3.6 — Tier-1 tests for the adapter's own PURE validation logic (Design Doc R29, `AGENTS.md`'s "Testability & Regression Safety")

Several of the checks `Dispatch()`/`DrawFullscreenTriangle()`/`CreateTexture()`/
`CreateBuffer()` perform in Step 3.4 are plain logic over already-resolved
plain values — no live `VkDevice`/`Renderer&` involved — and must not be left
with zero automated coverage just because they happen to live inside a
GPU-owning class, mirroring `AGENTS.md`'s own explicit instruction: "Before
wiring new logic directly into a GPU/SDL-owning class, ask whether it can
instead be extracted as a small pure function/class that takes
already-resolved plain values - if it can, do that". Extract these into small,
free (or static-method) pure functions in a NEW,
`tests/Core/Plugins/PluginRenderPassBuilderAdapterV3ValidationTests.cpp`-testable
header (e.g. `src/Core/Plugins/PluginRenderPassBuilderAdapterV3Validation.h`,
mirroring `PluginRenderResourceTranslation.h`'s own PHASE1 precedent for
"pure logic lives in its own small header, separate from the GPU-owning
class that calls it"), covering AT LEAST:

- Handle validation: given a `(handle.generation, handle.index)` pair, a
  "current adapter generation" value, and a table size, does it correctly
  accept/reject (mirrors `ResolveTexture()`/`ResolveBuffer()`'s own logic,
  Step 3.4).
- The 32-resource-per-call / 8192×8192-dimension caps (Locked Architecture
  Decision #12) — one test per boundary (31 vs. 32 vs. 33 calls; 8192 vs.
  8193 in either dimension).
- The `groupsX/Y/Z <= 64` cap (Locked Product Decision #2) — one test per
  boundary per axis.
- `paramSize != op->maxParamBytes` exact-equality rejection (Dispatch() step
  3) — including the "equal" acceptance case.
- The "unbound slot"/"wrong-kind-bound-slot"/"`isPrivateOutputTarget` bound
  as `CombinedImageSampler`" rejection rules (Dispatch() step 5) — one test
  per rule.

This is NOT a request to make the WHOLE adapter Tier-1-testable (it is
fundamentally a GPU-owning class once `Dispatch()` actually records real
commands — that part stays Tier 2, exactly like every other GPU-facing class
in this engine, per `AGENTS.md`'s own "Tier 2" note) — only these specific,
already-pure decision points, which the Design Doc's own R29 explicitly names
("the operation-registry's own lookup logic" and, by the same reasoning,
its own caller-side validation logic) as something a future maintainer must
be able to change with a fast, GPU-free regression check, not just a live
smoke test. Add the new test file to `tests/CMakeLists.txt` the same way
PHASE1's own `PluginRenderResourceTranslationTests.cpp` was added.

## Verification

- `cmake --build build` (incremental).
- Build and run the new Step 3.6 Tier-1 test binary/target — every new case
  passes, and the full pre-existing suite's pass count is otherwise unchanged
  except for these new cases (no regression) — a fast, GPU-free check, not
  the full Phase 5 `ctest` pass.
- Live smoke test: `run_app_background` `GreatTamanaEditor.exe`,
  `gte_send_request GET /get_logs?limit=200` to confirm zero new
  `GTE_LOG_WARNING`/`GTE_LOG_ERROR` lines from `RenderFeatureCompositor`/
  `PluginRenderOperationRegistry`/`PluginRenderPassBuilderAdapter_v3` during
  a normal run with both demo plugins loaded (in particular: zero
  "rejected a PluginTextureHandle..."/"slot not covered"/"paramSize"
  warnings — if any appear, that is a real bug in this phase's own code,
  not noise to ignore), `GET /get_game_view` for the parity screenshot
  comparison (Step 3.5), `GET /render_graph` to confirm the `_v3` demo
  plugin's own declared pass names appear somewhere in the JSON response's
  `offscreen_regime.passes`/`present_regime.passes` arrays (note: these are
  the REAL, snake_case JSON keys `RenderGraphMetadata.cpp`'s own `to_json()`
  actually emits — `RenderGraphMetadata::offscreenRegime`/`presentRegime` are
  the C++ field names, confirmed by direct reading of
  `src/Renderer/RenderGraph/RenderGraphMetadata.h/.cpp`; do not search the
  JSON body for the camelCase spelling) (a quick sanity check
  ahead of PHASE4's own deeper diagnostics-integration verification), **and
  confirm each of those pass entries reports the expected `AfterEverything`-tier
  scheduling position relative to `"RenderOpaque"`/the Atmosphere composite
  pass** (a direct, cheap confirmation that Step 2.4's fix actually landed,
  not just that nothing crashed). `stop_app_background` when done.
- Confirm `_v2`'s own existing demo plugin(s) still work with ZERO
  behavior change (Step 3.2's own required pre-check, re-confirmed after
  every subsequent edit in this phase).

## Non-Goals for this phase

- `gte.builtin.blit_fullscreen`/the real 2-pass blur demo/actually
  DISPATCHING `gte.builtin.box_blur` — PHASE3. This phase registers
  `gte.builtin.box_blur` (Step 3.1) and fixes the generic sampler-resolution
  mechanism it will need (Step 2.5/3.4), but is not required to exercise it.
- `IPluginBlackboard`'s REAL implementation — PHASE4 (the static no-op
  stand-in from Step 3.3 is what PHASE4 replaces).
- Any Render Graph panel UI change — PHASE4.
- Plugin-registered custom operations — explicitly out of scope for the
  whole campaign (Locked Product Decision #4).

## Completion

Write `PHASE2_COMPLETION_REPORT.md`: exact registry entry table shipped
(id/kind/slots/opCode/maxParamBytes for all 4 built-in ops), the exact
pipeline-migration diff summary (including the two deleted
`ContributeRenderGraphPasses()` call sites and the new `Core.h` member
ordering), the exact `RenderPassEvent` value used for every `_v3`-declared
pass and why, the exact sampler-override mechanism shipped for
`"SceneColor"`, the exact generation-counter scheme implemented (confirming
it is a process-wide monotonic counter incremented once per adapter
construction, never per-handle/per-frame-reset), the parity-proof
screenshots/evidence, any `ask_questions` round needed, confirmation that
the blackboard stand-in is the static no-op singleton from Step 3.3, AND
confirmation that `RenderFeatureCompositor::m_v3OpDescriptorSets`/
`EnsureV3OpDescriptorSet()` (Step 2.6) is genuinely PERSISTENT (never
recreated per frame) — e.g. by letting the live smoke test run for at least
a few hundred frames (well past the fixed 256-set compute descriptor pool's
own capacity if the leak were still present) with zero
`vkAllocateDescriptorSets failed` error and zero descriptor-set-count growth
across repeated `GET /get_logs` polls.
`git_add` + `git_commit` ("editor-core-separation-9 PHASE2: operation
registry & adapter v3").
