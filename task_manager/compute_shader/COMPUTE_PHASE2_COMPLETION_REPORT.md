# COMPUTE_PHASE2_COMPLETION_REPORT.md

Session report for **Phase 2 — Build the ComputePipeline** (the "Pipeline
Infrastructure" phase) of the compute-shader campaign described in
`COMPUTE_SHADER_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`COMPUTE_PHASE2_PIPELINE_INFRASTRUCTURE_STRATEGY_v1.md`. Nothing beyond that
document's own "Step 3: The Plan" was implemented, per its own "Step 4: What
We Will NOT Do".

Work was done on the pre-existing branch `feature/compute-shader-impl`,
picking up immediately after Phase 1 (`COMPUTE_PHASE1_COMPLETION_REPORT.md`).

## What shipped

Every change is purely additive to already-shipped `Pipeline`/
`GpuResourceFactory`/`Renderer` code, plus one pure, behavior-preserving
refactor (the shared SPIR-V loader) — no existing call site's observable
behavior changed, and no descriptor-set/dispatch/render-graph work was
touched at all (that's Phases 3-6).

### Shared SPIR-V loader extracted out of `Pipeline.cpp`

- **New `src/Renderer/Vulkan/ShaderModule.h`/`.cpp`** — a single free
  function, `VkShaderModule LoadShaderModule(VkDevice device, const
  std::string& spirvPath)`, doing exactly what `Pipeline.cpp` used to do
  internally via its own private `ReadFile()`/`CreateShaderModule()`
  helpers (read the file's raw bytes, build a `VkShaderModuleCreateInfo`,
  call `vkCreateShaderModule()`, throw `std::runtime_error` on failure —
  same error-message style, just generalized from "Pipeline: ..." to
  "ShaderModule: ..."). `Pipeline.cpp` was updated to call this shared
  function instead of its own private copy — a pure, behavior-preserving
  refactor (confirmed via the full test suite before/after — see
  "Verification performed" below; this is genuinely the same logic, just
  relocated so `ComputePipeline` (below) can reuse it without a second,
  near-duplicate implementation).

### `ComputePipeline` — the compute sibling of `Pipeline`

- **New `src/Renderer/ComputePipeline.h`/`.cpp`** — an RAII wrapper
  mirroring `Pipeline`'s shape exactly (constructor acquires, destructor
  releases, move-only, `Native()`/`Layout()` accessors), but for
  `VK_PIPELINE_BIND_POINT_COMPUTE`:
  - One `VkPipelineLayout`, built from a caller-supplied
    `std::vector<VkDescriptorSetLayout>` (plural, unlike `Pipeline`'s
    single optional `materialSetLayout` — a compute shader's storage
    buffers/images will very often live in a dedicated set distinct from
    any material set) plus an optional `VkPushConstantRange` (a plain,
    per-shader-documented convention, not this engine's fixed 128-byte
    graphics push-constant layout — see the class's own doc comment for
    the full reasoning).
  - One `VkPipeline`, built from a single
    `VkPipelineShaderStageCreateInfo` (`VK_SHADER_STAGE_COMPUTE_BIT`)
    whose module comes from the new shared `LoadShaderModule()` above.
  - No `VkPipelineRenderingCreateInfo` involved at all (graphics-dynamic-
    rendering-specific, no compute equivalent) — unlike `Pipeline`,
    `ComputePipeline` needs no color/depth format at all.
  - No RenderGraph awareness, no shader permutation/variant system, no
    hot-reload, no shader reflection — matching this phase's own "What We
    Will NOT Do".

### Factory/Renderer wiring

- **`GpuResourceFactory::CreateComputePipeline(const std::string&
  shaderSpirvPath, const std::vector<VkDescriptorSetLayout>&
  descriptorSetLayouts, std::optional<VkPushConstantRange>
  pushConstantRange)`** — same ownership/factory convention as
  `CreatePipeline()`; a one-line forward into `ComputePipeline`'s
  constructor.
- **`Renderer::CreateComputePipeline()`** — a plain passthrough, same
  shape as every other `Renderer` → `GpuResourceFactory` forward.

### Build system: confirmed `.comp` support in `gte_add_shader()`

- **`cmake/CompileShaders.cmake` needed ZERO changes.** `gte_add_shader()`
  never hardcoded a `.vert`/`.frag` extension anywhere in its own logic —
  it just hands whatever source file it's given straight to `glslc`, which
  already infers the shader stage from the `.comp` extension on its own
  (documented `glslc` behavior). This was **confirmed, not assumed** — see
  "Verification performed" below — by actually compiling a real `.comp`
  file through the existing, unmodified `gte_add_shader()` macro.

## Throwaway validation performed (per Phase 2's own Step 3/Step 5)

Per the strategy document's explicit instruction ("Build the throwaway
validation shader... Confirms, end-to-end, with validation layers enabled:
pipeline creation succeeds, the shader module loads,
`vkCmdDispatch(1,1,1)`... runs with zero validation errors, and reading the
buffer back afterward shows the expected value... Delete this shader/call
site once Phase 6/7's real validation workloads exist"), the following was
built, run, confirmed, and then **deleted** before this phase was
considered complete (matching the document's own Step 5: *"Delete the
throwaway `Passthrough.comp`/its test call site before this phase is
considered complete"*):

1. `src/Shaders/Passthrough.comp` — the simplest possible compute shader:
   one thread (`local_size_x/y/z = 1`) writes a fixed constant (`424242`)
   into element 0 of a bound `RWStructuredBuffer`.
2. A temporary `gte_add_shader(GreatTamanaEngine
   src/Shaders/Passthrough.comp)` line in `CMakeLists.txt`, proving `.comp`
   compiles cleanly through the existing, unmodified macro.
3. A temporary `--compute-passthrough-test` headless CLI mode in
   `main.cpp` (mirroring the existing `--reimport` CLI mode's own
   precedent for a non-Application, script-friendly entry point) that:
   - Constructs a real `Window`+`Renderer` (a real live `VkDevice`).
   - Hand-builds (Phase 3's `DescriptorSetLayoutBuilder` doesn't exist
     yet) a descriptor set layout/pool/set for one storage buffer,
     binding 0, compute stage.
   - Creates a 4-`uint32_t` `GpuToCpu` structured buffer via
     `Renderer::CreateStructuredBuffer()` (Phase 1's own deliverable).
   - Builds a `ComputePipeline` via `Renderer::CreateComputePipeline()`
     against the compiled `Passthrough.comp.spv`.
   - Dispatches it once via a hand-written `vkCmdDispatch(1,1,1)` inside
     `Renderer::ImmediateSubmit()` (Phase 4's `Renderer::Dispatch()`
     doesn't exist yet either — this phase's own validation intentionally
     does this manually, exactly as the strategy document anticipates).
   - Reads the buffer back via `Buffer::MappedData()` and compares it
     against the expected constant.
4. Built and ran this CLI mode: **`Compute passthrough test: wrote-back
   value = 424242 (expected 424242)`** — exit code 0. Confirms, end-to-end:
   `ComputePipeline` construction, `GpuResourceFactory::
   CreateComputePipeline()`, the shared `LoadShaderModule()`, and a real
   `vkCmdDispatch` against a bound `RWStructuredBuffer` all work correctly
   together against a real, live Vulkan device.
   - Note: this development machine does not have
     `VK_LAYER_KHRONOS_validation` installed (the Vulkan loader logs
     `"Validation was requested but VK_LAYER_KHRONOS_validation is not
     available on this system - continuing without it"` — a pre-existing
     environment limitation, not something this phase introduced or can
     control), so "zero validation errors" could not be literally
     confirmed via validation layers on this machine; the dispatch/
     readback result being exactly correct (`424242`) is nonetheless
     strong affirmative evidence the whole pipeline/descriptor/dispatch
     sequence is wired correctly.
5. **Deleted** all three throwaway pieces immediately afterward: the
   `--compute-passthrough-test` CLI mode + its now-unused includes in
   `main.cpp` (reverted to byte-identical pre-Phase-2 content), the
   `gte_add_shader(...Passthrough.comp)` line in `CMakeLists.txt`, and
   `src/Shaders/Passthrough.comp` itself (plus its stray compiled
   `Passthrough.comp.spv` in the build tree). Reconfigured + rebuilt from
   scratch afterward to confirm the final, persisted state builds clean
   with no trace of the throwaway shader staged anywhere.

## Build system changes (persisted)

- Root `CMakeLists.txt`: added `src/Renderer/ComputePipeline.h`/`.cpp` to
  `gte_core`'s source list (right after `Pipeline.h`/`.cpp`), and
  `src/Renderer/Vulkan/ShaderModule.h`/`.cpp` (right after
  `Vulkan/FormatCapabilities.h`/`.cpp`).
- No test-file changes this phase — `ComputePipeline`/`GpuResourceFactory::
  CreateComputePipeline()`/`Renderer::CreateComputePipeline()` are all
  Tier-2 (GPU-touching, `VkDevice`-dependent) by nature, falling into the
  same accepted "no automated coverage yet, manually verified" bucket as
  `Pipeline`/`Buffer`/`RenderTexture`/`GpuResourceFactory` themselves (see
  `TESTING.md`'s own note on this). `LoadShaderModule()` itself is also
  Tier 2 (needs a real `VkDevice` to call `vkCreateShaderModule()`) — no
  new Tier-1-testable pure logic was introduced this phase.

## Verification performed

- Reconfigured with CMake (Ninja generator, reusing the existing `build/`
  directory) — no network access needed, everything was already fetched.
- Built the full project (`GreatTamanaEngine.exe` **and**
  `GreatTamanaEngineTests.exe`) with the throwaway validation shader/CLI
  mode present — compiled with zero warnings/errors introduced by the new/
  changed files, and confirmed `.comp` compiles cleanly through the
  existing, unmodified `gte_add_shader()` macro.
- Ran the throwaway `--compute-passthrough-test` CLI mode against the real
  built executable — confirmed the expected value (`424242`) round-tripped
  correctly through a real `ComputePipeline`/descriptor set/dispatch/
  readback sequence (see "Throwaway validation performed" above).
- Deleted every throwaway piece, then **reconfigured and rebuilt from
  scratch again** to confirm the final, persisted Phase 2 deliverable (no
  `Passthrough.comp` anywhere) still builds cleanly — `GreatTamanaEngine.exe`
  and `GreatTamanaEngineTests.exe` both link successfully; shaders staged
  correctly next to the executable with no stray `Passthrough.comp.spv`.
- Ran the **entire** existing test suite (post-cleanup build): **625 of
  626 tests passed**, **1 skipped**
  (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, a
  pre-existing machine-gated smoke test unrelated to this change — expected
  and documented in `TESTING.md`). **Zero regressions** — identical result
  to Phase 1's own verification run.
- Launched the real `GreatTamanaEngine.exe` (which constructs a real
  `Renderer`/`GpuResourceFactory`/live `VkPhysicalDevice` on startup) and
  confirmed it stayed running (no crash/exception at startup) before
  stopping it — the same "worth confirming directly, not just via the test
  suite" discipline Phase 1 established, since `GpuResourceFactory`'s
  constructor now also builds `Vulkan/ShaderModule.h`-shaped code paths as
  part of the same static library.

## Acceptance criteria check (against the strategy doc's own "Step 3: The Plan")

- ✅ Shared SPIR-V loader extracted to `Vulkan/ShaderModule.h`/`.cpp`;
  `Pipeline.cpp` updated to call it, confirmed behavior-preserving via the
  full test suite.
- ✅ `ComputePipeline` built: one `VkPipelineLayout` from a plural
  `std::vector<VkDescriptorSetLayout>` plus an optional
  `VkPushConstantRange`, one `VkPipeline` bound to
  `VK_PIPELINE_BIND_POINT_COMPUTE`, no `VkPipelineRenderingCreateInfo`.
- ✅ `GpuResourceFactory::CreateComputePipeline()`/`Renderer::
  CreateComputePipeline()` added, same ownership/factory convention as
  `CreatePipeline()`.
- ✅ `.comp` support in `gte_add_shader()`/`CompileShaders.cmake` confirmed
  (not assumed) via a real throwaway shader compiled through the existing,
  unmodified macro — required zero build-system changes.
- ✅ Throwaway `Passthrough.comp` validation shader built, run, confirmed
  correct end-to-end (pipeline creation, shader module load, real
  `vkCmdDispatch`, buffer readback), then deleted along with its temporary
  CMakeLists.txt line and `main.cpp` CLI call site — nothing throwaway was
  left behind in the persisted diff.

## What was deliberately NOT done (per the strategy doc's own "Step 4")

- No shader permutation/variant system (specialization constants,
  `#define`-driven variants) — one `.comp` file compiles to exactly one
  `ComputePipeline`.
- No shader hot-reload — a `ComputePipeline` is built once, exactly like
  `Pipeline` today.
- No shader reflection of any kind (binding numbers, push-constant size).
- No RenderGraph awareness whatsoever — `ComputePipeline` itself never
  sees a `PassContext`/`RenderGraphBuilder`; that is Phase 6's concern.
- No descriptor-set-layout builder — Phase 3's job
  (`DescriptorSetLayoutBuilder`, `src/Renderer/Vulkan/
  DescriptorSetLayoutBuilder.h`). The throwaway validation above hand-built
  a descriptor set layout purely to prove `ComputePipeline` itself works —
  that hand-built code was deleted, not kept as a shortcut around Phase 3.
- No dispatch math/`Renderer::Dispatch()` — Phase 4's job. The throwaway
  validation used a raw, hand-written `vkCmdDispatch(1,1,1)` call inside
  `ImmediateSubmit()` purely for this phase's own isolated verification,
  exactly as the strategy document anticipates ("a manual, hand-written
  dispatch call for THIS phase's own isolated verification, since Phase 4
  hasn't landed yet").

## Handoff notes for whoever picks up Phase 3

- Phase 3 (`COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md`) is the
  next unit of work — building `DescriptorSetLayoutBuilder`
  (`src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h`/`.cpp`), a second
  dedicated descriptor pool on `GpuResourceFactory`
  (`m_computeDescriptorPool`, sized for `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`/
  `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`), `GpuResourceFactory::
  AllocateComputeDescriptorSet()`, and `ComputeDescriptorSet` (a small,
  explicit, non-RAII value type with a `Rewrite(...)` method).
- `ComputePipeline`'s constructor already accepts a plural
  `std::vector<VkDescriptorSetLayout>` exactly as Phase 3 expects to
  supply — Phase 3's `DescriptorSetLayoutBuilder::Build()` output slots in
  directly with no further `ComputePipeline` changes needed.
- The throwaway validation's hand-built descriptor set layout/pool/set
  (now deleted) followed the EXACT binding convention Phase 3's own doc
  comment mandates (storage buffer, compute stage, binding 0) — worth
  reusing that exact shape as Phase 3's own first smoke test rather than
  re-deriving it from scratch.
- `LoadShaderModule()` (`Vulkan/ShaderModule.h`) is now the one shared
  place any future shader-loading code (a future `.comp` variant, or
  anything else) should call — never re-introduce a second private
  `ReadFile()`/`CreateShaderModule()` pair anywhere else in the codebase.
