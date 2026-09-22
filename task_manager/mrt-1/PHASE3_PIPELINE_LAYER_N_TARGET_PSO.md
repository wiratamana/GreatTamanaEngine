# PHASE3 — Pipeline Layer: Build a Real N-Target PSO

Parent: `PHASE0_MASTER_STRATEGY.md` — **read that file first**, and read
`PHASE1`/`PHASE2`'s own completion reports before starting.

**Use `ask_questions` whenever you hit a genuine ambiguity or a design choice
this document (or `PHASE0_MASTER_STRATEGY.md`) doesn't already pin down.** If
you delegate any further sub-task, that delegation prompt must repeat this
same instruction.

## Step 1: The Goal (Where are we going?)

Let `Pipeline` be constructed against N color formats (N up to
`kMaxColorAttachments` = 8, from PHASE1) instead of exactly one, building a
real `VkPipelineRenderingCreateInfo::colorAttachmentCount`/
`pColorAttachmentFormats` and one `VkPipelineColorBlendAttachmentState` per
target — while **every existing single-format call site
(`GpuResourceFactory::CreatePipeline()`/`Renderer::CreatePipeline()` and
every real pass that calls either) keeps compiling and behaving completely
unmodified**, with no source change required at any of those call sites.

## Step 2: The Situation (Where are we now?)

- `Pipeline`'s constructor (`src/Renderer/Pipeline.h` line 103,
  `.cpp` line 29) signature:
  ```cpp
  Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, const std::string& vertexShaderSpirvPath,
      const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
      VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE, const char* debugName = nullptr);
  ```
- Inside the constructor (`Pipeline.cpp`):
  - Line 143-151: exactly one `VkPipelineColorBlendAttachmentState
    colorBlendAttachment` (`blendEnable = VK_FALSE`, full write mask),
    `colorBlend.attachmentCount = 1; colorBlend.pAttachments =
    &colorBlendAttachment;`.
  - Line 193-198: `VkPipelineRenderingCreateInfo renderingInfo` with
    `colorAttachmentCount = 1; pColorAttachmentFormats = &colorFormat;`.
- `GpuResourceFactory::CreatePipeline()` (`GpuResourceFactory.h` line 137,
  `.cpp` line 279-286) takes a single `VkFormat colorFormat` and forwards it
  straight through:
  ```cpp
  Pipeline GpuResourceFactory::CreatePipeline(VkFormat colorFormat, const std::string& vertexShaderSpirvPath,
      const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture, const char* debugName) const
  {
      return Pipeline(m_device, colorFormat, m_depthFormat, vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
          useMaterialTexture ? MaterialDescriptorSetLayout() : VK_NULL_HANDLE, debugName);
  }
  ```
- `Renderer::CreatePipeline()` (`Renderer.cpp` line ~294, declared
  `Renderer.h` line 483) takes NO `VkFormat` parameter at all — it always
  calls `GpuResourceFactory::CreatePipeline()` with exactly `ColorFormat()`,
  unconditionally (confirmed directly against the real source — there is no
  "explicit override" parameter anywhere on `Renderer::CreatePipeline()`; the
  `line ~160` "resolved format" logic a previous draft of this document
  pointed at is actually inside a COMPLETELY DIFFERENT function,
  `Renderer::CreateRenderTexture()`'s own `VK_FORMAT_UNDEFINED`-means-
  "match `ColorFormat()`" resolution — unrelated to pipeline creation. This
  phase's new N-format overload on `Renderer::CreatePipeline()` (Step 3.2
  below) is therefore a genuinely NEW overload with no existing single-format
  override behavior to preserve beyond the one, unconditional `ColorFormat()`
  call every existing call site already goes through.
- Every real call site of either method today passes exactly one format —
  confirmed no existing call site needs more than one; this phase's new
  capability has ZERO real consumers until PHASE4.

## Step 3: The Plan (How do we get there?)

### 3.1 — `Pipeline`: add an N-format constructor, keep the 1-format one

Two workable shapes exist; pick ONE and apply it consistently (use
`ask_questions` if genuinely torn):

- **(a) Overload** — add a SECOND constructor taking
  `const std::vector<VkFormat>& colorFormats` (or `std::span<const VkFormat>`
  if this codebase already uses `<span>` elsewhere for a similar purpose —
  check `RenderGraph::ExecuteCompiledGraph()`'s own `Compile(input,
  std::span<const TextureHandle>(finalOutputs))` call for precedent, which
  suggests `std::span` is already an accepted idiom here), and have the
  EXISTING single-`VkFormat` constructor simply delegate to it with a
  one-element span/vector. This is the RECOMMENDED shape — it means the
  actual PSO-building logic exists in exactly one place.
- **(b) Single constructor, changed parameter type** — change the existing
  constructor's `colorFormat` parameter itself to
  `std::span<const VkFormat>`, and give every existing call site (which
  passes a bare `VkFormat`) an implicit single-element construction path.
  This is workable too but touches the one, single, canonical constructor
  signature directly, which is a larger textual diff across every call
  site's own header includes than (a). Prefer (a) unless a strong reason
  favors (b).

Sketch (shape (a), the recommended one):

```cpp
// Pipeline.h - NEW constructor, ADDITIVE (the existing single-VkFormat
// constructor below is UNCHANGED in its own signature and becomes a thin
// forwarder in Pipeline.cpp - see that file).
// Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE3 - builds
// a real N-color-attachment PSO. `colorFormats` must be non-empty and no
// larger than gte::rg::kMaxColorAttachments (RenderGraphTypes.h) - asserted
// in the .cpp. Every entry gets its own VkPipelineColorBlendAttachmentState,
// all identical (opaque, no blending, full RGBA write mask) - matching this
// engine's existing single-target default exactly; per-attachment blend
// state customization is explicitly out of scope for this campaign (a
// future need, not a speculative addition now).
Pipeline(VkDevice device, std::span<const VkFormat> colorFormats, VkFormat depthFormat,
    const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath,
    VertexLayout vertexLayout = VertexLayout::PositionColor,
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE, const char* debugName = nullptr);
```

`Pipeline.cpp` changes:
- Move the ENTIRE existing constructor body into this new
  `std::span<const VkFormat>`-taking constructor.
- Inside it: `assert(!colorFormats.empty() && colorFormats.size() <=
  gte::rg::kMaxColorAttachments);` (include `RenderGraphTypes.h` — check this
  does not introduce an unwanted circular/heavy include; `Pipeline.h`
  currently only includes `<volk.h>`/`<string>` — if pulling in
  `RenderGraphTypes.h` here feels architecturally wrong (Pipeline is a
  lower-level Renderer type; `rg::` is a higher-level namespace built ON TOP
  of Renderer), instead define a LOCAL constant in `Pipeline.h`/`.cpp` (e.g.
  `namespace gte { inline constexpr std::size_t kPipelineMaxColorAttachments
  = 8; }`) kept in sync by comment cross-reference with
  `gte::rg::kMaxColorAttachments`, rather than reaching upward into `rg::`
  from `Pipeline`. Use `ask_questions` if unsure which direction this
  project's own layering convention prefers — check whether `Pipeline.h`/
  `.cpp` include anything else from `Renderer/RenderGraph/` today (they do
  not, as far as this document's own research found) before deciding.
- Replace the single `colorBlendAttachment`/`colorBlend.attachmentCount = 1`
  block with:
  ```cpp
  std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorFormats.size());
  for (VkPipelineColorBlendAttachmentState& state : colorBlendAttachments) {
      state.blendEnable = VK_FALSE;
      state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  }

  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = static_cast<std::uint32_t>(colorBlendAttachments.size());
  colorBlend.pAttachments = colorBlendAttachments.data();
  ```
- Replace the `renderingInfo.colorAttachmentCount = 1; pColorAttachmentFormats
  = &colorFormat;` pair with:
  ```cpp
  renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
  renderingInfo.pColorAttachmentFormats = colorFormats.data();
  ```
- ADD BACK the original single-`VkFormat` constructor as a thin forwarder:
  ```cpp
  Pipeline::Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat,
      const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath,
      VertexLayout vertexLayout, VkDescriptorSetLayout materialSetLayout, const char* debugName)
      : Pipeline(device, std::span<const VkFormat>(&colorFormat, 1), depthFormat, vertexShaderSpirvPath,
            fragmentShaderSpirvPath, vertexLayout, materialSetLayout, debugName)
  {
  }
  ```
  (Delegating-constructor syntax — confirm this compiles cleanly with this
  project's exact compiler/standard settings; if delegating constructors
  interact awkwardly with the existing try/catch shader-module-cleanup
  structure inside the body, keep the ORIGINAL full body in the
  single-format constructor instead and have IT be the one that builds a
  one-element span and calls a shared PRIVATE helper method/free function
  that both constructors call — pick whichever avoids fragile double-catch
  semantics; check the existing try/catch around `vertModule`/`fragModule`
  destruction carefully either way.)

### 3.2 — `GpuResourceFactory`/`Renderer`: parallel N-format overloads

`GpuResourceFactory.h`/`.cpp`: add an overload of `CreatePipeline()` taking
`std::span<const VkFormat> colorFormats` instead of `VkFormat colorFormat`,
forwarding into `Pipeline`'s new span constructor. The EXISTING single-format
overload keeps its exact current signature and body (still forwards into
`Pipeline`'s single-format constructor, or — if using the delegating-
constructor shape above — can equally forward into the span constructor with
a one-element span; either is correct, prefer whichever keeps the
`GpuResourceFactory` diff smallest).

`Renderer::CreatePipeline()` (`Renderer.h`/`.cpp`): same pattern — add a
parallel N-format overload. **No existing call site of either method needs
to change** — this phase adds capability, it does not migrate anything.

### 3.3 — What this phase does NOT touch

- No real pass calls the new N-format overload yet — that's PHASE4's job.
  This phase alone has zero live consumers of its own new code path (other
  than, ideally, a small compile-only smoke check — see Definition of Done).
- `RenderGraph.cpp`, `RenderGraphBuilder`/`RenderGraphTypes.h` — untouched
  (already done in PHASE1/PHASE2).

### Definition of Done for this phase

- `Pipeline` can be constructed against 1..8 color formats; the existing
  single-format constructor/call sites are all still valid and produce an
  IDENTICAL PSO to before (same `colorAttachmentCount = 1`, same single
  `VkPipelineColorBlendAttachmentState`) — confirm this by re-reading the
  generated code path for the 1-format case line-by-line, the same
  equivalence discipline PHASE2 required.
- `GpuResourceFactory::CreatePipeline()`/`Renderer::CreatePipeline()` each
  gain a parallel N-format overload; every existing call site compiles
  completely unmodified. **Confirmed directly via `search_in_dir` for
  `.CreatePipeline(` across all of `src/` — the ONLY two real call sites of
  `Renderer::CreatePipeline()` today are `Game/Instantiation/
  MeshAssetGpuCatalog.cpp` (the `Mesh`/`TexturedMesh` pipelines) and
  `Game/Instantiation/PrimitiveGpuCatalog.cpp` (the default/Triangle
  primitive pipeline).** A previous draft of this document (and of
  `PHASE0_MASTER_STRATEGY.md`) incorrectly listed "sky background, scene
  grid, mesh preview, GPU skinning, Atmosphere passes" as additional
  `CreatePipeline()`/`Pipeline` call sites this phase must keep compiling —
  that is factually wrong and should not be used as a checklist:
  `AtmosphereSkyBackgroundRenderer`/`SceneGridRenderer`/`AssetPreviewMesh`
  (mesh preview) each build their OWN raw `VkPipeline` directly, bypassing
  `Renderer::CreatePipeline()`/`Pipeline` entirely (confirmed via each
  file's own header comment — e.g. `AtmosphereSkyBackgroundRenderer.h`'s
  explicit "bypasses `Renderer::CreatePipeline()`/`CreateMesh()`/`Submit()`
  entirely, builds its own dedicated `VkPipeline`/`VkPipelineLayout`
  directly" note), and GPU Skinning / every Atmosphere LUT pass are
  COMPUTE-only work built via the entirely separate `ComputePipeline`/
  `CreateComputePipeline()` path, which has no color-format/attachment-count
  concept at all and is completely untouched by this campaign. These passes
  are unaffected by this phase not because of the additive-overload design,
  but because they never went through `Pipeline`'s constructor to begin
  with — still re-confirm this via `search_in_dir` before finishing, since a
  future engine change could add a new real call site between when this
  document was written and when this phase actually runs.
- A minimal, throwaway compile-time (or, if cheap enough given this is a
  live-device-needing class, a real live smoke instantiation inside an
  existing Editor-only debug code path — do NOT wire it into any real,
  user-visible pass yet) proof that a genuine 2-format `Pipeline`
  constructs successfully against a live `VkDevice` without a validation-
  layer error is strongly encouraged here, before PHASE4 builds real shaders
  against it — if this is impractical without PHASE4's shader/pass existing
  yet, it is acceptable to defer this specific live check to PHASE4's own
  Definition of Done instead; note which path was taken in this phase's
  completion report.
- A fast, targeted incremental build of `Pipeline.cpp`/`GpuResourceFactory.cpp`/
  `Renderer.cpp` and their direct dependents succeeds.
- `PHASE3_COMPLETION_REPORT.md` written, recording which of shape (a)/(b)
  from Step 3.1 was used and why, and whether the live-smoke-instantiation
  check was done here or deferred to PHASE4.
