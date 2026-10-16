#include "MeshPreviewRenderer.h"

#include "Buffer.h"
#include "Compute/ComputeDescriptorSet.h"
#include "Pipeline.h"
#include "Renderer.h"
#include "RenderTexture.h"
#include "ShaderFile.h"
#include "Texture2D.h"
#include "RenderGraph/CommandBuffer.h"
#include "../Core/Logging.h"
#include "../Core/PipelineDesc.h"

#include <array>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <vector>

namespace gte {

namespace {

constexpr const char* kLogCategory = "MeshPreview";

// Push constants of MeshPreview.vert.
struct PreviewPush {
    Mat4 model;
    Mat4 viewProjection;
};
static_assert(sizeof(PreviewPush) == 128, "layout differs from MeshPreview.vert");

// Must cover the scene global uniform block that MeshPreview.frag declares.
constexpr std::uint64_t kDummyUniformBlockSize = 128;

} // namespace

struct MeshPreviewRenderer::Impl {
    explicit Impl(Renderer& owner)
        : renderer(owner)
    {
    }

    // Also runs for a half-built instance: whatever exists is released after the GPU stops using it.
    ~Impl()
    {
        renderer.WaitForGpuIdle();
        ReleaseUiHandle();
        if (setOne.has_value()) {
            renderer.FreeComputeDescriptorSet(setOne->Native());
        }
        renderer.DestroyDescriptorSetLayout(setOneLayout);
        renderer.DestroyDescriptorSetLayout(zeroLayout);
    }

    void ReleaseUiHandle() noexcept
    {
        if (uiHandle != UiTextureHandle::Invalid) {
            renderer.ReleaseUiTextureHandle(uiHandle);
            uiHandle = UiTextureHandle::Invalid;
        }
    }

    // Set 1 stands in for MeshPreview.frag's lighting interface; one layout serves the set and the pipeline.
    void Build()
    {
        constexpr DescriptorBindingDesc kSetOneBindings[] = {
            { .binding = 0, .kind = DescriptorKind::CombinedImageSampler, .stages = ShaderStageMask::Fragment },
            { .binding = 8, .kind = DescriptorKind::UniformBuffer, .stages = ShaderStageMask::Fragment },
        };
        zeroLayout = renderer.CreateDescriptorSetLayout({});
        setOneLayout = renderer.CreateDescriptorSetLayout(kSetOneBindings);
        setOne.emplace(renderer.AllocateComputeDescriptorSet(setOneLayout));

        const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
        whiteTexture.emplace(renderer.CreateTexture2D(whitePixel, 1, 1, "MeshPreviewDummyWhite"));

        uniformBuffer.emplace(renderer.CreateBuffer(
            kDummyUniformBlockSize, BufferUsage::UniformBuffer, BufferMemoryUsage::CpuToGpu, "MeshPreviewDummyUniformBlock"));
        const std::array<std::byte, kDummyUniformBlockSize> zeroBytes{};
        uniformBuffer->Upload(zeroBytes.data(), zeroBytes.size());

        ComputeDescriptorWrite uniformWrite;
        uniformWrite.binding = 8;
        uniformWrite.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformWrite.buffer = uniformBuffer->Native();
        uniformWrite.bufferOffset = 0;
        uniformWrite.bufferRange = kDummyUniformBlockSize;
        setOne->Rewrite(std::vector<ComputeDescriptorWrite>{
            ComputeDescriptorWrite::CombinedImageSampler(0, whiteTexture->View(), whiteTexture->Sampler()),
            uniformWrite,
        });

        const DescriptorSetLayoutHandle extraSets[] = { zeroLayout, setOneLayout };
        PipelineDesc desc = PipelinePreset::Opaque(renderer.ColorPixelFormat(), renderer.DepthPixelFormat());
        desc.vertexLayout = VertexLayout::PositionNormal;
        desc.cullMode = CullMode::None; // Imported meshes have no guaranteed winding.
        desc.layout.extraSetLayouts = extraSets;
        desc.layout.pushConstantByteSize = sizeof(PreviewPush);
        desc.layout.pushConstantStages = ShaderStageMask::Vertex;

        const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/MeshPreview.vert.spv");
        const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/MeshPreview.frag.spv");
        pipeline.emplace(renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
    }

    // Makes `target` exactly width x height and its UI handle current. False when the engine could not.
    bool EnsureTarget(int width, int height)
    {
        if (target.has_value() && targetWidth == width && targetHeight == height) {
            return true;
        }
        try {
            ReleaseUiHandle();
            if (!target.has_value()) {
                target.emplace(
                    renderer.CreateRenderTexture(width, height, PixelFormat::Undefined, "MeshPreview", "MeshPreviewDepth"));
            } else {
                renderer.ResizeRenderTexture(*target, width, height);
            }
            targetWidth = width;
            targetHeight = height;
            uiHandle = renderer.GetUiTextureHandle(*target);
            return true;
        } catch (const std::exception& e) {
            target.reset();
            targetWidth = 0;
            targetHeight = 0;
            if (!targetFailureLogged) {
                targetFailureLogged = true;
                GTE_LOG_ERROR(kLogCategory, std::string("Preview target could not be made: ") + e.what());
            }
            return false;
        }
    }

    Renderer& renderer;
    DescriptorSetLayoutHandle zeroLayout = DescriptorSetLayoutHandle::Invalid;
    DescriptorSetLayoutHandle setOneLayout = DescriptorSetLayoutHandle::Invalid;
    std::optional<ComputeDescriptorSet> setOne;
    std::optional<Texture2D> whiteTexture;
    std::optional<Buffer> uniformBuffer;
    std::optional<Pipeline> pipeline;

    std::optional<Buffer> vertexBuffer;
    std::optional<Buffer> indexBuffer;
    std::uint32_t indexCount = 0;

    std::optional<RenderTexture> target;
    UiTextureHandle uiHandle = UiTextureHandle::Invalid;
    int targetWidth = 0;
    int targetHeight = 0;
    bool targetFailureLogged = false;
};

std::unique_ptr<MeshPreviewRenderer> MeshPreviewRenderer::TryCreate(Renderer& renderer)
{
    auto impl = std::make_unique<Impl>(renderer);
    try {
        impl->Build();
    } catch (const std::exception& e) {
        GTE_LOG_ERROR(kLogCategory, std::string("Mesh preview could not be built: ") + e.what());
        return nullptr;
    }
    return std::unique_ptr<MeshPreviewRenderer>(new MeshPreviewRenderer(std::move(impl)));
}

MeshPreviewRenderer::MeshPreviewRenderer(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

MeshPreviewRenderer::~MeshPreviewRenderer() = default;

bool MeshPreviewRenderer::SetMesh(std::span<const MeshPreviewVertex> vertices, std::span<const std::uint32_t> indices)
{
    Impl& impl = *m_impl;
    if (impl.vertexBuffer.has_value() || impl.indexBuffer.has_value()) {
        impl.renderer.WaitForGpuIdle(); // The previous frame may still read the old buffers.
    }
    impl.vertexBuffer.reset();
    impl.indexBuffer.reset();
    impl.indexCount = 0;
    if (vertices.empty() || indices.size() < 3) {
        return false;
    }
    try {
        impl.vertexBuffer.emplace(impl.renderer.CreateDeviceLocalBuffer(
            vertices.data(), vertices.size_bytes(), BufferUsage::VertexBuffer, "MeshPreviewVertices"));
        impl.indexBuffer.emplace(impl.renderer.CreateDeviceLocalBuffer(
            indices.data(), indices.size_bytes(), BufferUsage::IndexBuffer, "MeshPreviewIndices"));
    } catch (const std::exception& e) {
        impl.vertexBuffer.reset();
        impl.indexBuffer.reset();
        GTE_LOG_ERROR(kLogCategory, std::string("Mesh upload failed: ") + e.what());
        return false;
    }
    impl.indexCount = static_cast<std::uint32_t>(indices.size());
    return true;
}

UiTextureHandle MeshPreviewRenderer::Render(const Mat4& model, const Mat4& viewProjection, int width, int height,
    const std::array<std::uint8_t, 4>& clearColorRgba)
{
    Impl& impl = *m_impl;
    if (!impl.vertexBuffer.has_value() || !impl.indexBuffer.has_value()) {
        return UiTextureHandle::Invalid;
    }
    if (!impl.EnsureTarget(width < 1 ? 1 : width, height < 1 ? 1 : height)) {
        return UiTextureHandle::Invalid;
    }

    const PreviewPush push{ model, viewProjection };
    impl.renderer.Clear(clearColorRgba[0], clearColorRgba[1], clearColorRgba[2], clearColorRgba[3]);

    const Pipeline& pipeline = *impl.pipeline;
    const VkDescriptorSet setOne = impl.setOne->Native();
    const Buffer& vertexBuffer = *impl.vertexBuffer;
    const Buffer& indexBuffer = *impl.indexBuffer;
    const std::uint32_t indexCount = impl.indexCount;
    const Extent2D extent = impl.target->Extent();

    // No GPU timing slot: this preview must not overwrite the Profiler's Game/Scene View timings.
    impl.renderer.RenderOffscreen(*impl.target, std::nullopt, [&](rg::CommandBuffer& cmd) {
        cmd.SetViewportAndScissor(extent.width, extent.height);
        cmd.BindPipeline(pipeline);
        cmd.BindDescriptorSet(/*setIndex=*/1, setOne);
        cmd.PushConstants(push, ShaderStageMask::Vertex);
        cmd.BindVertexBuffer(vertexBuffer);
        cmd.BindIndexBuffer(indexBuffer);
        cmd.DrawIndexed(indexCount);
    });
    return impl.uiHandle;
}

int MeshPreviewRenderer::Width() const noexcept
{
    return m_impl->targetWidth;
}

int MeshPreviewRenderer::Height() const noexcept
{
    return m_impl->targetHeight;
}

} // namespace gte
