# COMPUTE_PHASE2_PIPELINE_INFRASTRUCTURE_STRATEGY_v1.md

### Child document 2 of 7 — see `COMPUTE_SHADER_MASTER_STRATEGY_v1.md` for the full campaign map.
### Corresponds to the user's requested **"Module 1: ComputePipeline"**.

## Step 1: The Goal

Get a real `VkPipeline` bound to `VK_PIPELINE_BIND_POINT_COMPUTE` compiling,
loading, and ready to bind — completely independent of any specific
workload (no culling logic, no blur logic, nothing RenderGraph-aware yet).
This mirrors exactly how the original Render Graph campaign's own Phase 1
built pure vocabulary with zero live-device dependency before touching
execution, and how `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s
own Phase A already scopes this same idea for the culling workload
specifically — **this document generalizes that into shared infrastructure
both this campaign and the GPU-driven document consume**, so it is built
exactly once.

## Step 2: The Situation

- `Pipeline.h/.cpp` (`src/Renderer/Pipeline.h/.cpp`) only ever builds a
  `VkGraphicsPipelineCreateInfo` (vertex + fragment stage,
  `VkPipelineRenderingCreateInfo` for dynamic rendering). There is no
  `VkComputePipelineCreateInfo` anywhere in the codebase.
- `Pipeline.cpp`'s SPIR-V-loading logic (reading a compiled `.spv` file off
  disk and calling `vkCreateShaderModule()`) is currently private to that
  one file — there is no shared, reusable "load a `VkShaderModule` from a
  `.spv` path" free function a `ComputePipeline` could call without
  duplicating that exact file-read + `vkCreateShaderModule` dance a second
  time.
- `GpuResourceFactory` (`src/Renderer/GpuResourceFactory.h/.cpp`) has no
  `CreateComputePipeline()` method — its constructor already owns a
  `VkCommandPool`/`m_materialSetLayout`/`m_materialDescriptorPool`, the
  exact kind of "own the shared plumbing, hand out ready-to-use objects"
  role a compute pipeline factory method belongs in.
- `cmake/CompileShaders.cmake`'s `gte_add_shader()` compiles `.vert`/`.frag`
  files to SPIR-V via `glslc` by inferring the shader stage from the file
  extension — `glslc` already recognizes `.comp` as a valid stage
  extension with zero tool changes needed; this must be *confirmed*, not
  assumed, as the very first concrete step of this phase.
- Every `Pipeline`'s constructor takes a `VkDescriptorSetLayout` optionally
  (see `Pipeline.h`'s `materialSetLayout` parameter) — the equivalent
  concept for `ComputePipeline` needs to accept potentially MULTIPLE
  descriptor set layouts (a compute shader's storage buffers/images will
  very often live in a dedicated set distinct from any material set), so
  `ComputePipeline`'s constructor shape should not simply copy `Pipeline`'s
  single-optional-layout signature verbatim — see Plan below.

## Step 3: The Plan

- **Extract a shared SPIR-V loader first.** New `src/Renderer/Vulkan/
  ShaderModule.h/.cpp` — a single free function,
  `VkShaderModule LoadShaderModule(VkDevice device, const std::string&
  spirvPath)`, doing exactly what `Pipeline.cpp` already does internally
  (read the file's raw bytes, build a `VkShaderModuleCreateInfo`, call
  `vkCreateShaderModule()`, throw `std::runtime_error` on failure — mirror
  `Pipeline.cpp`'s existing error message style exactly). Update
  `Pipeline.cpp` itself to call this new shared function instead of its own
  private copy, as a pure, behavior-preserving refactor (run the full test
  suite before/after, per `AGENTS.md`'s "Testability & Regression Safety" —
  no test should change behavior, since this is byte-for-byte the same
  logic, just relocated).
- **New `src/Renderer/ComputePipeline.h/.cpp`** — an RAII wrapper mirroring
  `Pipeline`'s shape exactly (constructor acquires, destructor releases,
  move-only, `Native()`/`Layout()` accessors), but for
  `VK_PIPELINE_BIND_POINT_COMPUTE`:
  - One `VkPipelineLayout`, built from a caller-supplied
    `std::vector<VkDescriptorSetLayout>` (plural — see Situation above) plus
    an optional push-constant range. Compute shaders' per-dispatch
    parameters vary far more than graphics' fixed `model`/`viewProj` pair
    (a culling shader wants view-frustum planes + counts; a blur shader
    wants a resolution + kernel radius) — `ComputePipeline`'s constructor
    should accept a plain `VkPushConstantRange` (or `std::optional<std::uint32_t>`
    for its byte size, sized per-shader) rather than assuming any one fixed
    128-byte layout the way `Pipeline`'s graphics push-constant convention
    does. Document per-shader push-constant layouts as local conventions
    (a comment block above each `.comp` file's own `layout(push_constant)`
    block), not a shared engine-wide struct.
  - One `VkPipeline`, built from a single
    `VkPipelineShaderStageCreateInfo` (`VK_SHADER_STAGE_COMPUTE_BIT`) whose
    module comes from the new shared `LoadShaderModule()` above.
  - `VkComputePipelineCreateInfo::layout` set to the constructed
    `VkPipelineLayout`; no `VkPipelineRenderingCreateInfo` involved at all
    (that struct is graphics-dynamic-rendering-specific and has no compute
    equivalent).
- **`GpuResourceFactory::CreateComputePipeline(const std::string&
  shaderSpirvPath, const std::vector<VkDescriptorSetLayout>&
  descriptorSetLayouts, std::optional<std::uint32_t> pushConstantBytes)`**
  — same ownership/factory convention as `CreatePipeline()`.
  `Renderer::CreateComputePipeline()` forwards to it exactly like every
  other `GpuResourceFactory` passthrough on `Renderer`.
- **Confirm `.comp` support in `gte_add_shader()`/`CompileShaders.cmake`**
  with a real, throwaway shader (see below) before assuming it "just
  works" — `glslc`'s stage inference by extension is documented behavior,
  but this project's own `CompileShaders.cmake` wrapper has never been
  exercised against anything but `.vert`/`.frag`, so this is worth an
  explicit, first, cheap verification step rather than an assumption
  carried silently into Phase 4/6.
- **Throwaway validation shader**: `Shaders/Passthrough.comp` — the
  simplest possible compute shader (e.g. one thread writes a fixed constant
  into element 0 of a bound `RWStructuredBuffer`), invoked via a temporary,
  non-shipped test call site (mirroring the original Render Graph
  campaign's own Phase 6 "throwaway two-pass test scene" discipline).
  Confirms, end-to-end, with validation layers enabled: pipeline creation
  succeeds, the shader module loads, `vkCmdDispatch(1,1,1)` (once Phase 4
  exists — or a manual, hand-written dispatch call for THIS phase's own
  isolated verification, since Phase 4 hasn't landed yet and this phase
  should not block on it) runs with zero validation errors, and reading the
  buffer back afterward shows the expected value. Delete this shader/call
  site once Phase 6/7's real validation workloads exist — never ship it.

## Step 4: What We Will NOT Do

- No shader permutation/variant system (specialization constants,
  `#define`-driven variants) — one `.comp` file compiles to exactly one
  `ComputePipeline`, mirroring `Pipeline`'s own one-shader-pair-per-pipeline
  simplicity.
- No shader hot-reload — a `ComputePipeline` is built once at startup/asset-
  load time, exactly like `Pipeline` today.
- No shader reflection of any kind (binding numbers, push-constant size) —
  see the master document's campaign-wide refusal.
- No RenderGraph awareness whatsoever in this phase — `ComputePipeline`
  itself never sees a `PassContext`/`RenderGraphBuilder`; that is Phase 6's
  concern entirely.

## Step 5: Their Role

- This is, per the GPU-driven-rendering companion document's own words,
  "the single highest-risk, first-of-its-kind Vulkan surface area" this
  engine has touched since its original graphics pipeline was built. Get it
  manually verified with validation layers clean, via the throwaway
  passthrough shader, before letting Phase 3/4 build anything on top of it.
- If this is being built alongside (or after) `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s
  own Phase A, treat them as the SAME deliverable — do not let two
  different engineers independently build two different `ComputePipeline`
  classes. Whichever lands first should be the one the other document's
  own Phase A is considered "already satisfied by."
- Delete the throwaway `Passthrough.comp`/its test call site before this
  phase is considered complete — a leftover unused shader compiled into
  every build is exactly the kind of small debt `AGENTS.md`'s existing
  cleanup discipline (see `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`,
  Section B.3, on `Renderer::Present()`'s own dead code) warns against
  accumulating.
