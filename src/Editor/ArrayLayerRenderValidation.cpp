#include "ArrayLayerRenderValidation.h"

#include "../Math/Vec3.h"
#include "../Renderer/Primitives/PrimitiveMeshGenerator.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../Renderer/Vertex.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gte {

namespace {

constexpr std::uint32_t kArrayWidth = 256;
constexpr std::uint32_t kArrayHeight = 256;
constexpr std::uint32_t kArrayLayers = 4;
constexpr const char* kArrayName = "ArrayLayerRenderValidationCascades";
constexpr VkFormat kVisualizeOutputFormat = VK_FORMAT_R8G8B8A8_UNORM;

constexpr std::array<const char*, 4> kWritePassNames = {
    "ArrayLayerRenderValidationWrite0",
    "ArrayLayerRenderValidationWrite1",
    "ArrayLayerRenderValidationWrite2",
    "ArrayLayerRenderValidationWrite3",
};
constexpr std::array<const char*, 4> kVisualizePassNames = {
    "ArrayLayerRenderValidationVisualize0",
    "ArrayLayerRenderValidationVisualize1",
    "ArrayLayerRenderValidationVisualize2",
    "ArrayLayerRenderValidationVisualize3",
};
constexpr std::array<const char*, 4> kLayerOutputNames = {
    "ArrayLayerRenderValidationLayer0",
    "ArrayLayerRenderValidationLayer1",
    "ArrayLayerRenderValidationLayer2",
    "ArrayLayerRenderValidationLayer3",
};

constexpr std::array<Vec3, 3> kObjectPositions = {
    Vec3{ -2.0f, 0.0f, 0.0f },
    Vec3{ 0.0f, 0.0f, 0.0f },
    Vec3{ 2.0f, 0.0f, 0.0f },
};

// Deliberately hand-picked per layer - proves per-layer targeting, not
// frustum culling (no generalized culling system is wired here - see this
// file's own header comment).
constexpr std::array<std::array<bool, 3>, kArrayLayers> kObjectVisibleInLayer = { {
    { true, false, false }, // layer 0
    { false, true, false }, // layer 1
    { false, false, true }, // layer 2
    { true, true, true }, // layer 3
} };

struct VisualizePushConstants {
    std::uint32_t layerIndex = 0;
};

} // namespace

ArrayLayerRenderValidation::~ArrayLayerRenderValidation()
{
    // m_writePipeline/m_cubeMesh/m_visualizePipeline/m_layerOutputs[*] are
    // RAII types and clean up themselves. m_visualizeDescriptorSetLayout is
    // BORROWED from m_visualizePipeline's own ReflectedDescriptorSetLayout(0)
    // - owned/destroyed by that ComputePipeline itself - mirrors
    // TextureArrayValidation::~TextureArrayValidation()'s own identical
    // reasoning. Never destroyed here.
}

void ArrayLayerRenderValidation::EnsureInitialized(Renderer& renderer)
{
    if (m_writePipeline.has_value()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Depth-only pipeline - an empty colorFormats span (see Pipeline.cpp's
    // own assert fix this harness required).
    m_writePipeline.emplace(renderer.CreatePipeline(std::span<const VkFormat>{},
        "shaders/ArrayLayerRenderValidation.vert.spv", "shaders/ArrayLayerRenderValidation.frag.spv",
        VertexLayout::PositionColor, /*useMaterialTexture=*/false, "ArrayLayerRenderValidationPipeline"));

    const std::vector<Vertex> cubeVertices = PrimitiveMeshGenerator::Generate(PrimitiveType::Cube);
    m_cubeMesh.emplace(renderer.CreateMesh(cubeVertices.data(), cubeVertices.size() * sizeof(Vertex),
        static_cast<std::uint32_t>(cubeVertices.size()), "ArrayLayerRenderValidationCube"));

    for (std::size_t i = 0; i < kObjectPositions.size(); ++i) {
        m_objectModel[i] = Mat4::Translation(kObjectPositions[i]);
    }

    // ONE SHARED, wide view-projection transform, built once - every layer
    // sees the exact same camera; only which cubes each layer's own write
    // pass submits differs (see this file's own header comment /
    // kObjectVisibleInLayer). The 3 cubes sit at world X = -2/0/2, so each
    // one lands at its own distinct, non-overlapping screen position under
    // this one camera.
    {
        const Vec3 target{ 0.0f, 0.0f, 0.0f };
        const Vec3 eye{ 0.0f, 0.0f, -5.0f };
        const Mat4 view = Mat4::LookAtLH(eye, target, Vec3::Up());
        const Mat4 proj = Mat4::OrthographicLH_ZO(-3.5f, 3.5f, -1.5f, 1.5f, 0.1f, 10.0f, /*flipY=*/true);
        m_sharedViewProj = proj * view;
    }

    // The 4 extraction passes' shared compute pipeline + per-layer
    // descriptor set/output - mirrors TextureArrayValidation's own Pass B
    // shape.
    m_visualizePipeline.emplace(
        renderer.CreateComputePipeline("shaders/ArrayLayerRenderValidationVisualize.comp.spv"));
    m_visualizeDescriptorSetLayout = m_visualizePipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    for (std::size_t i = 0; i < kLayerOutputNames.size(); ++i) {
        m_visualizeDescriptorSets[i] =
            ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_visualizeDescriptorSetLayout));
        m_layerOutputs[i].emplace(renderer.CreateRenderTexture(static_cast<int>(kArrayWidth),
            static_cast<int>(kArrayHeight), kVisualizeOutputFormat, kLayerOutputNames[i], /*depthDebugName=*/nullptr,
            /*allowStorageImageAccess=*/true));
    }
}

IEditorLayer::ArrayLayerRenderValidationHandles ArrayLayerRenderValidation::AddPass(
    rg::RenderGraphBuilder& builder, Renderer& renderer, rg::RenderPassToggleRegistry* toggleRegistry)
{
    // editor-core-separation-21 campaign's own precedent (see
    // TextureArrayValidation::AddPass()) - an ADDITIONAL, independent gate
    // on top of the caller's own ctx.showArrayLayerRenderValidationOutput
    // feature toggle.
    const bool enabled =
        toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled("ArrayLayerRenderValidation");
    if (!enabled) {
        return IEditorLayer::ArrayLayerRenderValidationHandles{};
    }

    EnsureInitialized(renderer);

    rg::TextureArrayDesc desc{};
    desc.width = kArrayWidth;
    desc.height = kArrayHeight;
    desc.arrayLayers = kArrayLayers;
    desc.hasDepth = true; // default, stated explicitly for clarity.
    const rg::TextureArrayHandle arrayHandle = builder.CreateTextureArray(kArrayName, desc);
    builder.KeepTextureArrayOutput(arrayHandle);

    // --- 4 write passes: one per layer, shared camera, hand-picked cubes --
    for (std::uint32_t i = 0; i < kArrayLayers; ++i) {
        builder.AddRenderPass(
            kWritePassNames[i], rg::PassKind::Graphics, rg::ViewScope::SceneView, rg::RenderPassCategory::Debug,
            [arrayHandle, i](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteArrayLayer(arrayHandle, i, rg::ResourceAccess::DepthStencilAttachmentReadWrite,
                    /*clearColor=*/std::nullopt, /*clearDepth=*/1.0f);
            },
            [this, arrayHandle, i](rg::PassContext& ctx) {
                // The attachment itself is already bound by RenderGraph's
                // own bracket - this handle is only needed if execute()
                // must read back the same layer it just wrote, which it
                // does not here; resolved anyway for parity/future use.
                const rg::PassContext::ResolvedTextureArrayLayer layer = ctx.resolveArrayLayer(arrayHandle, i);
                (void)layer;

                rg::CommandBuffer cmd = ctx.Cmd();
                for (std::size_t obj = 0; obj < kObjectVisibleInLayer[i].size(); ++obj) {
                    if (!kObjectVisibleInLayer[i][obj]) {
                        continue;
                    }
                    cmd.Draw(*m_writePipeline, *m_cubeMesh, m_objectModel[obj], m_sharedViewProj);
                }
            });
    }
    m_writtenThisFrame = true;

    // --- 4 extraction/visualization passes --------------------------------
    IEditorLayer::ArrayLayerRenderValidationHandles handles;
    std::array<rg::TextureHandle*, 4> handleSlots = { &handles.layer0, &handles.layer1, &handles.layer2,
        &handles.layer3 };

    for (std::size_t i = 0; i < kLayerOutputNames.size(); ++i) {
        const rg::TextureHandle outputHandle = builder.ImportTexture(kLayerOutputNames[i], m_layerOutputs[i]->Target(),
            VK_IMAGE_LAYOUT_UNDEFINED, m_layerOutputs[i]->Sampler(), m_layerOutputs[i]->DepthSampler());
        *handleSlots[i] = outputHandle;

        const std::uint32_t layerIndex = static_cast<std::uint32_t>(i);
        ComputeDescriptorSet& visualizeDescriptorSet = m_visualizeDescriptorSets[i];

        builder.AddRenderPass(
            kVisualizePassNames[i], rg::PassKind::Compute, rg::ViewScope::SceneView, rg::RenderPassCategory::Debug,
            [arrayHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.ReadTextureArray(arrayHandle, rg::ResourceAccess::ShaderRead);
                pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
            },
            [this, &visualizeDescriptorSet, arrayHandle, outputHandle, layerIndex](rg::PassContext& ctx) {
                const rg::PassContext::ResolvedTextureArray src = ctx.resolveTextureArray(arrayHandle);
                const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

                VkSampler sampler = VK_NULL_HANDLE;
                if (ctx.textureArrays != nullptr && arrayHandle.index < ctx.textureArrays->size()) {
                    sampler = (*ctx.textureArrays)[arrayHandle.index].target.sampler;
                }

                visualizeDescriptorSet.Rewrite(m_device,
                    std::vector<ComputeDescriptorWrite>{
                        ComputeDescriptorWrite::CombinedImageSampler(0, src.view, sampler),
                        ComputeDescriptorWrite::StorageImage(1, dest.view),
                    });

                VisualizePushConstants pushConstants{};
                pushConstants.layerIndex = layerIndex;

                rg::CommandBuffer cmd = ctx.Cmd();
                cmd.BindComputePipeline(*m_visualizePipeline);
                cmd.BindDescriptorSet(visualizeDescriptorSet.Native());
                cmd.SetPushConstants(pushConstants);
                cmd.DispatchOverSize(kArrayWidth, kArrayHeight);
            });
    }

    return handles;
}

void ArrayLayerRenderValidation::FinalizeForSampling(VkCommandBuffer cmd)
{
    if (!m_writtenThisFrame) {
        return;
    }
    m_writtenThisFrame = false;

    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);
    const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);

    for (const auto& output : m_layerOutputs) {
        if (output.has_value()) {
            rg::EmitImageBarrier(cmd, output->Image(), range, previous, next);
        }
    }
}

} // namespace gte
