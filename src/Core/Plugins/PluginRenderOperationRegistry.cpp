#include "PluginRenderOperationRegistry.h"

#include "../../Renderer/Renderer.h"
#include "../../Renderer/Vertex.h"
#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <cassert>
#include <utility>

namespace gte {

PluginRenderOperationRegistry::PluginRenderOperationRegistry(Renderer& renderer)
    : m_renderer(renderer)
{
}

void PluginRenderOperationRegistry::InsertOp(PluginRenderOpInfo info)
{
    assert(info.slots.size() <= 8
        && "PluginRenderOperationRegistry::InsertOp: a registered operation's own slot table must never exceed 8 "
           "entries - IPluginCommandRecorder::BindTexture()/BindBuffer() bind into a fixed 8-entry scratch array "
           "(PluginRenderPassBuilderAdapter_v3).");
    const std::string id = info.id;
    m_ops.emplace(id, std::move(info));
}

void PluginRenderOperationRegistry::RegisterUberOp(const char* id, std::uint32_t opCode)
{
    PluginRenderOpInfo info;
    info.id = id;
    info.kind = PluginRenderOpKind::Compute;
    info.slots = { PluginRenderOpSlot{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, false } };
    info.opCode = opCode;
    info.maxParamBytes = sizeof(RenderFeatureOpsPushConstants);
    info.computePipeline = &(*m_opsPipeline);
    info.descriptorSetLayout = m_opsDescriptorSetLayout;
    InsertOp(std::move(info));
}

void PluginRenderOperationRegistry::RegisterBoxBlur()
{
    // Binding convention (matches Shaders/BoxBlur.comp/ComputeBlurValidation.cpp
    // exactly): binding 0 = read-only sampler2D sourceTexture (CombinedImageSampler),
    // binding 1 = writeonly rgba8 image2D destinationImage (StorageImage), a
    // (width, height) uint32 push-constant pair (8 bytes).
    // task_manager/better-render-pass-1 campaign, PHASE7
    // (PHASE7_MIGRATE_PLUGIN_RENDER_OPERATION_REGISTRY.md) - a path-only
    // CreateComputePipeline() call: real SPIR-V reflection builds the
    // descriptor-set layout and push-constant range directly from
    // shaders/BoxBlur.comp.spv's own compiled binding/push-constant
    // metadata, instead of a hand-built DescriptorSetLayoutBuilder layout +
    // a manually restated VkPushConstantRange.
    m_boxBlurPipeline.emplace(m_renderer.CreateComputePipeline("shaders/BoxBlur.comp.spv"));
    m_boxBlurDescriptorSetLayout = m_boxBlurPipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    PluginRenderOpInfo info;
    info.id = "gte.builtin.box_blur";
    info.kind = PluginRenderOpKind::Compute;
    info.slots = {
        PluginRenderOpSlot{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false },
        PluginRenderOpSlot{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, false },
    };
    info.opCode = 0; // unused - gte.builtin.box_blur has its own dedicated pipeline, never the shared uber-ops one.
    info.maxParamBytes = sizeof(std::uint32_t) * 2;
    info.computePipeline = &(*m_boxBlurPipeline);
    info.descriptorSetLayout = m_boxBlurDescriptorSetLayout;
    InsertOp(std::move(info));
}

// editor-core-separation-9 campaign, PHASE3
// (PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md, Step 3.2) -
// registers `gte.builtin.blit_fullscreen`, this campaign's ONE brand-new,
// tiny, additive GRAPHICS-kind registry operation - a minimal
// fullscreen-triangle passthrough fragment shader (Shaders/
// PluginBlitFullscreen.vert/.frag), the second real proof (alongside
// RegisterBoxBlur() above, PHASE2) that a new operation lands with ZERO
// IPluginRenderPassBuilder_v3 interface change, this time for a GRAPHICS-kind
// operation.
//
// This op's own graphics Pipeline is built through the standard, shared
// `Pipeline` class (Renderer/Pipeline.h) directly (NOT
// Renderer::CreatePipeline()'s own convenience overloads, which only ever
// bind GpuResourceFactory's own SHARED material descriptor-set-layout when
// `useMaterialTexture=true` - this op needs its OWN, independent
// VkDescriptorSetLayout instead, exactly like RegisterBoxBlur()/
// EnsureBuiltinsRegistered() above already build their own independent
// layouts rather than reusing any shared one) - `Pipeline`'s constructor
// ALWAYS unconditionally enables a real depth test AND always declares a
// real vertex-input binding (VertexLayout::PositionColor here), with no way
// to opt out (see Pipeline.h's own class comment) - a REAL, confirmed
// finding (by direct code reading, not a guess) that
// src/Editor/GBufferValidation.cpp's own header comment already documents
// for the EXACT same underlying shader technique (a full-screen triangle
// synthesized purely from gl_VertexIndex, ignoring real vertex-attribute
// data). This registry therefore mirrors GBufferValidation.cpp's own
// already-shipped, already-verified workaround exactly, rather than a raw,
// hand-rolled VkPipeline (AtmosphereSkyBackgroundRenderer's own style,
// which was considered and rejected here - see
// PHASE3_COMPLETION_REPORT.md for the full reasoning): (a) a real, but
// throwaway/never-read-by-the-shader, 3-vertex dummy Mesh is bound before
// every draw (PluginRenderOpInfo::dummyVertexBuffer,
// PluginRenderPassBuilderAdapter_v3::DrawFullscreenTriangle()); (b)
// PluginRenderPassBuilderAdapter_v3::AddGraphicsPass() transparently
// attaches a scratch/unused depth-stencil write (cleared to 1.0f, exactly
// mirroring GBufferValidation.cpp's own kGBufferScratchClearDepth) against
// this plugin's own private output target's companion depth buffer (every
// RenderTexture always owns one - RenderTexture.h) - invisible to the
// plugin author, since IPluginPassSetupContext has no
// WriteDepthStencilAttachment method at all (the Design Doc's own curated
// PluginResourceAccess vocabulary never included one - a `_v3` plugin has
// no ABI-level way to declare a depth attachment itself).
void PluginRenderOperationRegistry::RegisterBlitFullscreen()
{
    // Binding convention (matches Shaders/PluginBlitFullscreen.frag exactly):
    // binding 0 = sourceTexture, a read-only combined image sampler,
    // FRAGMENT stage only - DescriptorSetLayoutBuilder::AddCombinedImageSampler()'s
    // own `stageFlags` parameter defaults to VK_SHADER_STAGE_COMPUTE_BIT, so
    // this MUST be passed explicitly (flagged during PHASE2's own planning -
    // see PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md Step 2.2 - this is
    // this whole campaign's first GRAPHICS-kind registry entry).
    DescriptorSetLayoutBuilder blitLayoutBuilder(m_device);
    m_blitDescriptorSetLayout =
        blitLayoutBuilder.AddCombinedImageSampler(/*binding=*/0, VK_SHADER_STAGE_FRAGMENT_BIT).Build();

    // See this method's own header comment above - a real, but
    // throwaway/never-read-by-the-shader, 3-vertex triangle, purely so
    // Pipeline's mandatory vertex binding (Pipeline.h) has SOMETHING real
    // bound at draw time. Values are never read by Shaders/
    // PluginBlitFullscreen.vert (a pure gl_VertexIndex full-screen triangle,
    // mirroring Shaders/AtmosphereSkyBackground.vert/GBufferValidation.vert).
    const Vertex dummyVertices[3] = {
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
    m_blitDummyTriangle.emplace(
        m_renderer.CreateMesh(dummyVertices, sizeof(dummyVertices), 3, "PluginBlitFullscreenDummyTriangle"));

    // Deliberately HARDCODED VK_FORMAT_R8G8B8A8_UNORM (NOT
    // Renderer::ColorFormat()) - see m_blitPipeline's own header comment
    // (PluginRenderOperationRegistry.h) for the full reasoning: this op only
    // ever draws into a `_v3` plugin's own private compositing target,
    // which RenderFeatureCompositor::EnsureTextureSized() always creates at
    // this exact fixed format, never the swapchain's own negotiated one.
    // The depth format DOES use the real Renderer::DepthFormat() - every
    // RenderTexture's own companion DepthBuffer (including a plugin's own
    // private target) is always created against that real, negotiated
    // format (GpuResourceFactory::CreateRenderTexture()).
    m_blitPipeline.emplace(m_device, VK_FORMAT_R8G8B8A8_UNORM, m_renderer.DepthFormat(),
        "shaders/PluginBlitFullscreen.vert.spv", "shaders/PluginBlitFullscreen.frag.spv", VertexLayout::PositionColor,
        m_blitDescriptorSetLayout, "PluginBlitFullscreen.vert/.frag");

    PluginRenderOpInfo info;
    info.id = "gte.builtin.blit_fullscreen";
    info.kind = PluginRenderOpKind::DrawFullscreenTriangle;
    info.slots = { PluginRenderOpSlot{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false } };
    info.opCode = 0; // unused - meaningful only for entries sharing the shared uber-ops pipeline.
    info.maxParamBytes = 0; // a literal passthrough - no parameters needed at all.
    info.graphicsPipeline = &(*m_blitPipeline);
    info.descriptorSetLayout = m_blitDescriptorSetLayout;
    info.dummyVertexBuffer = m_blitDummyTriangle->VertexBuffer();
    InsertOp(std::move(info));
}

void PluginRenderOperationRegistry::EnsureBuiltinsRegistered()
{
    if (m_builtinsRegistered) {
        return;
    }
    m_builtinsRegistered = true;

    const Renderer::VulkanContextInfo context = m_renderer.GetVulkanContextInfo();
    m_device = context.device;

    // --- The shared "uber ops" pipeline (RenderFeatureOps.comp) - migrated
    // verbatim from RenderFeatureCompositor::EnsureOpsInitialized() (this
    // phase's own Step 3.2) - byte-for-byte identical construction, just
    // relocated. Binding convention (matches Shaders/RenderFeatureOps.comp
    // exactly): binding 0 = privateTarget, the ONLY binding - a single
    // read-write storage image. ---
    // task_manager/better-render-pass-1 campaign, PHASE7
    // (PHASE7_MIGRATE_PLUGIN_RENDER_OPERATION_REGISTRY.md) - a path-only
    // CreateComputePipeline() call: real SPIR-V reflection builds the
    // descriptor-set layout and push-constant range directly from
    // shaders/RenderFeatureOps.comp.spv's own compiled binding/push-constant
    // metadata, instead of a hand-built DescriptorSetLayoutBuilder layout +
    // a manually restated VkPushConstantRange.
    m_opsPipeline.emplace(m_renderer.CreateComputePipeline("shaders/RenderFeatureOps.comp.spv"));
    m_opsDescriptorSetLayout = m_opsPipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    RegisterUberOp("gte.builtin.solid_fill", /*opCode=*/0);
    RegisterUberOp("gte.builtin.radial_vignette", /*opCode=*/1);
    RegisterUberOp("gte.builtin.color_grade", /*opCode=*/2);

    // --- The shared blend pipeline (RenderFeatureBlend.comp) - migrated
    // verbatim from RenderFeatureCompositor::EnsureBlendPipelineInitialized() -
    // NOT itself a PluginRenderOpInfo entry (no plugin ever Dispatch()es this
    // directly - it is purely RenderFeatureCompositor's own internal
    // compositing mechanism, unreachable from any _v3 opId). Binding
    // convention (matches Shaders/RenderFeatureBlend.comp exactly): binding 0
    // = dstIn, binding 1 = srcIn (both read-only combined image samplers),
    // binding 2 = destinationImage (a write-only storage image). ---
    // task_manager/better-render-pass-1 campaign, PHASE7
    // (PHASE7_MIGRATE_PLUGIN_RENDER_OPERATION_REGISTRY.md) - a path-only
    // CreateComputePipeline() call: real SPIR-V reflection builds the
    // descriptor-set layout and push-constant range directly from
    // shaders/RenderFeatureBlend.comp.spv's own compiled binding/push-constant
    // metadata, instead of a hand-built DescriptorSetLayoutBuilder layout +
    // a manually restated VkPushConstantRange.
    m_blendPipeline.emplace(m_renderer.CreateComputePipeline("shaders/RenderFeatureBlend.comp.spv"));
    m_blendDescriptorSetLayout = m_blendPipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    // --- gte.builtin.box_blur - the genuinely NEW operation PHASE2 added
    // (Design Doc R13's central proof, a COMPUTE-kind operation), reusing
    // the ALREADY-EXISTING, ALREADY-SHIPPED shaders/BoxBlur.comp.spv
    // verbatim - its own, separate pipeline/layout, with zero
    // IPluginRenderPassBuilder_v3 interface change. ---
    RegisterBoxBlur();

    // --- gte.builtin.blit_fullscreen - PHASE3's own genuinely NEW operation
    // (Design Doc R13's central proof, a SECOND time, this time for a
    // GRAPHICS-kind operation) - a minimal fullscreen-triangle passthrough
    // fragment shader, its own, separate Pipeline/layout, again with zero
    // IPluginRenderPassBuilder_v3 interface change. ---
    RegisterBlitFullscreen();
}

const PluginRenderOpInfo* PluginRenderOperationRegistry::Find(const std::string& id) const noexcept
{
    const auto it = m_ops.find(id);
    return it == m_ops.end() ? nullptr : &it->second;
}

const ComputePipeline& PluginRenderOperationRegistry::OpsPipeline() const noexcept
{
    assert(m_opsPipeline.has_value() && "PluginRenderOperationRegistry::OpsPipeline: EnsureBuiltinsRegistered() was never called");
    return *m_opsPipeline;
}

VkDescriptorSetLayout PluginRenderOperationRegistry::OpsDescriptorSetLayout() const noexcept
{
    return m_opsDescriptorSetLayout;
}

const ComputePipeline& PluginRenderOperationRegistry::BlendPipeline() const noexcept
{
    assert(m_blendPipeline.has_value() && "PluginRenderOperationRegistry::BlendPipeline: EnsureBuiltinsRegistered() was never called");
    return *m_blendPipeline;
}

VkDescriptorSetLayout PluginRenderOperationRegistry::BlendDescriptorSetLayout() const noexcept
{
    return m_blendDescriptorSetLayout;
}

} // namespace gte
