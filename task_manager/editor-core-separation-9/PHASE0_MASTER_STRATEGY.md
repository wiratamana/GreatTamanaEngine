# editor-core-separation-9 — PHASE0 MASTER STRATEGY

**Branch:** `feature/editor-core-separation` (stay on it — do not create a new branch).

**Project root:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

**This folder:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\task_manager\editor-core-separation-9`

**Source design/requirements document (read this IN FULL before starting ANY phase):**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\render-pass\RENDERPASSBUILDER_FEATURE_AGNOSTIC_DESIGN_REQUIREMENTS_AND_INVESTIGATION_2026-09-25.md`
(referred to below as "the Design Doc"). It is a real, careful investigation
+ requirements list (R1–R32), plus PROPOSED/PSEUDOCODE interface shapes
(Part 4) and a phased outline (Part 5, P0–P4). **This master strategy is the
thing that actually turns Part 5 into real, buildable phases** — it CORRECTS
two concrete mechanisms the Design Doc's own pseudocode under-specifies (see
Step 2.6/2.7 below), and LOCKS every open question the Design Doc's own Part
6 left unresolved, via a real `ask_questions` round already completed with
the human product owner (see "Locked Product Decisions" below — do not
re-litigate these; challenge via `ask_questions` only if real code
contradicts one).

This document is the ORCHESTRATOR for this whole campaign. Every child phase
file (`PHASE1_*.md` .. `PHASE5_*.md`) in this same folder must be read
together with this file before starting work on that phase. Every phase
produces its own `PHASEn_COMPLETION_REPORT.md` in this same folder when done,
and ends with its own `git_add` + `git_commit`.

**Use `ask_questions` whenever a real design ambiguity comes up that this
master strategy or the relevant phase file does not already resolve — never
silently guess. This applies transitively: if a phase (or any task it
delegates) itself ever hands off further work, that further work must also be
told to use `ask_questions` for its own genuine ambiguities.**

**Use the engine's own internal logging (`GTE_LOG_INFO`/`GTE_LOG_WARNING`,
`src/Core/Logging.h`) plus `GET /get_logs` for all runtime diagnosis — never
`printf`/`std::cout`/`OutputDebugString`/raw C++ logging for anything new
added under `src/` or `plugins/`.**

---

## Step 1: The Goal (Where are we going?)

Today's real, shipped `plugins/gte_plugin_abi/IPluginRenderPassBuilder_v2.h`
is a **closed enumeration**: exactly 3 fixed C++ virtual methods
(`AddSolidFillPass`/`AddRadialVignettePass`/`AddColorGradePass`), each
hardcoded to one `opCode` inside one host-owned uber compute shader
(`src/Shaders/RenderFeatureOps.comp`). Adding a 4th effect requires an ABI
header edit + an adapter edit + a shader edit + an engine rebuild — no
third-party plugin author can ever add their own visual effect. Worse,
"compute shader writes a texture/buffer, a later pass reads it" — ordinary,
basic render-graph plumbing — is **completely impossible** for any plugin
today.

This campaign ships a NEW, ADDITIVE interface, **`IPluginRenderPassBuilder_v3`**
(`_v1`/`_v2` are NEVER touched, deprecated, or removed — Locked Design
Decision, matching `editor-core-separation-5`/`-6`'s own established
convention), that is genuinely feature-agnostic by construction:

1. **A real resource vocabulary** (`PluginTextureHandle`/`PluginBufferHandle`,
   physical-shape-only descriptors) a plugin uses to create transient
   textures/buffers, mirroring `rg::TextureHandle`/`rg::BufferHandle`
   (`src/Renderer/RenderGraph/RenderGraphTypes.h`) exactly, but ABI-safe.
2. **A real two-phase setup/execute pass-declaration model**
   (`IPluginPassSetupContext::ReadTexture`/`WriteTexture`/`ReadBuffer`/
   `WriteBuffer`/`WriteColorAttachment`), mirroring
   `rg::RenderGraphBuilder::PassBuilder` almost exactly — so "compute writes a
   texture, next pass reads it" and "compute writes a buffer, next pass reads
   it" become first-class, zero-new-ABI primitives, answering the user's
   original question directly.
3. **A growable, string-keyed, HOST-OWNED operation registry**
   (`PluginRenderOperationRegistry`) plugins call through
   (`IPluginCommandRecorder::Dispatch(opId, ...)`/`DrawFullscreenTriangle(opId, ...)`)
   instead of one fixed C++ method per effect — a brand-new operation becomes
   a HOST-SIDE CONTENT ADDITION (register one more entry), never an ABI
   change. This campaign proves it twice: once by porting the 3 existing
   `_v2` effects byte-for-byte, once by adding a genuinely NEW operation
   (`gte.builtin.box_blur`, reusing the ALREADY-EXISTING, ALREADY-SHIPPED
   `src/Shaders/BoxBlur.comp` shader — zero new GLSL needed for that one) with
   zero `IPluginRenderPassBuilder_v3` interface change.
4. **A real, committed, permanent demo plugin** proving the 2-pass
   "compute writes a texture, a later pass reads it" GPU blur, verified with a
   real, live, mathematically-checked pixel proof (mirrors
   `editor-core-separation-6` PHASE6's own precedent).
5. **A generic, ABI-safe, string-keyed cross-plugin blackboard**
   (`IPluginBlackboard::Publish`/`Fetch`), mirroring
   `rg::RenderPassBlackboard` (`RenderPipeline.h`) but ABI-safe (a fixed
   tagged-union value type, never `std::any`).
6. **Zero new compositing/ordering mechanism** — every `_v3` render feature
   is blended into the final frame through the EXACT SAME, already-shipped
   `RenderFeatureCompositor`/`RenderFeatureBlendMode`/per-plugin private
   offscreen target that `_v2` already uses (Locked Product Decision #6
   below) — this campaign only changes HOW a plugin's own private target gets
   filled, never how it gets composited afterward.
7. **Confirm (not rebuild) diagnostics/tooling integration** — a `_v3` pass
   declared via `IPluginPassSetupContext`/`IPluginCommandRecorder` is, under
   the hood, a REAL `rg::PassRecord` produced by the SAME
   `rg::RenderGraphBuilder::AddRenderPass()` chokepoint every internal engine
   pass already uses — it is therefore ALREADY generically visible in
   `rg::RenderGraphSnapshot`/`rg::RenderGraphMetadata`/the Editor's "Render
   Graph" panel/`GET /render_graph`, with **zero panel/JSON code change
   required** (see Step 2.7 below — this mirrors the `mrt-1` campaign's own
   "`RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp` needed ZERO changes"
   precedent, `AGENTS.md`'s "Multi-Render-Target (MRT)" section). PHASE4
   VERIFIES this live rather than building new plumbing for it.

**Explicitly OUT OF SCOPE for this campaign** (Design Doc's own Part 5, P4,
plus this campaign's own Locked Product Decisions):

- **No plugin-supplied shader bytecode/SPIR-V of any kind** — every operation
  in `PluginRenderOperationRegistry` is 100% host-authored/curated this
  campaign (Locked Product Decision #4). A plugin registering its OWN custom
  operation backed by its OWN shader is a separate, future,
  security-reviewed campaign (Design Doc's R17/P4) — not started here.
- No change to `_v1`/`_v2`/`RenderFeatureCompositor`'s existing blend-chain
  mechanics, `GtePluginRenderFeatureDescriptor`, or `RenderFeatureBlendMode`'s
  5 existing modes (a `Custom = 5` enumerator per Design Doc R21 is
  explicitly NOT added this campaign — no real consumer needs it yet).
- No persistence of any kind (nothing this campaign adds is saved to disk).
- No new HTTP endpoints (verification uses the ALREADY-EXISTING
  `GET /render_graph`/`GET /get_logs`/`GET /get_game_view`/`GET /get_texture`
  — see "Reference commands" below).
- No change to `RenderPassId`/`RenderPassEvent`/`RenderPassTagMask` or any
  other Core render-graph type — `_v3`'s adapter TRANSLATES into the
  already-existing, unmodified `rg::RenderGraphBuilder`/`rg::PassBuilder`
  surface, exactly like `RenderPipeline` already translates its own opaque
  types into the same, unmodified `AddRenderPass()` chokepoint
  (`AGENTS.md`'s "Render Pass System" section, render-pass-3 campaign).

## Step 2: The Situation (Where are we now?)

Confirmed by direct inspection of the REAL, CURRENT source (every path below
was actually opened and read during this campaign's planning — not guessed).

### 2.1 — The real, shipped `_v2` shape (what we are extending, never replacing)

- `plugins/gte_plugin_abi/IPluginRenderPassBuilder_v2.h` — exactly 3 pure
  virtual methods, confirmed verbatim.
- `src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.h/.cpp` — each of the 3
  methods fills a `RenderFeatureOpsPushConstants` (4×`vec4`, 64 bytes) and
  calls the ONE shared `RenderFeatureCompositor::DispatchOps()`.
- `src/Core/Plugins/RenderFeatureCompositor.h/.cpp` — owns `m_opsPipeline`/
  `m_opsDescriptorSetLayout` (one `ComputePipeline` built from
  `shaders/RenderFeatureOps.comp.spv`, ONE binding — `binding=0`, a single
  read-write storage image, per `DescriptorSetLayoutBuilder(m_device).AddStorageImage(0)`)
  and `m_blendPipeline`/`m_blendDescriptorSetLayout` (`RenderFeatureBlend.comp`,
  3 bindings: 2×`CombinedImageSampler` + 1×`StorageImage`). `DispatchOps()`
  declares exactly one `AddRenderPass(..., PassKind::Compute, ...)` per call,
  writing `privateTarget` via `WriteTexture(privateTarget, ComputeShaderWrite)`.
- `src/Shaders/RenderFeatureOps.comp` — the real uber shader: `opCode` 0/1/2
  selects Solid Fill/Radial Vignette/Color Grade via an `if/else if/else` on
  `pc.opCodeAndPad.x`, one binding (`binding=0, rgba8, image2D privateTarget`).
- `RenderFeatureCompositor::Entry` = `{ IRenderFeatureModule_v2* module;
  GtePluginRenderFeatureDescriptor descriptor; bool enabledOverride; }`,
  held in two vectors (`m_postComposite`/`m_preUi`), sorted by
  `descriptor.priority` ascending (stable, lexical tie-break on collision).
  `ContributeRenderGraphPasses()` rebuilds `combinedList` fresh EVERY FRAME,
  seeds the chain (a `Replace`-mode `DispatchBlend()` copy into a per-view
  "Seed" accumulator, closing the same-physical-image read+write hazard for
  the N==1 case), then for each surviving (enabled) entry: ensures a private
  target (`EnsurePrivateTargetState()`, one `RenderTexture` +
  `ComputeDescriptorSet` per interned `"<Plugin>_<View>_Private"` name),
  constructs a **fresh `PluginRenderPassBuilderAdapter_v2`** for that one
  entry, calls `entry.module->AddRenderGraphPasses(adapter)`, then
  `DispatchBlend()`s that plugin's private target against the running
  accumulator (or, for the LAST entry, directly into `resolved->target` —
  the real, final Game/Scene View color handle for this frame).
- `plugins/gte_plugin_abi/RenderFeatureDescriptor.h` — `RenderFeatureStage`
  (only `PostComposite`/`PreUI` wired), `RenderFeatureBlendMode` (5 modes,
  `Replace`=0), `GtePluginRenderFeatureDescriptor` (`name[64]`, `stage`,
  `priority`, `blendMode`) — **untouched by this campaign** (Locked Product
  Decision #6).
- `plugins/gte_plugin_abi/IRenderFeatureModule.h` — `IRenderFeatureModule_v1`
  (untouched) and `IRenderFeatureModule_v2` (`GetRenderFeatureDescriptor()` +
  `AddRenderGraphPasses(IPluginRenderPassBuilder_v2&)`) — this campaign adds a
  THIRD, sibling interface, `IRenderFeatureModule_v3`, to this same file
  (Locked Architecture Decision #2 below).

### 2.2 — The internal engine's own generic render-graph vocabulary (what `_v3` mirrors)

Confirmed by direct reading, not just the Design Doc's own summary:

- `src/Renderer/RenderGraph/RenderGraphTypes.h` — `TextureHandle`/
  `BufferHandle`/`VolumeTextureHandle` (3 distinct POD index+generation
  structs, deliberately never one shared template); `ResourceAccess`
  (`ColorAttachmentWrite`/`DepthStencilAttachmentReadWrite`/`ShaderRead`/
  `TransferSrc`/`TransferDst`/`ComputeShaderRead`/`ComputeShaderWrite`/
  `IndirectCommandRead`/`VertexBufferRead`/`VertexShaderStorageRead`, no
  `default:` case anywhere, ever); `TextureDesc`/`BufferDesc` (physical-shape
  ONLY — the file's own "standing rule" comment, ~line 259, plus its own
  documented past bug: a `debugName` field with pointer-identity
  `operator==` silently broke resource pooling — **do not repeat this on the
  plugin side**).
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` — `RenderGraphBuilder::PassBuilder`
  (`ReadTexture`/`WriteTexture`/`ReadBuffer`/`WriteBuffer`/
  `WriteColorAttachment`/`WriteDepthStencilAttachment`/`ReadVolumeTexture`/
  `WriteVolumeTexture`), `CreateTexture(name, desc)`/`CreateBuffer(name, desc)`
  (matched by desc VALUE EQUALITY against an existing pool — **`_v3`'s own
  `CreateTexture`/`CreateBuffer` reuse this exact same pool automatically by
  simply calling through to it — no new pooling mechanism needed**,
  satisfying Design Doc R31 for free), and the two-phase
  `AddPass(name, setup, execute)`/`AddRenderPass(name, kind, viewScope,
  category, setup, execute, drawKind, renderPassEvent, tags)` chokepoint.
- `src/Renderer/ComputeDescriptorSet.h` — `ComputeDescriptorWrite::
  StorageBuffer()`/`StorageImage()`/`CombinedImageSampler()` static factories
  + `ComputeDescriptorSet::Rewrite()` (one `vkUpdateDescriptorSets()` call) —
  the exact, already-existing, already-generic mechanism `_v3`'s command
  recorder reuses to bind an arbitrary plugin-declared handle to an
  arbitrary op's own descriptor slot, with zero new Vulkan machinery.
- `src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h` — fluent
  `AddStorageBuffer`/`AddStorageImage`/`AddCombinedImageSampler` + `Build()`
  — the exact, already-existing mechanism a NEW registry entry (e.g.
  `gte.builtin.box_blur`) uses to build ITS OWN descriptor set layout,
  independent of the shared uber-ops layout `_v2` already has.
- `src/Shaders/BoxBlur.comp` — a REAL, ALREADY-SHIPPED, ALREADY-TESTED
  (`ComputeBlurValidation.cpp`) generic NxN box blur: `binding=0` a
  `sampler2D sourceTexture` (read-only, `CombinedImageSampler`), `binding=1`
  a `rgba8 image2D destinationImage` (write-only `StorageImage`), a
  `(width, height)` push-constant pair, samples by NORMALIZED UV — meaning
  it works correctly even when `sourceTexture` and `destinationImage` are
  DIFFERENT resolutions (a natural downsample-blur). **This is this
  campaign's chosen 4th registry operation — zero new GLSL required** for
  proving R13's central claim ("a new operation is a host-side content
  addition, not an ABI change").
- `src/Renderer/RenderGraph/RenderGraphMetadata.h/.cpp` (`editor-core-separation-7`)
  — `RenderGraphMetadata::renderFeatures` already carries
  `RenderFeatureDebugEntry` (`enabled` field added by `editor-core-separation-8`
  PHASE2) straight into `GET /render_graph`'s JSON, and
  `RenderGraphPassMetadata` already generically resolves EVERY real
  `rg::PassRecord` (name/kind/category/reads/writes/timing) regardless of who
  declared it — a `_v3` plugin's own passes need ZERO new field here, since
  they are real `PassRecord`s (see Step 2.7 below).

### 2.3 — What the Design Doc's own pseudocode (Part 4.3/4.4) under-specifies — CORRECTED here

**Correction #1 (real, load-bearing):** Design Doc §4.3's
`PluginRenderOperationRegistry::ComputeOpInfo` only carries `{ id, opCode,
maxParamBytes }` — this silently assumes EVERY registered operation shares
ONE fixed descriptor-set layout/pipeline (the existing `RenderFeatureOps.comp`
uber shader). That is false the moment a genuinely different shader is
registered: `gte.builtin.box_blur` needs a `CombinedImageSampler` PLUS a
`StorageImage` (2 bindings, a different `VkDescriptorSetLayout`, a different
`ComputePipeline`, a different push-constant shape — `(width,height)` as
`uint32`s, not 4×`vec4`) — completely incompatible with the shared
`RenderFeatureOps.comp` layout. **The real registry entry must therefore
carry, per operation: its OWN `ComputePipeline*`/`VkPipeline` (or, for a
`DrawFullscreenTriangle`-family op, its OWN graphics `Pipeline*`), its OWN
`VkDescriptorSetLayout`, an ORDERED SLOT TABLE (each slot: a `VkDescriptorType`
— `StorageImage`/`CombinedImageSampler`/`StorageBuffer` — plus which kind of
plugin handle, texture or buffer, it expects), `opCode` (defaulted to 0,
ONLY meaningful for the shared "uber ops" family that legitimately reuses one
pipeline for several named operations), and `maxParamBytes`.** This is Locked
Architecture Decision #4 below (PHASE2's own central piece of work) — a real,
necessary generalization of the Design Doc's own pseudocode, not an
implementation detail to improvise later.

**Correction #2 (real, load-bearing):** Design Doc §4.6's
`IPluginRenderPassBuilder_v3` interface has no method for a plugin to obtain
a handle to ITS OWN per-(plugin,view) private compositing target — the exact
target `RenderFeatureCompositor` already allocates and later blends
(`EnsurePrivateTargetState()`). Without one, a `_v3` plugin has no way to
hand its finished per-frame image back to the compositor at all. **This
campaign adds `PluginTextureHandle GetPrivateOutputTarget()` to
`IPluginRenderPassBuilder_v3`** (confirmed via `ask_questions`, Locked
Product Decision #5) — the plugin's own LAST pass(es) must
`WriteColorAttachment`/`WriteTexture` into this handle THEMSELVES, exactly
like any other declared resource — no hidden/implicit "last write wins"
auto-detection, keeping the whole model uniformly generic (R15).

### 2.4 — Resource-kind vocabulary: curated subset, not a raw mirror

`PluginResourceAccess` (Design Doc §4.1) exposes only
`ColorAttachmentWrite`/`ShaderRead`/`ComputeShaderRead`/`ComputeShaderWrite` —
a plugin never needs `DepthStencilAttachmentReadWrite`/`TransferSrc`/
`TransferDst`/`IndirectCommandRead`/`VertexBufferRead`/
`VertexShaderStorageRead` (all internal-engine-only concepts today). The
`_v3` adapter (gte_core-internal) is the ONLY place that translates a
`PluginResourceAccess` value into a real `rg::ResourceAccess` value — a pure,
Tier-1-testable mapping function (PHASE1's own deliverable).

### 2.5 — Handle translation: how a raw `rg::TextureHandle` never crosses the ABI

`PluginTextureHandle`/`PluginBufferHandle` (Design Doc §4.1) are `{ index,
generation }` PODs — but they must NEVER be numerically identical to (or
interchangeable with) a real `rg::TextureHandle`/`rg::BufferHandle`, per
`PublicSurface.md`'s ABI rule (no raw internal type crosses the boundary, and
a plugin must never be able to fabricate a handle to an arbitrary host
resource by guessing — Design Doc R5). **The adapter (`gte_core`-internal)
owns a small, per-(plugin, view, frame)-scoped translation table** — a
`std::vector<rg::TextureHandle>`/`std::vector<rg::BufferHandle>` — where a
`PluginTextureHandle::index` is simply that vector's own index.
`CreateTexture()`/`CreateBuffer()`/`GetPrivateOutputTarget()`/
`TryGetNamedTexture()` all APPEND to (or, for the latter two, reuse a
lazily-cached single entry in) this same table; `PluginTextureHandle::generation`
is a small per-adapter-instance monotonic counter (defensive — catches a
plugin holding onto a handle across frames, since a fresh adapter/table is
constructed every frame exactly like `PluginRenderPassBuilderAdapter_v2`
already is). This table is the ONE mechanism every `IPluginPassSetupContext`/
`IPluginCommandRecorder` method resolves a plugin handle through before
calling the real `rg::RenderGraphBuilder::PassBuilder`/`rg::PassContext`
methods underneath.

### 2.6 — `_v3` reuses the EXACT SAME per-plugin compositing pipeline as `_v2` (Locked Product Decision #6)

`RenderFeatureCompositor::Entry` gains a second, alternate module pointer
(`IRenderFeatureModule_v3* moduleV3 = nullptr`, alongside the existing
`IRenderFeatureModule_v2* module`) — a loaded plugin implements EITHER `_v2`
OR `_v3` (never both; `OnPluginsLoaded()` queries both capabilities and
whichever one resolves wins, `_v3` checked first since it is now the
recommended path — Locked Product Decision #1). Both kinds of `Entry` are
sorted/collision-detected/enabled-toggled by the EXACT SAME, byte-for-byte
unchanged logic (`SortAndDetectCollisionsInStage()`, `SetFeatureEnabled()`,
`SetFeaturePriority()`) — none of that code needs to know which kind of
module it is looking at. `ContributeRenderGraphPasses()`'s per-entry loop
branches ONLY at the "which adapter do I construct" point: a `moduleV2`
entry builds a `PluginRenderPassBuilderAdapter_v2` (byte-for-byte unchanged);
a `moduleV3` entry builds a `PluginRenderPassBuilderAdapter_v3` (new), then
calls `entry.moduleV3->AddRenderGraphPasses(adapter)` — everything AFTER
that (the `DispatchBlend()` call reading `privateTarget`) is completely
unchanged either way, since both adapters ultimately fill the SAME
`EnsurePrivateTargetState()`-provided private `RenderTexture`.

### 2.7 — Diagnostics/tooling integration is ALREADY generic — PHASE4 mostly VERIFIES, does not build

A `_v3` plugin's pass, declared via `IPluginPassSetupContext`/
`IPluginCommandRecorder` and translated by the adapter into a real
`frame.builder.AddRenderPass(pluginDebugName, PassKind::Compute or Graphics,
...)` call, produces a REAL `rg::PassRecord` with a REAL, plugin-author-chosen
`debugName` — indistinguishable, to `RenderGraphCompiler`/`RenderGraphSnapshot`/
`RenderGraphMetadata`/the "Render Graph" panel/`GET /render_graph`, from any
internal engine pass. **This means R22 ("must be representable in the SAME
`RenderFeatureDebugEntry`-style POD... extend that same panel section, do not
build a second one") is ALREADY satisfied for individual PASSES by
construction — mirrors the `mrt-1` campaign's own real, confirmed finding
that `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp` needed ZERO changes
for a whole new resource-write shape (`AGENTS.md`'s "Multi-Render-Target
(MRT)" section).** What is NOT automatic: the "Render Graph" panel's
existing "Plugin Render Features" section (which lists `RenderFeatureDebugEntry`
rows — one per LOADED PLUGIN, not per pass) has no way to say "this plugin
is a `_v3` plugin, and here are its own N passes, nested" — PHASE4's real,
scoped work is confirming this live and, if genuinely useful, adding a small,
additive UI label (never new data plumbing) — see PHASE4 for the exact,
narrow scope.

## Step 3: The Plan (How do we get there?)

### Locked Product Decisions (resolved via a real `ask_questions` round with the human — do not re-litigate; challenge via `ask_questions` only if real code contradicts one)

1. **`IPluginRenderPassBuilder_v3` becomes the RECOMMENDED path for future
   plugin authors.** `_v2` is kept forever, unchanged, for backward
   compatibility — never deprecated, never removed — but this campaign's own
   new demo plugin, and `AGENTS.md`/`docs/conventions/plugin-architecture.md`
   (PHASE5), point future authors at `_v3` first.
2. **Dispatch group-count cap: 64×64×1 groups per single `Dispatch()` call**
   (with the engine's standard 16×16 local-size convention, this is up to
   ~1,048,576 threads per dispatch — generous, but bounded). Enforced
   host-side, loud `GTE_LOG_WARNING` + refuse-and-skip (never crash) if a
   plugin requests more — mirrors `PluginHost`'s own "clean skip, never
   crash" convention (`AGENTS.md`, Design Doc R27/R28).
3. **`TryGetNamedTexture()` exposes exactly ONE name in this campaign:
   `"SceneColor"`** — read-only, resolved against the SAME
   `resolved->target` (`Core::PluginRenderFeatureTargetInfo::target`)
   `RenderFeatureCompositor::ContributeRenderGraphPasses()` already imports
   every frame. No `"SceneDepth"` yet (no real consumer needs it — mirrors
   `editor-core-separation-6`'s own Locked Design Decision #3 reasoning for
   deferring exactly this kind of speculative exposure).
4. **`PluginRenderOperationRegistry` is 100% host-authored/curated this whole
   campaign.** No mechanism exists (or is added) for a plugin to register
   its own custom operation. Plugin-supplied shader code is explicitly
   deferred to a separate, future, security-reviewed campaign (Design Doc
   R17/P4) — not started, not scaffolded, not stubbed here.
5. **A new, explicit `PluginTextureHandle GetPrivateOutputTarget()` method**
   on `IPluginRenderPassBuilder_v3` — see Step 2.3, Correction #2. No
   implicit/auto-detected "last write wins" behavior anywhere in this
   campaign.
6. **`_v3` reuses the EXACT SAME per-plugin private-target + 5-mode blend
   pipeline `_v2` already has** (`RenderFeatureCompositor`,
   `RenderFeatureBlendMode`, unchanged) — see Step 2.6. `_v3` only changes
   HOW a plugin's own private target gets filled (a real, generic multi-pass
   resource graph instead of 3 fixed ops), never how it gets composited
   afterward.
7. **This campaign ships a real, permanent demo plugin** (`plugins/demo_render_feature_v3/`
   or similarly named — PHASE3 finalizes the exact folder name, mirroring
   `plugins/demo_render_feature/`'s own existing naming convention) proving
   the 2-pass "compute writes a texture, next pass reads it" GPU blur,
   committed to the repo, verified with a real, live, HTTP-driven,
   mathematically-checked pixel proof (mirrors `editor-core-separation-6`
   PHASE6's own precedent) — never a throwaway/manual-only test.

### Locked Architecture Decisions (resolved by direct code reading, not product choice — see Step 2 for the evidence)

8. **New ABI files, all under `plugins/gte_plugin_abi/` (zero real
   `gte_core`/`gte_editor` header ever included, per `PublicSurface.md`):**
   - `PluginRenderResource.h` — `PluginTextureHandle`/`PluginBufferHandle`,
     `PluginResourceAccess`, `PluginTextureDesc`/`PluginBufferDesc`.
   - `IPluginRenderPassBuilder_v3.h` — `IPluginPassSetupContext`,
     `IPluginCommandRecorder`, `PluginBlackboardValueKind`/
     `PluginBlackboardValue`, `IPluginBlackboard`, and the top-level
     `IPluginRenderPassBuilder_v3` interface itself.
   - `IRenderFeatureModule.h` gains `IRenderFeatureModule_v3` (appended,
     `IRenderFeatureModule_v1`/`_v2` completely untouched) —
     `GetRenderFeatureDescriptor() const` (returns the SAME
     `GtePluginRenderFeatureDescriptor` — unchanged struct) +
     `AddRenderGraphPasses(IPluginRenderPassBuilder_v3&)`, plus
     `kIRenderFeatureModule_v3_Name`.
   - `PublicSurface.md` gets a new "Added by later phases" bullet for this
     campaign (PHASE5), mirroring the existing `editor-core-separation-6`
     bullet's own format exactly.
9. **New `gte_core`-internal files, all under `src/Core/Plugins/`:**
   - `PluginRenderOperationRegistry.h/.cpp` — see Locked Architecture
     Decision #4 below for its exact shape.
   - `PluginRenderPassBuilderAdapter_v3.h/.cpp` — implements
     `IPluginRenderPassBuilder_v3`/`IPluginPassSetupContext`/
     `IPluginCommandRecorder`/`IPluginBlackboard` (the latter as a thin
     forwarder to a per-frame blackboard instance `RenderFeatureCompositor`
     owns — PHASE4), owns the handle-translation table (Step 2.5), never
     held past the end of one `ContributeRenderGraphPasses()` per-entry loop
     iteration (mirrors `PluginRenderPassBuilderAdapter_v2`'s own exact
     lifetime rule). **Its `Dispatch()`/`DrawFullscreenTriangle()` methods'
     OWN descriptor sets are NOT owned/cached by this per-frame adapter
     instance** — they are looked up/lazily created through a new persistent,
     never-recreated-per-frame accessor on `RenderFeatureCompositor` itself,
     `EnsureV3OpDescriptorSet()`, keyed by `(plugin, view, pass debugName)` —
     see PHASE2's own Step 2.6 for the full, concrete Vulkan-descriptor-pool-
     lifetime reasoning this requirement is built on (a real, load-bearing
     correctness/resource-exhaustion hazard found during this campaign's own
     double-check pass, not a stylistic preference).
   - `PluginRenderResourceTranslation.h/.cpp` — pure, Tier-1-testable
     `PluginResourceAccess` → `rg::ResourceAccess` and
     `PluginTextureDesc`/`PluginBufferDesc` → `rg::TextureDesc`/`rg::BufferDesc`
     mapping functions (PHASE1's own deliverable — no live `VkDevice`
     involved, mirrors `RenderGraphTypesTests.cpp`'s own established
     precedent for this kind of pure enum/struct mapping logic).
10. **`RenderFeatureCompositor::Entry` gains `IRenderFeatureModule_v3* moduleV3 = nullptr`**
    alongside the existing `IRenderFeatureModule_v2* module` (renamed
    `moduleV2` for clarity, or left as `module` with a comment — PHASE2's own
    call, either is fine as long as it is unambiguous) — see Step 2.6.
    `OnPluginsLoaded()` queries `kIRenderFeatureModule_v3_Name` FIRST, then
    falls back to `kIRenderFeatureModule_v2_Name` (a plugin declaring BOTH
    capabilities is a plugin-author error — loud `GTE_LOG_WARNING`, `_v3`
    wins, mirroring this codebase's general "loud, never silent" collision
    discipline).
11. **`PluginRenderOperationRegistry`'s real entry shape** (correcting Design
    Doc §4.3 — see Step 2.3, Correction #1): each registered operation
    carries its OWN pipeline (`ComputePipeline*` for a `Dispatch`-family op,
    a graphics `Pipeline*` for a `DrawFullscreenTriangle`-family op), its OWN
    `VkDescriptorSetLayout`, an ordered `std::vector<SlotDesc>` (`SlotDesc`
    = `{ VkDescriptorType type; bool isBuffer; }` — `BindTexture`/`BindBuffer`
    validate the caller passed the right kind for that slot, refusing
    (loud warning, skip) a mismatch), an `opCode` (default 0, meaningful only
    for entries sharing the pre-existing `RenderFeatureOps.comp` pipeline),
    and `maxParamBytes`. The registry OWNS (constructs once, lazily, on first
    use) every pipeline it registers, INCLUDING migrating ownership of the
    pre-existing `RenderFeatureOps.comp`/`RenderFeatureBlend.comp` pipelines
    OUT of `RenderFeatureCompositor` and INTO the registry (PHASE2) — `_v2`'s
    `DispatchOps()`/`DispatchBlend()` keep their exact own byte-for-byte
    behavior, just sourcing `m_opsPipeline`/`m_opsDescriptorSetLayout`
    through a small registry accessor instead of owning them directly (a
    pure refactor, zero observable behavior change, verified by re-running
    every existing `_v2` demo plugin unmodified before/after).
12. **Resource/parameter caps (Design Doc R6/R16/R27):** `maxParamBytes` per
    operation capped at 128 bytes (mirrors this engine's own 128-byte
    graphics push-constant convention, cited directly by the Design Doc's
    own R16); a single `_v3` plugin may declare at most 32
    `CreateTexture`/`CreateBuffer` calls per `AddRenderGraphPasses()`
    invocation (a defensive, generous resource-exhaustion guard — Design Doc
    R6); a single `CreateTexture` call is capped at 8192×8192 (a generous,
    typical GPU texture-dimension ceiling). Every cap violation is a loud
    `GTE_LOG_WARNING` naming the plugin + the offending call + the limit,
    then a clean skip (the offending resource/pass/dispatch is simply not
    created/recorded) — never a crash, mirroring `PluginHost.cpp`'s own
    established discipline.
13. **`_v3`'s cross-plugin blackboard is owned by `RenderFeatureCompositor`**,
    ONE instance, cleared at the START of every `ContributeRenderGraphPasses()`
    call (mirrors `rg::RenderPassBlackboard`'s own per-frame lifetime
    exactly) — `PluginRenderPassBuilderAdapter_v3::Blackboard()` returns a
    reference to it. Shared across BOTH `_v2` and `_v3` entries this frame in
    principle (the storage itself does not care which ABI version published
    into it), though only `_v3` exposes it to plugins this campaign (`_v2`'s
    own 3-method interface has no `Blackboard()` accessor and is never given
    one — an ADDITIVE-only campaign).

### Phase map

- **`PHASE1_RESOURCE_VOCABULARY_AND_ABI_FOUNDATION.md`** — the new ABI
  headers (`PluginRenderResource.h`, `IPluginRenderPassBuilder_v3.h`'s full
  interface declarations, `IRenderFeatureModule_v3`), PLUS the pure,
  Tier-1-tested `PluginRenderResourceTranslation.h/.cpp` mapping functions.
  Zero behavior change — nothing calls or implements any of this yet.
- **`PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md`** — `PluginRenderOperationRegistry`
  (Locked Architecture Decision #11), the `RenderFeatureOps.comp`/
  `RenderFeatureBlend.comp` pipeline-ownership migration (zero `_v2` behavior
  change, re-verified), `gte.builtin.box_blur` registered as a genuinely NEW
  operation, `PluginRenderPassBuilderAdapter_v3` (full resource creation +
  pass declaration + `Dispatch`/`DrawFullscreenTriangle` + handle-translation
  table + `GetPrivateOutputTarget()`/`TryGetNamedTexture("SceneColor")`),
  `RenderFeatureCompositor`'s `moduleV3` wiring. Verified by a demo plugin
  that reimplements `_v2`'s exact 3 effects via `_v3`'s generic `Dispatch()`,
  pixel-diffed against the existing `_v2` demo plugin's own output (proving
  byte-for-byte equivalence).
- **`PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md`** — the real,
  permanent 2-pass GPU blur demo plugin (Locked Product Decision #7): a
  compute pass reads `"SceneColor"`, writes a NEW transient half-res texture
  via `gte.builtin.box_blur`; a graphics pass reads that texture and draws it
  into `GetPrivateOutputTarget()` via one brand-new, tiny, additive registry
  operation (`gte.builtin.blit_fullscreen` — a minimal fullscreen-triangle
  passthrough fragment shader, mirroring `AtmosphereSkyBackground`'s own
  vertex-buffer-free 3-vertex draw convention) — this is the phase that
  answers the user's original question for real, end-to-end, with a live,
  mathematically-verified pixel proof.
- **`PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md`** — `IPluginBlackboard`/
  `PluginBlackboardValue` (Locked Architecture Decision #13), PLUS the
  diagnostics verification described in Step 2.7 (confirm `_v3` passes are
  already generically visible via `GET /render_graph`/the "Render Graph"
  panel; add a small, additive UI label ONLY if genuinely useful, never new
  data plumbing).
- **`PHASE5_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`** — updates
  `AGENTS.md`'s "Plugin Architecture" section,
  `docs/conventions/plugin-architecture.md`, `PublicSurface.md`, runs the ONE
  full clean build + full `ctest` pass + final live HTTP smoke test this
  whole campaign is allowed to run, and writes `CAMPAIGN_COMPLETION_REPORT.md`.

### Campaign-wide rules every phase must follow ("Workflow Rule 1" – "Workflow Rule 9" below — later phase files cite these exact numbers)

1. **No full build, no full regression test, for Phases 1–4.** Only a fast,
   targeted, incremental build/compile check (`cmake --build build`) — this
   machine's full clean build and full `ctest` pass are slow and reserved
   for Phase 5 alone.
2. **Use the engine's own internal logging + `GET /get_logs`** for anything
   inside `GreatTamanaEditor.exe`'s own run — never
   `printf`/`std::cout`/`OutputDebugString`/raw C++ logging for anything new
   added under `src/`/`plugins/`. Use `GTE_LOG_INFO`/`GTE_LOG_WARNING`
   (`src/Core/Logging.h`), exactly like every existing call site this
   campaign touches already does.
3. **Use the engine's own network debugging endpoints for visual/behavioral
   verification** — `run_app_background` the real `GreatTamanaEditor.exe`,
   then `gte_send_request` against `http://127.0.0.1:8080`:
   `GET /get_logs?limit=N`, `GET /list_tabs`,
   `GET /activate_tab?name=Render%20Graph`, `GET /get_swapchain`,
   `GET /get_game_view`, `GET /get_texture?name=...`, `GET /render_graph`,
   `POST /clear_logs`. Always `stop_app_background` the PID when finished
   with a check.
4. **Every phase writes its own `PHASEn_COMPLETION_REPORT.md`** in this same
   folder once its own verification passes, documenting exactly what
   changed, any real deviation from this phase's own plan, and the exact
   verification evidence gathered.
5. **`git_add` + `git_commit` at the end of every phase** — one commit per
   phase, message referencing the phase number and its one-line summary.
6. **If a phase hits a genuine design ambiguity not already resolved by
   this master strategy or its own phase file, use `ask_questions` before
   guessing.** This applies transitively to anything a phase itself
   delegates further.
7. **Implementation phases (1–5) must NOT call `delegate_task` themselves.**
   Only the double-check/orchestration step of this campaign (run
   separately, after all phase files are scaffolded and reviewed) is
   allowed to use `delegate_task` to hand off each phase's real
   implementation. The one exception: Phase 5's own mandatory final full
   regression pass may invoke `delegate_task` ONLY if that regression pass
   surfaces a real, newly-broken, unexplained test failure that needs a
   dedicated fix — never for any other reason, and never by any phase other
   than Phase 5.
8. **Every phase's code must follow `AGENTS.md`'s existing coding
   guidelines** (Clean Architecture, RAII, `namespace gte`) and
   `plugins/gte_plugin_abi/PublicSurface.md`'s ABI-boundary rule — never
   invent a new pattern where an existing file already shows the exact shape
   to copy (`PluginRenderPassBuilderAdapter_v2.h/.cpp`,
   `RenderFeatureCompositor.h/.cpp`, `RenderGraphBuilder.h`'s own
   `PassBuilder` in particular — read them in full before PHASE2).
9. **Run `git_status` at the very START of every phase** — confirm the
   branch still reads `feature/editor-core-separation` and the working tree
   is either clean or contains only the exact diff the immediately-prior
   phase already committed — **and run it AGAIN immediately before that
   phase's own final commit**, to confirm the about-to-be-staged diff
   touches ONLY the files this phase's own plan says it may touch.

### What this campaign explicitly does NOT do (Non-Goals)

- No plugin-supplied shader bytecode/SPIR-V (deferred, Design Doc P4).
- No plugin-registered custom operations in `PluginRenderOperationRegistry`
  (Locked Product Decision #4).
- No change to `_v1`/`_v2`/`GtePluginRenderFeatureDescriptor`/
  `RenderFeatureBlendMode`'s 5 existing modes/`RenderFeatureCompositor`'s
  ordering-and-blending mechanics.
- No persistence of any kind.
- No new HTTP endpoints.
- No `"SceneDepth"` or any other named resource beyond `"SceneColor"` this
  campaign.
- No change to any OTHER Editor panel, any OTHER network route, or any
  OTHER `_v1`/`_v2` plugin capability surface.

### Reference commands

- Main build tree (existing, already configured): `cmake --build build`
  (working directory `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`,
  Ninja/MinGW) — use this for every phase's own incremental build/compile
  check; a Ninja incremental build only recompiles what actually changed.
- Full regression test (Phase 5 ONLY): `cd /d
  C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
  --output-on-failure`.
- Run the live Editor for HTTP-driven smoke checks:
  `run_app_background` on `build\GreatTamanaEditor.exe`, then
  `gte_send_request` against `http://127.0.0.1:8080` (default port). Always
  `stop_app_background` the PID when finished.

### Every child phase file in this folder

1. `PHASE1_RESOURCE_VOCABULARY_AND_ABI_FOUNDATION.md`
2. `PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md`
3. `PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md`
4. `PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md`
5. `PHASE5_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`

Read this master file FIRST, then the source Design Doc (for full background
context — it is accurate on the "why", this master strategy is the "how"),
then the one phase file you are working on, then (if it exists yet) the
previous phase's own `PHASEn_COMPLETION_REPORT.md` for continuity clues,
before writing any code.
