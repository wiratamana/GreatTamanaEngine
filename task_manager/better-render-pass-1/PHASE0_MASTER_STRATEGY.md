# better-render-pass-1 — Render Pass Authoring Rehaul (Milestone 1) — MASTER STRATEGY

**Orchestrator document.** Every child phase (`PHASE1`..`PHASE10`) below reports back here
conceptually — read this file FIRST, always, before opening any child phase file. This campaign
implements **Milestone 1** of
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\better-render-pass-1\Investigation_Report_Render_Pass_Rehaul.txt`
("Case of the Copy-Pasted Red Tint") — Decision D2's own fixed sequencing. Milestone 2 (a real
bindless resource system, R3) and the adjacent Atmosphere dirty-flag optimization (R8) are
**explicitly, deliberately deferred** to a follow-up campaign (`better-render-pass-2`, not yet
created) — see "What This Campaign Does NOT Do" below for why, and for exactly what is left for
that future campaign to pick up.

Read the source investigation document in full before touching any code. Every "Finding N" /
"Requirement RN" / "Decision DN" reference below refers to that document.

---

## Step 1 — The Goal (Where are we going?)

Today, registering one simple screen effect requires six moving parts (Finding 1), a real,
live copy-paste bug already proves this ceremony breeds mistakes (Finding 2 —
`YellowTintScreenPass.cpp`/`IterEScreenPass.cpp` both draw red), there is no engine-owned command
buffer (Finding 3 — `PassContext::cmd` is a raw `VkCommandBuffer`), compute pipeline creation
requires hand-built descriptor-set layouts and push-constant ranges kept in sync with GLSL purely
by comment convention (Findings 4/6/7), every pipeline repeats the same lazy-init boilerplate
(Finding 8), `TextureDesc` cannot express `usage` (Finding 9), and the scaffolding tool's
auto-wire idempotency guard has one confirmed, live, unfixed gap (Finding 10(b)).

When this campaign is done:

1. A brand-new engine-owned `gte::rg::CommandBuffer` exists, wrapping `PassContext::cmd`, with a
   `BindComputePipeline`/`SetPushConstants<T>`/`Dispatch`/`DispatchOverSize`/`Draw`/`DrawIndexed`/
   `BindDescriptorSet` method set (R1) — additive, never breaking any existing pass body.
2. `GpuResourceFactory::CreateComputePipeline(path)` works with **only a path** in the common
   case — no hand-built `VkDescriptorSetLayout`, no hand-built `VkPushConstantRange` — powered by
   real SPIR-V reflection (SPIRV-Reflect, Decision D4) (R2). Every real, existing compute-pipeline
   call site in the engine (CullingPipelines, GpuSkinningPipelines x2, all 6 Atmosphere passes,
   ComputeBlurValidation, GBufferValidation, FrameDebuggerPreviewProcessing,
   VolumeTexturePreviewRenderer, PluginRenderOperationRegistry x3 — ~14 files, ~26 call sites) is
   migrated onto it — no two competing conventions left live indefinitely.
3. `cmd.SetPushConstants(myStruct)` asserts, in debug builds, that `sizeof(myStruct)` matches the
   pipeline's own reflected push-constant size (R4).
4. `TextureDesc` gains a genuine, equality-compared `usage` bitmask field
   (`TextureUsage::Sampled | TextureUsage::Storage | TextureUsage::TransferSrc | ::TransferDst`),
   with `RenderGraphResourcePool` audited so two textures differing only in `usage` are never
   silently pooled together — and, as a direct, concrete unlock, a genuinely TRANSIENT/pooled
   `RWTexture` becomes possible for the first time (R5).
5. `ScreenPassAutoWire.cpp`'s idempotency guard also checks whether the matching forward
   declaration is already present (not just whether the call itself is active) — closing Finding
   10(b) for good (R6) — and a new, additive `Core::AddScreenPostProcessPass()` convenience API
   cuts the common "draw one clear/tint over the screen" case down to one direct function call a
   game author can hand-write inline, with automatic stage/priority, while
   `Core::RegisterProjectRenderFeature()` stays exactly as-is for advanced/managed-compositing
   users (Decision D3, R7).
6. Nothing under `RenderGraphCompiler`/`RenderGraphBarrierPlanner`/`RenderGraphSnapshot` is
   touched (Decision D1) — that machinery is already correct and already O(P+E) (Finding 14).

---

## Step 2 — The Situation (Where are we now? — concrete evidence, already read from source)

Every fact below was confirmed by directly reading the live source tree before this document was
written (not guessed from the investigation report alone).

### 2.1 What already exists and must not be reinvented

- `RenderGraphBuilder::AddPass(name, setup, execute)` / `AddComputePass(...)` / `AddRenderPass(...)`
  (`src/Renderer/RenderGraph/RenderGraphBuilder.h`) already matches the client's own rough
  two-lambda sketch (Finding 13) — not reinvented by this campaign.
- `ComputePipeline` (`src/Renderer/ComputePipeline.h/.cpp`) is an RAII wrapper around
  `VkPipeline`/`VkPipelineLayout`, constructed via
  `GpuResourceFactory::CreateComputePipeline(shaderSpirvPath, descriptorSetLayouts = {},
  pushConstantRange = std::nullopt)`. This signature is UNCHANGED by this campaign — the plan is a
  new, ADDITIVE overload/internal path that fills those two optional arguments in via reflection
  when the caller omits them, never removing the manual override (the "escape hatch" R2 requires).
- `ComputeDispatch.h`'s `ComputeGroupCount()`/`ComputeGroupCount3D()` (pure ceiling-division math,
  already Tier-1-tested) stays exactly as-is — `CommandBuffer::DispatchOverSize()` (R1) is a thin
  wrapper calling these, never a reimplementation.
- `DescriptorSetLayoutBuilder` (`src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h/.cpp`) stays
  exactly as-is too — it becomes an internal implementation detail the new reflection-driven path
  calls into (reflection decides WHICH `AddStorageBuffer`/`AddStorageImage`/
  `AddCombinedImageSampler` calls to make; the builder itself is unchanged), and remains directly
  usable by any exotic manual override.
- `RenderGraphResourcePool`/`RenderGraphCompiler`/`RenderGraphBarrierPlanner` — genuinely
  untouched by this whole campaign except the one, narrow, audited addition described in PHASE8.

### 2.2 Every real `CreateComputePipeline()` call site today (confirmed via `search_in_dir`)

| # | File | Pipeline(s) | Notes |
|---|------|-------------|-------|
| 1 | `src/Renderer/Culling/CullingPipelines.cpp` | 1 (`FrustumCull.comp`) | render-pass-5, PHASE3. Production, always-on Game View path. |
| 2 | `src/Renderer/GpuSkinning/GpuSkinningPipelines.cpp` | 2 (`SkinVerticesPositionNormal.comp`, `SkinVerticesPositionNormalUv.comp`) | Production. |
| 3 | `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` | 6 (Transmittance/MultiScattering/SkyView/AerialPerspectiveVolume/AerialPerspectiveComposite/AerialPerspectiveVolumeDebugSlice) | Production (5 of 6 run every real frame; the Debug Slice one only fires from an Editor button). |
| 4 | `src/Editor/ComputeBlurValidation.cpp` | 1 (`BoxBlur.comp`) | Editor-only, Scene-View-only debug validation pass. |
| 5 | `src/Editor/GBufferValidation.cpp` | 1 (`GBufferCopy.comp`) | Editor-only, Scene-View-only debug validation pass. |
| 6 | `src/Editor/FrameDebuggerPreviewProcessing.cpp` | 1 (`FrameDebuggerPreview.comp`) | Editor-only, Frame Debugger preview channel/levels compositing. |
| 7 | `src/Renderer/VolumeTexturePreviewRenderer.cpp` | 1 (`VolumeTexturePreview.comp`) | Editor-only, Atmosphere volume-texture Inspector preview. |
| 8 | `src/Core/Plugins/PluginRenderOperationRegistry.cpp` | 3 (`BoxBlur.comp`, `RenderFeatureOps.comp`, `RenderFeatureBlend.comp`) | Host-side implementation backing `gte_plugin_abi`'s `IPluginRenderPassBuilder_v3`/`RenderFeatureCompositor` — an internal implementation detail; the plugin ABI surface itself is not touched. |

Total: 8 files, 16 distinct `ComputePipeline` instances, ~26 `CreateComputePipeline(`/
`DescriptorSetLayoutBuilder` call-site occurrences. Every one of these is a migration target for
R2 — see PHASE4/5/6/7 for the batched migration plan.

### 2.3 The exact shape of today's hand-sync pain (one concrete example, `CullingPipelines.cpp`)

```cpp
VkPushConstantRange pushConstantRange{};
pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
pushConstantRange.offset = 0;
pushConstantRange.size = kCullingPushConstantSize; // hand-restated constant, must match .comp by hand

DescriptorSetLayoutBuilder layoutBuilder(m_device);
m_layout = layoutBuilder.AddStorageBuffer(/*binding=*/0)
               .AddStorageBuffer(/*binding=*/1)
               .AddStorageBuffer(/*binding=*/2)
               .Build();

m_pipeline.emplace(renderer.CreateComputePipeline(
    "shaders/FrustumCull.comp.spv", std::vector<VkDescriptorSetLayout>{ m_layout }, pushConstantRange));
```

After this campaign, the equivalent call becomes (binding numbers/types/push-constant size all
read directly from the compiled SPIR-V's own reflection data):

```cpp
m_pipeline.emplace(renderer.CreateComputePipeline("shaders/FrustumCull.comp.spv"));
m_layout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0); // still available, for building/rewriting a ComputeDescriptorSet
```

### 2.4 `PassContext` today (`src/Renderer/RenderGraph/RenderGraph.h`, ~line 667-818)

`PassContext::cmd` is a plain `VkCommandBuffer cmd = VK_NULL_HANDLE;`. A real compute pass's
`execute` callback today must call, in order: `renderer.BeginGraphPassRecording(ctx.cmd,
ctx.recordDraw);` → `renderer.Dispatch(pipeline, descriptorSet, &pushConstants,
sizeof(pushConstants), gx, gy, gz);` → `renderer.EndGraphPassRecording();` (three separate
objects). `PassContext` also already carries `resolveTexture()`/`resolveBuffer()`/
`resolveVolumeTexture()` (non-owning pointers into `RenderGraph`'s own physical-resource vectors)
and `recordDraw`/`recordIndirectDraw` (small callable structs, not `std::function`s — see that
struct's own extensive doc comment on why, render-pass-6 PHASE3). `RenderGraph` itself already
holds a non-owning `Renderer* m_renderer` member (added by editor-core-separation-26 PHASE6 for
the `Blit` pass kind) — `BuildPassContext()` can thread this same pointer into the new
`CommandBuffer` with zero new plumbing needed.

`Renderer::Dispatch()`/`Renderer::Submit()`/`Renderer::BeginGraphPassRecording()`/
`Renderer::EndGraphPassRecording()` (`src/Renderer/Renderer.h`, ~lines 201, 484, 682) are the real
methods a `CommandBuffer` wraps — their signatures are UNCHANGED by this campaign; `CommandBuffer`
is a thin, pass-author-facing façade over them, constructed fresh once per pass (mirrors
`PassContext` itself, which is already rebuilt once per pass by `RenderGraph::BuildPassContext()`).

### 2.5 `TextureDesc` today (`RenderGraphTypes.h`, ~line 278-291)

```cpp
struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool hasDepth = false;
    friend bool operator==(const TextureDesc&, const TextureDesc&) noexcept = default;
};
```

No `usage` field exists. `RenderGraphResourcePool::AcquireTexture()` always calls
`Renderer::CreateRenderTexture()` with `allowStorageImageAccess` defaulted to `false` — there is
no way today for a CreateTexture()-declared (transient/pooled) resource to ever become a storage
image (`COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`, Section A.6/C.2 — an
already-documented, long-standing gap). PHASE8 closes this for real.

### 2.6 The confirmed, live idempotency bug (Finding 10(b), `src/Editor/ScreenPassAutoWire.cpp`)

`TryAutoWireRegisterCall()`'s idempotency guard (lines ~171-184) only scans non-comment lines for
`registerFunctionName + "(core);"` (the ACTIVE CALL). It never separately checks whether
`"void " + registerFunctionName + "(gte::Core& core);"` (the FORWARD DECLARATION) already exists
among non-comment lines. If a human comments out only the call line (a workflow this file's own
header comment explicitly anticipates: *"Step 3 (idempotency guard): scan every NON-COMMENT line
for an already active call. A commented-out call... must NOT be mistaken for still-active"*) and
the scaffolding tool is re-triggered for that same pass name, both insertion indices are computed
again and BOTH a new forward declaration AND a new call line are inserted — producing a genuine,
confirmed duplicate forward declaration. PHASE9 fixes this.

### 2.7 Decision D3's existing substrate (`Core::RegisterProjectRenderFeature()`, `Core.cpp` ~line 382)

```cpp
bool Core::RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
    RenderFeatureBlendMode blendMode, std::int32_t priority, ProjectRenderFeatureCallback callback);
```

`ProjectRenderFeatureCallback` (`src/Core/Plugins/ProjectRenderFeatureCallback.h`) is already
`std::function<void(rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D)>` — already extremely
close to the client's own rough example. The "ceremony" Finding 1 identifies is the four required
arguments (`stage`, `blendMode`, `priority`, plus a SEPARATE scaffolded file + auto-wired call).
`ComputeNextScreenPassPriority()` (`ScreenPassPriorityAssignment.h`) computes priority by scanning
sibling `*ScreenPass.cpp` files on disk — a SCAFFOLD-TIME mechanism that cannot be reused by a
hand-written call that never goes through the scaffolding tool at all. PHASE9's new
`Core::AddScreenPostProcessPass()` needs its own, independent, RUNTIME priority auto-assignment
(a simple incrementing counter local to `Core`/`RenderFeatureCompositor`) — file-scanning is not
applicable here and must not be reused/duplicated.

---

## Step 3 — The Plan (child phases)

Ten implementation phases, in strict dependency order:

- **`PHASE1_SPIRV_REFLECT_DEPENDENCY_AND_SHADER_REFLECTION_CORE.md`** — SPIRV-Reflect (Decision D4)
  is already vendored (`third_party/spirv_reflect/`, via `cmake/FetchSpirvReflect.cmake`, mirroring
  `cmake/FetchVulkan.cmake`'s own `volk` target shape) and already linked into `gte_core` — a
  satisfied precondition this phase inherits rather than performs. This phase's own job: new
  `src/Renderer/Vulkan/ShaderReflection.h/.cpp`, a pure, Tier-1-testable function that reads a
  compiled `.comp.spv` file and returns every descriptor binding (set/binding/type/count), the
  push-constant block's offset/size, and the declared `layout(local_size_x/y/z)` work-group size,
  plus committing this module together with the already-made-but-not-yet-committed dependency
  changes in one coherent commit. Zero engine call sites changed yet.
- **`PHASE2_REFLECTION_BASED_COMPUTE_PIPELINE_CREATION.md`** — `ComputePipeline` gains a new,
  additive constructor overload/internal path (and `GpuResourceFactory::CreateComputePipeline()`/
  `Renderer::CreateComputePipeline()` gain a matching convenience overload) that builds its
  descriptor-set layout(s) and push-constant range FROM reflection when the caller omits them,
  and exposes `ReflectedDescriptorSetLayout(set)`/`PushConstantSize()`/`LocalGroupSize()`
  accessors. The existing, fully-manual constructor/overload stays, byte-for-byte, as the
  documented escape hatch. Depends on PHASE1.
- **`PHASE3_ENGINE_COMMAND_BUFFER_AND_TYPE_SAFE_PUSH_CONSTANTS.md`** — New
  `gte::rg::CommandBuffer` (R1) + `PassContext` gains a way to obtain one, built from
  `RenderGraph::BuildPassContext()`'s already-available `Renderer*`. Templated
  `SetPushConstants<T>()` (R4) debug-asserts `sizeof(T)` against the bound pipeline's
  `PushConstantSize()` (PHASE2). `DispatchOverSize()` uses the bound pipeline's own
  `LocalGroupSize()` (PHASE2) + `ComputeDispatch.h`'s existing ceiling-division math. Pure
  infrastructure — zero real pass migrated yet, zero behavior change. Depends on PHASE2.
- **`PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md`** — First real migration batch:
  `CullingPipelines` + `GpuSkinningPipelines` (both production, both already GPU-verified live
  systems) onto PHASE2's reflection path + PHASE3's `CommandBuffer`. Depends on PHASE3.
- **`PHASE5_MIGRATE_ATMOSPHERE_COMPUTE_PASSES.md`** — Second batch: `AtmosphereLutRenderer`'s six
  passes. Depends on PHASE4 (reuses its exact migration pattern).
- **`PHASE6_MIGRATE_EDITOR_DEBUG_COMPUTE_TOOLING.md`** — Third batch: `ComputeBlurValidation`,
  `GBufferValidation`, `FrameDebuggerPreviewProcessing`, `VolumeTexturePreviewRenderer`. Depends
  on PHASE4.
- **`PHASE7_MIGRATE_PLUGIN_RENDER_OPERATION_REGISTRY.md`** — Fourth, final migration batch:
  `PluginRenderOperationRegistry`'s three host-side pipelines, PLUS a full-repository grep audit
  confirming zero remaining production `DescriptorSetLayoutBuilder`/manual-`VkPushConstantRange`
  call site exists outside this campaign's own new reflection internals and documented exotic
  escape-hatch cases. Depends on PHASE4/5/6 (must run last among the migration batches).
- **`PHASE8_TEXTUREDESC_USAGE_FIELD_AND_RESOURCE_POOL_AUDIT.md`** — R5: `TextureUsage` bitmask
  enum, `TextureDesc::usage` field, `RenderGraphResourcePool::AcquireTexture()` audit + real
  storage-image opt-in for a pooled/transient texture for the first time. Independent of PHASE4-7
  (touches `RenderGraphTypes.h`/`RenderGraphResourcePool.cpp` only) — may run any time after
  PHASE3, sequenced here to keep the compute-pipeline migration batches uninterrupted.
- **`PHASE9_SCAFFOLDING_FIX_AND_SCREEN_POST_PROCESS_CONVENIENCE_API.md`** — R6 + Decision D3:
  fix `ScreenPassAutoWire.cpp`'s idempotency bug; new `Core::AddScreenPostProcessPass()`
  convenience API; update the Editor's scaffold template. Independent of PHASE1-8 (touches
  `src/Editor/`+`src/Core/` only) — may run any time, sequenced last among the "new capability"
  phases so its own regression test runs against an otherwise-stable tree.
- **`PHASE10_FINAL_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md`** — The ONLY phase allowed to run a
  full clean build + full `ctest` regression pass. Live, HTTP-driven Frame Debugger + Game/Scene
  View smoke test confirming zero visual regression across every migrated pass. Live
  reproduction of the Finding 10(b) fix against the real `Projects/ScreenPassAutoWireProbe/`
  fixture. `AGENTS.md` update. `CAMPAIGN_COMPLETION_REPORT.md`.

### Sequencing rationale

PHASE1→2→3 is a strict, linear infrastructure chain (reflection data → reflection-driven pipeline
construction → a `CommandBuffer` that depends on both pipeline accessors). PHASE4 must be the
first REAL migration because `CullingPipelines`/`GpuSkinningPipelines` are the smallest, most
recently-written, most structurally-similar pair (both already mirror each other almost exactly —
see `CullingPipelines.h`'s own header comment: *"Mirrors `GpuSkinningPipelines.h` EXACTLY"*),
making it the safest place to prove the whole migration pattern once before repeating it at scale.
PHASE5 (Atmosphere, 6 passes in one file) and PHASE6 (4 separate Editor-only files) can happen in
either order relative to each other (both only depend on PHASE4's proven pattern) — this document
fixes PHASE5 before PHASE6 purely because Atmosphere is production-critical and should be
proven working before spending time on Editor-only debug tooling. PHASE7 must be LAST among the
migration batches because its own audit step needs every other migration already landed to report
an accurate "zero remaining call sites" result. PHASE8 is independent and side-slotted. PHASE9 is
independent and side-slotted, placed last among the "new capability" work so it lands against an
otherwise-stable tree. PHASE10 is last by definition (Note 4's own "full build/test only in the
final phase" rule).

### What This Campaign Does NOT Do (explicit non-goals, mirrors Decision D2)

- **R3 (a real bindless resource system) — Milestone 2, explicitly deferred to a future
  `better-render-pass-2` campaign.** This is the single biggest, most expensive item in the whole
  investigation (Decision D2's own words) — its real blast radius is EVERY `ComputeDescriptorSet`/
  `.Rewrite()` call site this campaign's own migration touches (CullingPipelines,
  AtmosphereLutRenderer's 6 passes, ComputeBlurValidation, GBufferValidation, at minimum) plus a
  brand-new global descriptor-array design this campaign does not attempt. Nothing in this
  campaign blocks it — PHASE2's reflection work in fact makes a FUTURE bindless migration easier
  (binding tables are now machine-derived, not hand-typed), but building the bindless array itself
  is explicitly out of scope here.
- **R8 (Atmosphere dirty-flag/change-detection optimization)** — also explicitly deferred,
  alongside R3, to the same future campaign; it is an independent, separately-schedulable
  performance item (Finding 12) that touches the SAME four Atmosphere passes PHASE5 migrates here,
  but for a completely orthogonal reason (compute/upload skipping, not reflection/CommandBuffer
  ergonomics) — mixing the two changes into one phase would make either one harder to review and
  revert independently if something goes wrong.
- **No change to `RenderGraphCompiler`/`RenderGraphBarrierPlanner`/`RenderGraphSnapshot`** —
  Decision D1, restated: that machinery is already O(P+E) (Finding 14) and independently tested;
  there is no design-flaw or performance reason tied to it.
- **No change to `RenderPassEvent`/`RenderPassCategory`/`RenderPassDrawKind`/`ViewScope`/
  `RenderPassTagMask`** — R7's own explicit non-regression requirement; these stay reachable
  through the existing full/low-level API for any pass that genuinely needs them.
- **No deletion of `Core::RegisterProjectRenderFeature()`/the stage/blend-mode/priority
  machinery** — Decision D3's own explicit instruction: it remains available as an opt-in layer
  for the minority of passes needing managed cross-plugin compositing order.
- **No change to the scaffolding template's copy-paste-risk root cause beyond the one, narrow,
  confirmed idempotency bug (R6's own scope)** — R6's own "longer-term... a future, separate
  design pass" note explicitly defers a full scaffold-template rewrite; PHASE9 only fixes the
  confirmed duplicate-forward-declaration bug and adds the new convenience API, it does not
  rewrite the generated file's own boilerplate-comment shape.

### Cross-cutting rules every phase must follow

- Every new parameter added to an existing function signature is a TRAILING, DEFAULTED
  parameter — matching this codebase's own universal `AddRenderPass()`/`WriteColorAttachment()`
  convention (see `AGENTS.md`). Every pre-existing call site must keep compiling completely
  unmodified unless that phase's own explicit job is to migrate it.
- Every new exhaustive `switch` has NO `default:` case (see `AGENTS.md`).
- Every new Tier-1-testable pure-logic piece (ceiling-division-style math, reflection-result
  parsing, the idempotency-guard fix, the `usage`-aware pooling equality check) gets a matching
  Tier-1 test in the SAME phase that introduces it — never deferred to PHASE10.
- **Every brand-new Tier-1 test `.cpp` file must also be registered in `tests/CMakeLists.txt`'s
  `GTE_TEST_SOURCES` list** (confirmed by direct inspection: this is a plain, manually-maintained
  `set(GTE_TEST_SOURCES ...)`/`list(APPEND GTE_TEST_SOURCES ...)` list, NOT a `CONFIGURE_DEPENDS`
  glob — `add_executable(GreatTamanaEngineTests ${GTE_TEST_SOURCES})` only compiles exactly the
  files named in that list) — a new file that is merely created on disk but never added to this
  list silently never compiles or runs, and `ctest`/`--gtest_filter` will report "no tests found"
  rather than a build error. This concretely affects PHASE1's new
  `tests/Renderer/Vulkan/ShaderReflectionTests.cpp` and PHASE3's new
  `tests/Renderer/RenderGraph/CommandBufferTests.cpp` (both brand-new files) — PHASE2/8/9 only
  extend an ALREADY-registered existing test file, so no new list entry is needed for those. Add
  the new path to the list in the SAME phase that creates the file; do not rely on PHASE10's own
  test-count cross-check to catch a missed registration after the fact.
- Per Note 4/5 of this campaign's own process rules: no phase before PHASE10 runs a full clean
  build or full `ctest` regression pass — use a fast, incremental `cmake --build build` compile
  check plus a narrow, targeted test run (`ctest -R <pattern>` or the test binary's own
  `--gtest_filter`) for that phase's own new/changed tests only. Where a phase's own change is
  visually observable, use `gte_send_request`/`run_app_background` for a live, HTTP-driven visual
  check BEFORE moving on — do not wait until PHASE10 to discover a visual regression that was
  actually introduced three phases earlier.
- Every phase writes its own `PHASEn_COMPLETION_REPORT.md` in this same folder, documents exactly
  what changed, and commits via `git_add`/`git_commit`.
- Every implementation phase (PHASE1 through PHASE10) is delegated as its own `delegate_task` and
  must NOT itself call `delegate_task` — per this repository's own process note, implementation
  phases are leaf tasks, never sub-orchestrators. Every delegated implementation task must use
  `ask_questions` whenever it hits a genuine design ambiguity this document does not already
  resolve (e.g. the exact working-directory convention for PHASE1's new Tier-1 test to locate a
  compiled `.spv` fixture file).
