# COMPUTE_PHASE3_COMPLETION_REPORT.md

Session report for **Phase 3 — Descriptors: tell shader where stuff is**
(the "Descriptor Binding Model" phase) of the compute-shader campaign
described in `COMPUTE_SHADER_MASTER_STRATEGY_v2.md`. Scope was taken
directly from `COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md`.
Nothing beyond that document's own "Step 3: The Plan" was implemented, per
its own "Step 4: What We Will NOT Do".

Work was done on the pre-existing branch `feature/compute-shader-impl`,
picking up immediately after Phase 2
(`COMPUTE_PHASE2_COMPLETION_REPORT.md`).

## What shipped

Every change is purely additive to already-shipped `GpuResourceFactory`/
`Renderer` code — no existing call site's observable behavior changed, and
no dispatch/render-graph work was touched at all (that's Phases 4-6).

### New file: `DescriptorSetLayoutBuilder`

- **`src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h`/`.cpp`** — a small,
  fluent helper class wrapping `VkDescriptorSetLayoutCreateInfo`
  construction: `AddStorageBuffer(binding, stageFlags =
  VK_SHADER_STAGE_COMPUTE_BIT, count = 1)`, `AddStorageImage(...)`,
  `AddCombinedImageSampler(...)` each append one
  `VkDescriptorSetLayoutBinding` and return `*this` for chaining;
  `Build()` creates and returns a brand-new `VkDescriptorSetLayout` from
  every binding accumulated so far (throwing `std::runtime_error` on
  failure, mirroring every other Vulkan-object-creation error message
  style already established in this codebase). The caller owns the
  returned layout and is responsible for destroying it once every
  `ComputePipeline`/descriptor set built against it is gone — exactly the
  same lifetime discipline `GpuResourceFactory`'s own
  `m_materialSetLayout` already follows.
- **Binding-number convention documented, not enforced** (per the
  strategy document's own explicit refusal of shader reflection):
  bindings are assigned in declaration order, read top-to-bottom as (1)
  `StructuredBuffer`/read-only inputs, (2) `RWStructuredBuffer`/read-write
  buffers, (3) `Texture`/read-only sampled inputs, (4) `RWTexture`/storage
  images last. Written directly into the header's own doc comment so a
  future `.comp` file's author has a single, discoverable place to check
  the rule against.

### New file: `ComputeDescriptorSet`

- **`src/Renderer/ComputeDescriptorSet.h`/`.cpp`** — `ComputeDescriptorWrite`
  (a small POD describing one binding's current physical resource, with
  three static factory helpers — `StorageBuffer()`, `StorageImage()`
  (always `VK_IMAGE_LAYOUT_GENERAL`), `CombinedImageSampler()` (always
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`) — so a caller never has to
  guess which group of fields applies for a given descriptor type) plus
  `ComputeDescriptorSet` (a small, explicit, **non-RAII** value type
  wrapping a single `VkDescriptorSet`, exactly mirroring `MaterialTexture`'s
  own "allocated from a shared pool, never individually freed" convention
  — see `MaterialTexture.h`). `ComputeDescriptorSet::Rewrite(device,
  writes)` issues exactly one `vkUpdateDescriptorSets()` call covering
  every supplied write, using parallel `std::vector<VkDescriptorBufferInfo>`/
  `std::vector<VkDescriptorImageInfo>` reserved up front so pointers taken
  into them while building each `VkWriteDescriptorSet` are never
  invalidated by a mid-loop reallocation. Deliberately **not** optimized to
  skip unchanged bindings — cheap, always-correct, matching this engine's
  existing "correctness over micro-optimization until proven necessary"
  discipline (see `AGENTS.md`).

### `GpuResourceFactory` — second, dedicated compute descriptor pool

- **`m_computeDescriptorPool`** (new member) — created once in the
  constructor, right after the existing material descriptor pool,
  entirely separate from `m_materialDescriptorPool` (different descriptor
  type requirements — `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`/
  `STORAGE_IMAGE`/`COMBINED_IMAGE_SAMPLER` — so a compute-heavy feature can
  never exhaust the material-texture pool's budget or vice versa, exactly
  per the strategy document's own instruction). Sized generously but
  modestly (256 storage buffers, 128 storage images, 128 combined-image-
  samplers, 256 max sets — "a low hundreds, not thousands," since compute
  shaders are far less numerous per-frame than material textures).
  Destroyed in `Destroy()`; correctly threaded through both the move
  constructor and move-assignment operator.
- **`GpuResourceFactory::AllocateComputeDescriptorSet(VkDescriptorSetLayout
  layout)`** (new method) — allocates one `VkDescriptorSet` from
  `m_computeDescriptorPool` against a caller-supplied layout (built via
  `DescriptorSetLayoutBuilder` above). Like `m_materialDescriptorPool`'s
  own sets, a set allocated here is never individually freed — only the
  whole pool at once, when the factory itself is destroyed.
- **`Renderer::AllocateComputeDescriptorSet()`** — a plain one-line
  forward, mirroring every other `Renderer` → `GpuResourceFactory`
  passthrough (`CreateComputePipeline()`, `CreateStructuredBuffer()`,
  etc.).

## Build system changes

- Root `CMakeLists.txt`: added
  `src/Renderer/ComputeDescriptorSet.h`/`.cpp` to `gte_core`'s source list
  (right after `Renderer/ComputePipeline.h`/`.cpp`), and
  `src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h`/`.cpp` (right after
  `Vulkan/ShaderModule.h`/`.cpp`).
- No test-file changes this phase — `DescriptorSetLayoutBuilder`/
  `ComputeDescriptorSet`/`GpuResourceFactory::AllocateComputeDescriptorSet()`
  are all Tier-2 (GPU-touching, `VkDevice`-dependent) by nature, falling
  into the same accepted "no automated coverage yet, manually verified"
  bucket as `Pipeline`/`Buffer`/`RenderTexture`/`GpuResourceFactory`
  themselves (see `TESTING.md`'s own note on this). No new
  Tier-1-testable pure logic was introduced this phase — everything here
  is either a thin Vulkan-object-creation wrapper or a straight
  `vkUpdateDescriptorSets()` call, none of which is meaningfully
  decomposable into pure, device-free logic.

## Verification performed

- Reconfigured with CMake (Ninja generator, reusing the existing `build/`
  directory) — no network access needed, everything was already fetched.
- Built `gte_core` from a clean-of-these-changes incremental build —
  compiled with zero warnings/errors introduced by the new/changed files.
- Built the full project (`GreatTamanaEngine.exe` **and**
  `GreatTamanaEngineTests.exe`) — both link successfully; shaders staged
  correctly next to the executable.
- Ran the **entire** existing test suite: **625 of 626 tests passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  a pre-existing machine-gated smoke test unrelated to this change —
  expected and documented in `TESTING.md`). **Zero regressions** —
  identical result to Phase 1's and Phase 2's own verification runs.
- Launched the real `GreatTamanaEngine.exe` (which constructs a real
  `Renderer`/`GpuResourceFactory`/live `VkPhysicalDevice`/`VkDevice` on
  startup, exercising the new second descriptor pool's creation path end
  to end) and confirmed it stayed running (no crash/exception at startup,
  confirmed via `tasklist` a few seconds after launch) before stopping it
  — worth confirming directly since `GpuResourceFactory`'s constructor
  body grew a second `vkCreateDescriptorPool()` call this phase, and the
  test suite never constructs a live `Renderer`/`GpuResourceFactory` (see
  `TESTING.md`'s "Tier 2" note).
- No validation-layer run was possible on this development machine — as
  already noted in `COMPUTE_PHASE2_COMPLETION_REPORT.md`,
  `VK_LAYER_KHRONOS_validation` is not installed here (a pre-existing
  environment limitation, not something this phase introduced or can
  control). This phase adds no new descriptor *usage* (no
  `vkCmdBindDescriptorSets`/`vkUpdateDescriptorSets` call site is
  exercised by anything yet — see "What was deliberately NOT done" below),
  so there is nothing live to validate beyond the pool/layout creation
  paths already covered by the manual executable smoke test above.

## Acceptance criteria check (against the strategy doc's own "Step 3: The Plan")

- ✅ `DescriptorSetLayoutBuilder` built: fluent `AddStorageBuffer()`/
  `AddStorageImage()`/`AddCombinedImageSampler()`, `Build()` creates a
  fresh `VkDescriptorSetLayout` each call.
- ✅ Binding-number convention documented directly in the builder's own
  header comment (declaration order: read-only buffers, read-write
  buffers, read-only textures, storage images last).
- ✅ `GpuResourceFactory` extended with a SECOND, dedicated descriptor
  pool (`m_computeDescriptorPool`) sized for
  `STORAGE_BUFFER`/`STORAGE_IMAGE`/`COMBINED_IMAGE_SAMPLER`, never shared
  with `m_materialDescriptorPool`.
- ✅ `GpuResourceFactory::AllocateComputeDescriptorSet()`/
  `Renderer::AllocateComputeDescriptorSet()` added, allocating from that
  pool, never individually freed.
- ✅ `ComputeDescriptorSet` built as a small, explicit, non-RAII value type
  with a `Rewrite(...)` method taking the current buffer/image handles to
  bind, issuing exactly one `vkUpdateDescriptorSets()` call — matching the
  strategy document's specified shape exactly ("wraps a single
  `VkDescriptorSet` plus a small `Rewrite(...)`-style method... calling
  `vkUpdateDescriptorSets()` fresh each time it's invoked").
- ✅ The "descriptor set must be re-written whenever
  `RenderGraphResourcePool` hands back a different physical resource" rule
  is documented directly in `ComputeDescriptorSet.h`'s own class comment,
  ready to be flagged loudly to whoever builds Phase 6 (per the strategy
  document's own "Step 5: Their Role" instruction).

## What was deliberately NOT done (per the strategy doc's own "Step 4")

- No bindless descriptor indexing (`VK_EXT_descriptor_indexing`) —
  `DescriptorSetLayoutBuilder`'s `count` parameter exists purely for a
  small, fixed array binding if ever needed; nothing in this campaign uses
  it as anything but 1.
- No automatic re-validation that a shader's GLSL bindings match the C++
  `DescriptorSetLayoutBuilder` calls — a documented human convention only,
  consistent with this campaign's overall refusal of shader reflection.
- No descriptor-set caching/deduplication across different compute passes
  — each future compute pass will build and own its own layout/set.
- No support for a descriptor set spanning multiple frames-in-flight with
  independent double/triple-buffered copies — deferred to however Phase 6
  wires this into the render graph's existing resource-pooling model.
- No actual compute pass, dispatch, or `.comp` shader using any of this
  phase's new infrastructure yet — that begins in earnest with Phase 4
  (dispatch execution) and is fully exercised for the first time by Phase
  6/7's real workloads. This phase deliberately stayed a pure
  infrastructure addition, exactly like Phase 1/2 before it.

## Handoff notes for whoever picks up Phase 4

- Phase 4 (`COMPUTE_PHASE4_DISPATCH_EXECUTION_STRATEGY_v2.md`) is the next
  unit of work — `ComputeGroupCount()`/`ComputeGroupCount3D()` (pure,
  Tier-1-testable dispatch math in a new `src/Renderer/ComputeDispatch.h`)
  and `Renderer::Dispatch()` (the compute sibling of `Renderer::Submit()`,
  issuing `vkCmdBindPipeline`/`vkCmdBindDescriptorSets`/`vkCmdPushConstants`/
  `vkCmdDispatch` directly against the current render-graph pass's command
  buffer — see that document's own Step 6 for why NO
  `PassContext::recordDispatch()` callback should be added).
- `ComputePipeline`'s constructor already accepts a plural
  `std::vector<VkDescriptorSetLayout>` (Phase 2) — a layout built via this
  phase's `DescriptorSetLayoutBuilder` slots in directly with no further
  `ComputePipeline` changes needed.
- When Phase 4's dispatch call binds a descriptor set, it should take a
  plain `VkDescriptorSet` (e.g. `ComputeDescriptorSet::Native()`) — mirror
  how `Renderer::Submit()` already takes a plain `VkDescriptorSet
  materialDescriptorSet` parameter for the graphics side, rather than
  inventing a second convention.
- The throwaway validation shape Phase 2 already exercised by hand (a
  single storage-buffer binding, compute stage, binding 0) is a good first
  smoke test for `DescriptorSetLayoutBuilder`/`AllocateComputeDescriptorSet()`/
  `ComputeDescriptorSet` together once Phase 4's dispatch call exists —
  worth reusing that exact shape rather than re-deriving it from scratch,
  per Phase 2's own handoff note.
- Re-read this phase's own `ComputeDescriptorSet.h` class comment before
  wiring Phase 6's per-frame descriptor rewrite logic — it already states
  the exact hazard (`RenderGraphResourcePool` may hand back a different
  physical resource across frames) that Phase 6's own strategy document
  independently identifies as "the single easiest correctness mistake to
  make in the whole campaign."
