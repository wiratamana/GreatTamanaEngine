# PHASE3_SCENE_GRID_RENDERER — The `SceneGridRenderer` Pipeline-Owning Class

> Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: `PHASE2_GRID_SHADERS.md`
> (needs the compiled `SceneGrid.vert.spv`/`SceneGrid.frag.spv`) and
> `PHASE1_GRID_MATH_FOUNDATION.md` (this class does NOT call
> `SceneGridMath.h` itself — the math runs on the GPU — but must stay
> consistent with it; see Step 2 below).

## Step 1: The Goal (Where are we going?)

Add a new class, `src/Editor/SceneGridRenderer.h` / `.cpp`, that:

1. Owns a dedicated `VkPipeline`/`VkPipelineLayout` built directly from
   `SceneGrid.vert.spv`/`SceneGrid.frag.spv` (Phase 2), lazily on first use
   — mirroring `AssetPreviewMesh::EnsurePipeline()`'s exact, already-proven
   pattern in this codebase (same file-reading helper, same
   `VkShaderModule` creation/cleanup discipline, same push-constant-range
   convention).
2. Exposes one public method, `Draw(Renderer& renderer, VkCommandBuffer cmd,
   const Mat4& sceneViewProjection)`, that computes the inverse matrix,
   binds the pipeline, pushes the constants, and issues exactly one
   `vkCmdDraw(3, 1, 0, 0)` — no vertex/index buffer bound at all (per
   Phase 2's full-screen-triangle vertex shader).
3. Is a proper RAII owner (constructor does nothing GPU-related; a
   `Reset()` method + destructor tear down the pipeline safely, mirroring
   `AssetPreviewMesh::Reset()`'s exact `vkDeviceWaitIdle()`-before-destroy
   discipline).

This phase produces a fully self-contained, **compilable but still
unused** class — nothing in the engine calls `SceneGridRenderer::Draw()`
yet. That wiring is Phase 4's job specifically, so a mistake in Phase 4's
wiring can never be confused with a mistake in this class's own pipeline
setup.

## Step 2: The Situation / The Problem (Where are we now?)

- `src/Editor/AssetPreviewMesh.h`/`.cpp` is the exact precedent to copy the
  shape of (see its own class comment: *"Deliberately bypasses
  Renderer::CreatePipeline()/Renderer::CreateMesh()/Renderer::Submit()
  entirely... Builds its own small VkPipeline/VkPipelineLayout directly...
  this is exactly the kind of thing AGENTS.md's 'Editor Module Structure'
  section already sanctions"*). `SceneGridRenderer` needs the same
  treatment for a different reason: `Renderer::CreatePipeline()` only ever
  builds pipelines with `depthWriteEnable = true`, no alpha blending, and
  one of a few fixed vertex layouts — none of which fit a
  no-vertex-input, depth-tested-but-not-depth-writing, alpha-blended,
  `gl_FragDepth`-writing full-screen-triangle pipeline.
- Unlike `AssetPreviewMesh` (which owns its own `RenderTexture` and calls
  `Renderer::RenderOffscreen()` itself), `SceneGridRenderer` does **NOT**
  own a render target and does **NOT** call `RenderOffscreen()` /
  `BeginGraphPassRecording()`/`EndGraphPassRecording()` itself — Phase 4
  wires this class's `Draw()` call to run *inside* an already-open
  `vkCmdBeginRendering` bracket (the `"SceneView"` RenderGraph pass's own),
  supplied a live `VkCommandBuffer` from the outside. `SceneGridRenderer`
  itself has **zero** knowledge of `RenderGraph`/`RenderTexture`/
  `IEditorLayer` — it only ever sees a plain `Renderer&` (for
  `ColorFormat()`/`DepthFormat()`/`GetVulkanContextInfo()`, exactly what
  `AssetPreviewMesh::EnsurePipeline()` already uses those for) and a raw
  `VkCommandBuffer`.
- The push-constant layout is locked by Phase 2's shader:
  `mat4 invViewProj` immediately followed by `mat4 viewProj`, 128 bytes
  total, `VK_SHADER_STAGE_FRAGMENT_BIT` only (the vertex shader reads no
  push constants at all — double check `SceneGrid.vert` from Phase 2 has no
  `layout(push_constant)` block; only `SceneGrid.frag` does). This is
  DIFFERENT from every other pipeline in this engine (which all push
  `model` then `viewProj` to the VERTEX stage) — do not copy
  `AssetPreviewMesh.cpp`'s `VkPushConstantRange.stageFlags =
  VK_SHADER_STAGE_VERTEX_BIT` verbatim; this one must be
  `VK_SHADER_STAGE_FRAGMENT_BIT`.
- `Mat4::TryInverse()` is the correct choice here (never the asserting
  `Inverse()`) — a live, user-controlled `EditorCamera` can, at least in
  theory, reach a degenerate configuration (e.g. a zero/near-zero field of
  view momentarily during some future UI edit) and this class must degrade
  gracefully (skip drawing that frame) rather than assert-crash the whole
  Editor.

## Step 3: The Plan

### 3.1 — Create `src/Editor/SceneGridRenderer.h`

```cpp
#pragma once

#include "../Math/Mat4.h"

#include <volk.h>

namespace gte {

class Renderer;

// Draws the Editor's "Scene" panel infinite ground grid (Unity-style
// procedural shader grid - see
// task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md) - a
// single full-screen-triangle draw call using a dedicated pipeline built
// from SceneGrid.vert/frag (PHASE2_GRID_SHADERS.md). Deliberately
// bypasses Renderer::CreatePipeline()/Renderer::CreateMesh()/
# Renderer::Submit() entirely, for the same reason AssetPreviewMesh
// already does (see that class's own header comment) - this pipeline
// needs alpha blending, depth-test-without-depth-write, a
// fragment-shader-written gl_FragDepth, and zero vertex input, none of
// which Renderer::CreatePipeline()'s fixed built-in vertex
// layouts/pipeline state support.
//
// Unlike AssetPreviewMesh, this class owns NO RenderTexture and never
// calls Renderer::RenderOffscreen()/BeginGraphPassRecording() itself -
// Draw() is called from INSIDE an already-open vkCmdBeginRendering
// bracket the caller (see PHASE4_RENDERGRAPH_INTEGRATION.md) owns, so the
// grid is recorded as one more draw call alongside the real scene
// geometry already drawn into the exact same color/depth attachments this
// frame - this is what makes it correctly depth-TESTED (never
// depth-WRITTEN - see Draw()'s own doc comment) against real scene
// objects already in front of it.
//
// Owns its VkPipeline/VkPipelineLayout for as long as they're needed - all
// released by Reset() (called by the destructor, and MUST also be called
// explicitly by ImGuiEditorLayer's destructor BEFORE
// ImGui_ImplVulkan_Shutdown(), mirroring AssetPreviewMesh/
// ComputeBlurValidation's own exact requirement - though in practice, since
// this class holds no ImGui descriptor of its own at all, plain
// destruction order is already safe; Reset() is still exposed explicitly
// for symmetry/consistency with those two siblings).
class SceneGridRenderer {
public:
    SceneGridRenderer() = default;
    ~SceneGridRenderer();

    SceneGridRenderer(const SceneGridRenderer&) = delete;
    SceneGridRenderer& operator=(const SceneGridRenderer&) = delete;
    SceneGridRenderer(SceneGridRenderer&&) = delete;
    SceneGridRenderer& operator=(SceneGridRenderer&&) = delete;

    // Records one full-screen-triangle draw call for the grid directly
    // against `cmd`, using `sceneViewProjection` (the SAME combined
    // view * projection matrix the real Scene-view geometry was just
    // rendered with this frame - e.g. EditorCamera::ViewProjection(aspect))
    // both to unproject each pixel's camera ray AND to re-derive its
    // correct depth. Lazily builds this object's own pipeline on first
    // call (needs a live Renderer/VkDevice - can't happen in the default
    // constructor).
    //
    // A safe no-op (draws nothing) whenever `sceneViewProjection` turns
    // out to be singular (Mat4::TryInverse() fails) - never asserts/
    // crashes on live, user-controlled camera state.
    //
    // The bound pipeline enables depth TESTING (VK_COMPARE_OP_LESS,
    // matching every other pipeline in this engine) but disables depth
    // WRITING, and alpha-blends against whatever is already in the color
    // attachment - see PHASE0_MASTER_STRATEGY.md's own "Depth handling"/
    // "Blending" design decisions for why. The caller is responsible for
    // having already set a viewport/scissor covering the render target
    // (already true by construction inside a RenderGraph pass - see
    // RenderGraph::ExecuteCompiledGraph()'s own per-pass vkCmdSetViewport/
    // vkCmdSetScissor call) - this method never sets either itself.
    void Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection);

    // Releases the pipeline/pipeline layout (if built) - waits for the GPU
    // to be idle first, same reasoning as AssetPreviewMesh::Reset(). Safe
    // to call repeatedly/on an already-empty instance.
    void Reset();

private:
    void EnsurePipeline(Renderer& renderer);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace gte
```

(Fix the stray `#` typo in the pasted comment above when creating the real
file — same proofreading note as Phase 2.)

### 3.2 — Create `src/Editor/SceneGridRenderer.cpp`

Mirror `AssetPreviewMesh.cpp`'s `ReadShaderFile()`/`CreateShaderModule()`
anonymous-namespace helpers verbatim (copy them — they are file-local, not
shared, exactly like `BoneViewerWindow.cpp` also has its own independent
copies of the same small helpers; this engine's own convention is that
these tiny Editor-Vulkan-bootstrap helpers are duplicated per file, not
factored out — do not "clean this up" into a shared header as part of this
phase).

```cpp
#include "SceneGridRenderer.h"

#include "../Renderer/Renderer.h"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace gte {

namespace {

std::vector<char> ReadShaderFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "SceneGridRenderer: failed to open shader file '" + path + "' - was it compiled? See cmake/CompileShaders.cmake.");
    }
    const std::size_t size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& spirv)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size();
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(spirv.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("SceneGridRenderer: vkCreateShaderModule failed.");
    }
    return module;
}

bool DepthFormatHasStencil(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT;
}

} // namespace

SceneGridRenderer::~SceneGridRenderer()
{
    Reset();
}

void SceneGridRenderer::Reset()
{
    if (m_device != VK_NULL_HANDLE && m_pipeline != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
}

void SceneGridRenderer::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline != VK_NULL_HANDLE) {
        return;
    }

    const VkDevice device = m_device;
    const VkFormat colorFormat = renderer.ColorFormat();
    const VkFormat depthFormat = renderer.DepthFormat();

    const std::vector<char> vertSpirv = ReadShaderFile("shaders/SceneGrid.vert.spv");
    const std::vector<char> fragSpirv = ReadShaderFile("shaders/SceneGrid.frag.spv");

    VkShaderModule vertModule = CreateShaderModule(device, vertSpirv);
    VkShaderModule fragModule = VK_NULL_HANDLE;
    try {
        fragModule = CreateShaderModule(device, fragSpirv);
    } catch (...) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }

    try {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        // No vertex input at all - SceneGrid.vert synthesizes its 3
        // full-screen-triangle vertices purely from gl_VertexIndex.
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Depth TEST enabled (occluded correctly by real scene geometry),
        // depth WRITE disabled (a semi-transparent overlay must never
        // poison the depth buffer) - see PHASE0's own "Depth handling"
        // design decision.
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Standard alpha blending - see PHASE0's own "Blending" design
        // decision.
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &colorBlendAttachment;

        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        // mat4 invViewProj immediately followed by mat4 viewProj, 128
        // bytes total, FRAGMENT stage only - matches SceneGrid.frag's own
        // layout(push_constant) block EXACTLY (see PHASE2_GRID_SHADERS.md).
        // Deliberately NOT VK_SHADER_STAGE_VERTEX_BIT (unlike every other
        // pipeline in this engine) - SceneGrid.vert reads no push
        // constants at all.
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float) * 32; // 2 x mat4

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("SceneGridRenderer: vkCreatePipelineLayout failed.");
        }

        const bool depthHasStencil = DepthFormatHasStencil(depthFormat);
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        renderingInfo.depthAttachmentFormat = depthFormat;
        renderingInfo.stencilAttachmentFormat = depthHasStencil ? depthFormat : VK_FORMAT_UNDEFINED;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(std::size(stages));
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
            throw std::runtime_error("SceneGridRenderer: vkCreateGraphicsPipelines failed.");
        }
    } catch (...) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

void SceneGridRenderer::Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection)
{
    m_device = renderer.GetVulkanContextInfo().device;

    Mat4 invViewProj;
    if (!sceneViewProjection.TryInverse(invViewProj)) {
        return; // Degenerate camera matrix this frame - draw nothing rather than assert/crash.
    }

    EnsurePipeline(renderer);

    struct PushConstants {
        float invViewProj[16];
        float viewProj[16];
    } pushConstants;
    std::memcpy(pushConstants.invViewProj, invViewProj.Data(), sizeof(pushConstants.invViewProj));
    std::memcpy(pushConstants.viewProj, sceneViewProjection.Data(), sizeof(pushConstants.viewProj));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdPushConstants(
        cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace gte
```

Add `#include <cstring>` (for `std::memcpy`) to the includes list above.

### 3.3 — Register the new files in `CMakeLists.txt`

Inside the same `if(GTE_ENABLE_EDITOR)` `target_sources()` block from Phase
1, immediately after the `SceneGridMath.*` lines just added:

```cmake
        src/Editor/SceneGridMath.h
        src/Editor/SceneGridMath.cpp
        src/Editor/SceneGridRenderer.h
        src/Editor/SceneGridRenderer.cpp
```

### 3.4 — `Definition of Done` for this phase

- `SceneGridRenderer.h`/`.cpp` compile cleanly as part of `gte_core`.
- Nothing in the engine constructs a `SceneGridRenderer` or calls `Draw()`
  yet — that's expected and correct; Phase 4 is the first real caller.
- Fast compile check: build `GreatTamanaEngine` (the real executable
  target, not just the test target) since `SceneGridRenderer.cpp` needs the
  compiled shaders from Phase 2 to exist as a build dependency
  (`gte_add_shader()`'s custom command) — confirm the build succeeds and
  `build/<config>/shaders/SceneGrid.{vert,frag}.spv` are staged next to the
  built `.exe`.
