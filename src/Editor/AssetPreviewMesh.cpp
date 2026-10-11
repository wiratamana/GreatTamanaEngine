#include "AssetPreviewMesh.h"

#include "ProjectPanelData.h" // Utf8ToPath()
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()
#include "../Assets/MeshFile.h" // DecodeMeshDataFromBytes()
#include "../Core/PipelineDesc.h"
#include "../Math/Quat.h"
#include "../Renderer/Buffer.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/CommandBuffer.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/Texture2D.h"
#include "../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace gte {

namespace {

// Per-vertex layout THIS preview's own pipeline expects - position + normal,
// tightly packed (24 bytes/vertex), byte-identical to gte::MeshVertex
// (src/Renderer/MeshVertex.h) - see AssetPreviewMesh.h's class comment.
struct PreviewVertex {
    float position[3];
    float normal[3];
};

std::vector<std::byte> ReadShaderFileBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "AssetPreviewMesh: failed to open shader file '" + path + "' - was it compiled? See cmake/CompileShaders.cmake.");
    }
    const std::size_t size = static_cast<std::size_t>(file.tellg());
    std::vector<std::byte> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    return buffer;
}

} // namespace

AssetPreviewMesh::~AssetPreviewMesh()
{
    Reset();
}

void AssetPreviewMesh::Reset()
{
    if (m_device != VK_NULL_HANDLE
        && (m_vertexBuffer || m_indexBuffer || m_renderTexture || m_descriptor != VK_NULL_HANDLE || m_pipeline
            || m_localDescriptorPool != VK_NULL_HANDLE)) {
        // Same "stall before releasing, this is a rare user-driven event
        // not a per-frame cost" reasoning as AssetPreviewTexture::Reset().
        vkDeviceWaitIdle(m_device);
    }

    if (m_descriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_descriptor);
        m_descriptor = VK_NULL_HANDLE;
    }
    m_renderTexture.reset();
    m_vertexBuffer.reset();
    m_indexBuffer.reset();
    m_vertexCount = 0;
    m_indexCount = 0;
    m_texWidth = 0;
    m_texHeight = 0;
    m_cachedPath.clear();
    m_cachedWriteTime = std::filesystem::file_time_type{};
    m_cachedIsValid = false;

    if (m_renderer) {
        m_renderer->DestroyDescriptorSetLayout(m_setOneLayoutHandle);
        m_renderer->DestroyDescriptorSetLayout(m_setZeroFillerLayoutHandle);
    }
    m_setOneLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    m_setZeroFillerLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    m_pipeline.reset();

    // Local set=1 lighting/shadow stand-in - owned entirely by this class,
    // never routed through Renderer::AllocateComputeDescriptorSet(). The
    // pool destroy implicitly frees m_localSetOneDescriptorSet.
    m_localWhiteTexture.reset();
    m_localUniformBuffer.reset();
    if (m_localDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_localDescriptorPool, nullptr);
        m_localDescriptorPool = VK_NULL_HANDLE;
    }
    m_localSetOneDescriptorSet = VK_NULL_HANDLE;
    if (m_localSetOneLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_localSetOneLayout, nullptr);
        m_localSetOneLayout = VK_NULL_HANDLE;
    }
}

void AssetPreviewMesh::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline) {
        return;
    }

    const VkDevice device = m_device;
    m_renderer = &renderer;

    // Local set=1 lighting/shadow stand-in (MeshPreview.frag samples
    // DirectionalLightingAndReceiverMask.glsl's shared interface) - a real,
    // writable descriptor set the opaque pipeline API cannot allocate, so
    // it is still built by hand: binding 0 (combined image sampler) and
    // binding 8 (uniform buffer).
    m_localSetOneLayout = DescriptorSetLayoutBuilder(device)
                               .AddCombinedImageSampler(0, VK_SHADER_STAGE_FRAGMENT_BIT)
                               .AddUniformBuffer(8, VK_SHADER_STAGE_FRAGMENT_BIT)
                               .Build();

    VkDescriptorPoolSize localPoolSizes[2]{};
    localPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    localPoolSizes[0].descriptorCount = 1;
    localPoolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    localPoolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo localPoolInfo{};
    localPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    localPoolInfo.maxSets = 1;
    localPoolInfo.poolSizeCount = static_cast<std::uint32_t>(std::size(localPoolSizes));
    localPoolInfo.pPoolSizes = localPoolSizes;
    if (vkCreateDescriptorPool(device, &localPoolInfo, nullptr, &m_localDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("AssetPreviewMesh: vkCreateDescriptorPool (local set=1) failed.");
    }

    VkDescriptorSetAllocateInfo localAllocInfo{};
    localAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    localAllocInfo.descriptorPool = m_localDescriptorPool;
    localAllocInfo.descriptorSetCount = 1;
    localAllocInfo.pSetLayouts = &m_localSetOneLayout;
    if (vkAllocateDescriptorSets(device, &localAllocInfo, &m_localSetOneDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("AssetPreviewMesh: vkAllocateDescriptorSets (local set=1) failed.");
    }

    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    m_localWhiteTexture =
        std::make_unique<Texture2D>(renderer.CreateTexture2D(whitePixel, 1, 1, "AssetPreviewMeshDummyWhite"));

    // Zero-filled - must be at least as large as the shared
    // SceneGlobalUniformBlock (112 bytes, std140); 128 matches Scene
    // Services binding 8's own ceiling, purely coincidentally - this
    // class never includes SceneServicesDescriptorSet.h.
    constexpr VkDeviceSize kLocalDummyUniformBlockSize = 128;
    m_localUniformBuffer = std::make_unique<Buffer>(renderer.CreateBuffer(kLocalDummyUniformBlockSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, BufferMemoryUsage::CpuToGpu, "AssetPreviewMeshDummyUniformBlock"));
    const std::array<std::byte, kLocalDummyUniformBlockSize> zeroBytes{};
    m_localUniformBuffer->Upload(zeroBytes.data(), zeroBytes.size());

    VkDescriptorImageInfo localImageInfo{};
    localImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    localImageInfo.imageView = m_localWhiteTexture->View();
    localImageInfo.sampler = m_localWhiteTexture->Sampler();

    VkDescriptorBufferInfo localBufferInfo{};
    localBufferInfo.buffer = m_localUniformBuffer->Native();
    localBufferInfo.offset = 0;
    localBufferInfo.range = kLocalDummyUniformBlockSize;

    VkWriteDescriptorSet localWrites[2]{};
    localWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    localWrites[0].dstSet = m_localSetOneDescriptorSet;
    localWrites[0].dstBinding = 0;
    localWrites[0].descriptorCount = 1;
    localWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    localWrites[0].pImageInfo = &localImageInfo;
    localWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    localWrites[1].dstSet = m_localSetOneDescriptorSet;
    localWrites[1].dstBinding = 8;
    localWrites[1].descriptorCount = 1;
    localWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    localWrites[1].pBufferInfo = &localBufferInfo;
    vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(std::size(localWrites)), localWrites, 0, nullptr);

    // Opaque-handle twin of m_localSetOneLayout above, fed to the pipeline
    // builder below - identically-defined layouts are pipeline-layout-
    // compatible with the real m_localSetOneDescriptorSet bound at draw time.
    constexpr DescriptorBindingDesc kSetOneBindings[] = {
        { .binding = 0, .kind = DescriptorKind::CombinedImageSampler, .stages = ShaderStageMask::Fragment },
        { .binding = 8, .kind = DescriptorKind::UniformBuffer, .stages = ShaderStageMask::Fragment },
    };
    m_setZeroFillerLayoutHandle = renderer.CreateDescriptorSetLayout({});
    m_setOneLayoutHandle = renderer.CreateDescriptorSetLayout(kSetOneBindings);
    const DescriptorSetLayoutHandle extraSets[] = { m_setZeroFillerLayoutHandle, m_setOneLayoutHandle };

    PipelineDesc desc = PipelinePreset::Opaque(renderer.ColorPixelFormat(), renderer.DepthPixelFormat());
    desc.vertexLayout = VertexLayout::PositionNormal;
    desc.cullMode = CullMode::None; // An imported .pmx's winding isn't guaranteed - show the shape, don't cull it.
    desc.layout.extraSetLayouts = extraSets;
    desc.layout.pushConstantByteSize = sizeof(float) * 32; // model + viewProj, matches MeshPreview.vert.
    desc.layout.pushConstantStages = ShaderStageMask::Vertex;

    const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/MeshPreview.vert.spv");
    const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/MeshPreview.frag.spv");
    m_pipeline = std::make_unique<Pipeline>(renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
}

bool AssetPreviewMesh::EnsureMeshUploaded(Renderer& renderer, const std::string& absolutePath)
{
    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime =
        std::filesystem::last_write_time(Utf8ToPath(absolutePath), timeEc);

    if (!absolutePath.empty() && absolutePath == m_cachedPath && !timeEc && writeTime == m_cachedWriteTime) {
        return true; // Unchanged since last call - whatever m_cachedIsValid says still holds.
    }

    // Release only the mesh-specific GPU state (buffers) - NOT the
    // pipeline/RenderTexture/descriptor, which are reused across different
    // selected mesh assets. A full Reset() would be wrong here.
    if (m_device != VK_NULL_HANDLE && (m_vertexBuffer || m_indexBuffer)) {
        vkDeviceWaitIdle(m_device);
    }
    m_vertexBuffer.reset();
    m_indexBuffer.reset();
    m_vertexCount = 0;
    m_indexCount = 0;

    m_cachedPath = absolutePath;
    m_cachedIsValid = false;
    if (!timeEc) {
        m_cachedWriteTime = writeTime;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8ToPath(absolutePath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return true; // Not a (valid) Mesh *.gta - m_cachedIsValid stays false.
    }

    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty() || mesh->indices.size() < 3) {
        return true;
    }

    std::vector<PreviewVertex> vertices(mesh->positions.size());
    const bool hasNormals = mesh->normals.size() == mesh->positions.size();
    Vec3 minBounds = mesh->positions[0];
    Vec3 maxBounds = mesh->positions[0];
    for (std::size_t i = 0; i < mesh->positions.size(); ++i) {
        const Vec3& p = mesh->positions[i];
        vertices[i].position[0] = p.x;
        vertices[i].position[1] = p.y;
        vertices[i].position[2] = p.z;
        const Vec3 n = hasNormals ? mesh->normals[i] : Vec3::Up();
        vertices[i].normal[0] = n.x;
        vertices[i].normal[1] = n.y;
        vertices[i].normal[2] = n.z;

        minBounds.x = std::min(minBounds.x, p.x);
        minBounds.y = std::min(minBounds.y, p.y);
        minBounds.z = std::min(minBounds.z, p.z);
        maxBounds.x = std::max(maxBounds.x, p.x);
        maxBounds.y = std::max(maxBounds.y, p.y);
        maxBounds.z = std::max(maxBounds.z, p.z);
    }

    m_boundsCenter = (minBounds + maxBounds) * 0.5f;
    float radius = 0.0f;
    for (const Vec3& p : mesh->positions) {
        radius = std::max(radius, Length(p - m_boundsCenter));
    }
    m_boundsRadius = radius > kEpsilon ? radius : 1.0f;

    try {
        m_vertexBuffer = std::make_unique<Buffer>(renderer.CreateDeviceLocalBuffer(
            vertices.data(), vertices.size() * sizeof(PreviewVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            "AssetPreviewMeshVertices"));
        m_indexBuffer = std::make_unique<Buffer>(renderer.CreateDeviceLocalBuffer(mesh->indices.data(),
            mesh->indices.size() * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            "AssetPreviewMeshIndices"));
    } catch (const std::exception&) {
        m_vertexBuffer.reset();
        m_indexBuffer.reset();
        return true; // m_cachedIsValid stays false.
    }

    m_vertexCount = static_cast<std::uint32_t>(vertices.size());
    m_indexCount = static_cast<std::uint32_t>(mesh->indices.size());
    m_cachedIsValid = true;
    return true;
}

void AssetPreviewMesh::EnsureRenderTexture(Renderer& renderer, int width, int height)
{
    if (m_renderTexture && m_texWidth == width && m_texHeight == height) {
        return;
    }

    if (m_device != VK_NULL_HANDLE && (m_renderTexture || m_descriptor != VK_NULL_HANDLE)) {
        vkDeviceWaitIdle(m_device);
    }
    if (m_descriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_descriptor);
        m_descriptor = VK_NULL_HANDLE;
    }

    if (!m_renderTexture) {
        m_renderTexture = std::make_unique<RenderTexture>(
            renderer.CreateRenderTexture(width, height, VK_FORMAT_UNDEFINED, "AssetPreviewMesh", "AssetPreviewMeshDepth"));
    } else {
        m_renderTexture->Resize(width, height);
    }
    m_texWidth = width;
    m_texHeight = height;

    m_descriptor = ImGui_ImplVulkan_AddTexture(
        m_renderTexture->Sampler(), m_renderTexture->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

std::optional<AssetPreviewMesh::Preview> AssetPreviewMesh::Render(
    Renderer& renderer, const std::string& absolutePath, int viewportWidth, int viewportHeight)
{
    m_device = renderer.GetVulkanContextInfo().device;

    if (!EnsureMeshUploaded(renderer, absolutePath) || !m_cachedIsValid) {
        return std::nullopt;
    }

    EnsurePipeline(renderer);

    const int width = std::max(1, viewportWidth);
    const int height = std::max(1, viewportHeight);
    EnsureRenderTexture(renderer, width, height);

    // Slowly spins the mesh around its own up axis, driven directly by
    // elapsed time - no per-frame state to track (see class comment). The
    // camera itself never moves; recentering via -m_boundsCenter (applied
    // BEFORE the spin - see Mat4's "A*B applies B first" composition
    // convention, Math/Mat4.h) is what keeps an off-center mesh spinning
    // around its own visual middle rather than orbiting around whatever
    // arbitrary point its original modeling package considered the origin.
    constexpr float kSpinDegreesPerSecond = 24.0f;
    const float angleDeg = std::fmod(static_cast<float>(ImGui::GetTime()) * kSpinDegreesPerSecond, 360.0f);
    const Quat spin = Quat::FromAxisAngle(Vec3::Up(), DegToRad(angleDeg));
    const Mat4 model = spin.ToMat4() * Mat4::Translation(-m_boundsCenter);

    const float radius = m_boundsRadius;
    const float fovYRadians = DegToRad(45.0f);
    // Distance that puts the whole bounding sphere inside the vertical FOV,
    // plus a margin so it doesn't touch the panel's edges - see class
    // comment for why this doesn't need to be a pixel-perfect Unity-style
    // fit.
    const float distance = (radius / std::tan(fovYRadians * 0.5f)) * 1.35f;
    const Vec3 eye(0.0f, radius * 0.35f, -distance);
    const Vec3 target(0.0f, radius * 0.05f, 0.0f);
    const Mat4 view = Mat4::LookAtLH(eye, target, Vec3::Up());
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const Mat4 proj =
        Mat4::PerspectiveFovLH_ZO(fovYRadians, aspect, std::max(0.01f, radius * 0.02f), distance + radius * 4.0f, /*flipY=*/true);
    const Mat4 viewProj = proj * view;

    struct PushConstants {
        float model[16];
        float viewProj[16];
    } pushConstants;
    std::memcpy(pushConstants.model, model.Data(), sizeof(pushConstants.model));
    std::memcpy(pushConstants.viewProj, viewProj.Data(), sizeof(pushConstants.viewProj));

    // Neutral dark backdrop, matching AssetPreviewTexture/BuildTextureViewer's
    // own viewer background (see Panels/InspectorPanel.cpp) - this Clear()
    // only affects THIS RenderOffscreen() call immediately below (see this
    // class's own header comment for why sharing Renderer's one Clear()
    // color with Game's own Clear() calls is safe: each Clear()+
    // RenderOffscreen()/Present() pair this engine ever does happens
    // synchronously back-to-back, never interleaved).
    renderer.Clear(35, 35, 35, 255);

    const VkPipeline pipeline = m_pipeline->Native();
    const VkPipelineLayout layout = m_pipeline->Layout();
    const VkDescriptorSet localSetOne = m_localSetOneDescriptorSet;
    const VkBuffer vertexBuffer = m_vertexBuffer->Native();
    const VkBuffer indexBuffer = m_indexBuffer->Native();
    const std::uint32_t indexCount = m_indexCount;
    const VkExtent2D extent = m_renderTexture->Extent();

    // Phase 4C (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - std::nullopt:
    // this Inspector mesh preview is not one of the Profiler's three named
    // passes, and must never silently share a query slot with (or overwrite
    // the cached timing of) "Game View"/"Scene View" - see
    // Renderer::RenderOffscreen()'s own doc comment.
    renderer.RenderOffscreen(*m_renderTexture, std::nullopt, [&](rg::CommandBuffer& cmd) {
        VkViewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd.Native(), 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = extent;
        vkCmdSetScissor(cmd.Native(), 0, 1, &scissor);

        vkCmdBindPipeline(cmd.Native(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(
            cmd.Native(), VK_PIPELINE_BIND_POINT_GRAPHICS, layout, /*firstSet=*/1, 1, &localSetOne, 0, nullptr);
        vkCmdPushConstants(cmd.Native(), layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), &pushConstants);

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd.Native(), 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd.Native(), indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd.Native(), indexCount, 1, 0, 0, 0);
    });

    Preview preview;
    preview.descriptor = m_descriptor;
    preview.width = m_texWidth;
    preview.height = m_texHeight;
    preview.vertexCount = m_vertexCount;
    preview.triangleCount = m_indexCount / 3;
    return preview;
}

} // namespace gte
