#include "MeshPreviewRenderer.h"

#include "Buffer.h"
#include "DescriptorSet.h"
#include "Pipeline.h"
#include "Renderer.h"
#include "RenderTexture.h"
#include "SceneServicesDescriptorSet.h"
#include "ShaderFile.h"
#include "RenderGraph/CommandBuffer.h"
#include "../Core/Logging.h"
#include "../Core/PipelineDesc.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
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

// Covers the whole scene lighting block of MeshPreview.frag; all zeros selects the default light.
constexpr std::uint64_t kDummyUniformBlockSize = kSceneGlobalUniformBlockSize;

} // namespace

MeshPreviewData BuildMeshPreviewData(std::span<const Vec3> positions, std::span<const Vec3> normals)
{
    MeshPreviewData data;
    if (positions.empty()) {
        return data;
    }
    const bool hasNormals = normals.size() == positions.size();
    data.vertices.resize(positions.size());
    Vec3 minBounds = positions[0];
    Vec3 maxBounds = positions[0];
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Vec3& p = positions[i];
        const Vec3 n = hasNormals ? normals[i] : Vec3::Up();
        data.vertices[i] = MeshPreviewVertex{ { p.x, p.y, p.z }, { n.x, n.y, n.z } };
        minBounds = Vec3(std::min(minBounds.x, p.x), std::min(minBounds.y, p.y), std::min(minBounds.z, p.z));
        maxBounds = Vec3(std::max(maxBounds.x, p.x), std::max(maxBounds.y, p.y), std::max(maxBounds.z, p.z));
    }
    data.center = (minBounds + maxBounds) * 0.5f;
    float radius = 0.0f;
    for (const Vec3& p : positions) {
        radius = std::max(radius, Length(p - data.center));
    }
    data.radius = radius > kEpsilon ? radius : 1.0f;
    return data;
}

struct MeshPreviewRenderer::Impl {
    // Descriptor set layout made by the Renderer; destroyed with the owner.
    struct LayoutOwner {
        LayoutOwner(Renderer& owner, std::span<const DescriptorBindingDesc> bindings)
            : renderer(owner)
            , handle(owner.CreateDescriptorSetLayout(bindings))
        {
        }
        ~LayoutOwner() { renderer.DestroyDescriptorSetLayout(handle); }
        LayoutOwner(const LayoutOwner&) = delete;
        LayoutOwner& operator=(const LayoutOwner&) = delete;

        Renderer& renderer;
        DescriptorSetLayoutHandle handle;
    };

    // Descriptor set from the Renderer pool plus its registry handle; both are released with the owner.
    struct SetOwner {
        SetOwner(Renderer& owner, DescriptorSetLayoutHandle layout)
            : renderer(owner)
            , set(owner.AllocateDescriptorSet(layout))
            , handle(RegisterDescriptorSet(set.Native()))
        {
        }
        ~SetOwner()
        {
            UnregisterDescriptorSet(handle);
            renderer.FreeDescriptorSet(set.Native());
        }
        SetOwner(const SetOwner&) = delete;
        SetOwner& operator=(const SetOwner&) = delete;

        Renderer& renderer;
        DescriptorSet set;
        DescriptorSetHandle handle;
    };

    // The UI texture handle of the current target; released on Reset() and on destruction.
    struct UiHandleOwner {
        explicit UiHandleOwner(Renderer& owner) noexcept
            : renderer(owner)
        {
        }
        ~UiHandleOwner() { Reset(); }
        UiHandleOwner(const UiHandleOwner&) = delete;
        UiHandleOwner& operator=(const UiHandleOwner&) = delete;

        void Reset() noexcept
        {
            if (handle != UiTextureHandle::Invalid) {
                renderer.ReleaseUiTextureHandle(handle);
                handle = UiTextureHandle::Invalid;
            }
        }
        // Reset() first: the old handle must be released before a new one is made, and Set() never leaks one.
        void Set(UiTextureHandle value) noexcept
        {
            assert(handle == UiTextureHandle::Invalid && "UiHandleOwner::Set: call Reset() first");
            handle = value;
        }

        Renderer& renderer;
        UiTextureHandle handle = UiTextureHandle::Invalid;
    };

    explicit Impl(Renderer& owner)
        : renderer(owner)
        , uiHandle(owner)
    {
    }

    ~Impl() { renderer.IdleForTeardown("mesh preview"); }

    // Set 1 holds only the lighting block; one layout serves the set and the pipeline.
    void Build()
    {
        constexpr DescriptorBindingDesc kSetOneBindings[] = {
            { .binding = 8, .kind = DescriptorKind::UniformBuffer, .stages = ShaderStageMask::Fragment },
        };
        zeroLayout.emplace(renderer, std::span<const DescriptorBindingDesc>{});
        setOneLayout.emplace(renderer, kSetOneBindings);
        setOne.emplace(renderer, setOneLayout->handle);

        uniformBuffer.emplace(renderer.CreateBuffer(
            kDummyUniformBlockSize, BufferUsage::UniformBuffer, BufferMemoryUsage::CpuToGpu, "MeshPreviewDummyUniformBlock"));
        const std::array<std::byte, kDummyUniformBlockSize> zeroBytes{};
        uniformBuffer->Upload(zeroBytes.data(), zeroBytes.size());

        setOne->set.Rewrite(std::vector<DescriptorWrite>{
            DescriptorWrite::UniformBuffer(8, uniformBuffer->Native(), 0, kDummyUniformBlockSize),
        });

        const DescriptorSetLayoutHandle extraSets[] = { zeroLayout->handle, setOneLayout->handle };
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
            if (!target.has_value()) {
                target.emplace(
                    renderer.CreateRenderTexture(width, height, PixelFormat::Undefined, "MeshPreview", "MeshPreviewDepth"));
            } else {
                // Resize idles the GPU first; only then is the old ImGui descriptor safe to free.
                renderer.ResizeRenderTexture(*target, width, height);
            }
            uiHandle.Reset();
            targetWidth = width;
            targetHeight = height;
            uiHandle.Set(renderer.GetUiTextureHandle(*target));
            return true;
        } catch (const std::exception& e) {
            uiHandle.Reset();
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

    // Members are destroyed bottom-up: the UI handle goes before its target, the set before its layout.
    Renderer& renderer;
    std::optional<LayoutOwner> zeroLayout;
    std::optional<LayoutOwner> setOneLayout;
    std::optional<SetOwner> setOne;
    std::optional<Buffer> uniformBuffer;
    std::optional<Pipeline> pipeline;

    std::optional<Buffer> vertexBuffer;
    std::optional<Buffer> indexBuffer;
    std::uint32_t indexCount = 0;

    std::optional<RenderTexture> target;
    UiHandleOwner uiHandle;
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
    return std::make_unique<MeshPreviewRenderer>(CreateKey{}, std::move(impl));
}

MeshPreviewRenderer::MeshPreviewRenderer(CreateKey, std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

MeshPreviewRenderer::~MeshPreviewRenderer() = default;

bool MeshPreviewRenderer::SetMesh(std::span<const MeshPreviewVertex> vertices, std::span<const std::uint32_t> indices)
{
    Impl& impl = *m_impl;
    if (impl.vertexBuffer.has_value() || impl.indexBuffer.has_value()) {
        // The previous frame may still read the old buffers; keep them if the GPU cannot be drained.
        if (impl.renderer.WaitForGpuIdle(BlockSite::ResourceRelease, "mesh preview buffers") != BlockOutcome::Completed) {
            return false;
        }
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
    const std::array<float, 4> clearColor{ clearColorRgba[0] / 255.0f, clearColorRgba[1] / 255.0f,
        clearColorRgba[2] / 255.0f, clearColorRgba[3] / 255.0f };

    const Pipeline& pipeline = *impl.pipeline;
    const DescriptorSetHandle setOneHandle = impl.setOne->handle;
    const Buffer& vertexBuffer = *impl.vertexBuffer;
    const Buffer& indexBuffer = *impl.indexBuffer;
    const std::uint32_t indexCount = impl.indexCount;
    const Extent2D extent = impl.target->Extent();

    impl.renderer.RenderOffscreen(*impl.target, clearColor, [&](rg::CommandBuffer& cmd) {
        cmd.SetViewportAndScissor(extent.width, extent.height);
        cmd.BindPipeline(pipeline);
        cmd.BindDescriptorSet(/*setIndex=*/1, setOneHandle);
        cmd.PushConstants(push, ShaderStageMask::Vertex);
        cmd.BindVertexBuffer(vertexBuffer);
        cmd.BindIndexBuffer(indexBuffer);
        cmd.DrawIndexed(indexCount);
    });
    return impl.uiHandle.handle;
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
