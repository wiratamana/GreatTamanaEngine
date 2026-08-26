#include "AssetPreviewMesh.h"

#include "ProjectPanelData.h" // Utf8ToPath()
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()
#include "../Assets/MeshFile.h" // DecodeMeshDataFromBytes()
#include "../Math/Quat.h"
#include "../Renderer/Buffer.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderTexture.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace gte {

namespace {

// Per-vertex layout THIS preview's own pipeline expects - position + normal,
// tightly packed (24 bytes/vertex). Deliberately separate from this
// engine's shared position+color gte::Vertex (src/Renderer/Vertex.h) - see
// AssetPreviewMesh.h's class comment for why.
struct PreviewVertex {
    float position[3];
    float normal[3];
};

std::vector<char> ReadShaderFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "AssetPreviewMesh: failed to open shader file '" + path + "' - was it compiled? See cmake/CompileShaders.cmake.");
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
        throw std::runtime_error("AssetPreviewMesh: vkCreateShaderModule failed.");
    }
    return module;
}

bool DepthFormatHasStencil(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT;
}

} // namespace

AssetPreviewMesh::~AssetPreviewMesh()
{
    Reset();
}

void AssetPreviewMesh::Reset()
{
    if (m_device != VK_NULL_HANDLE
        && (m_vertexBuffer || m_indexBuffer || m_renderTexture || m_descriptor != VK_NULL_HANDLE
            || m_pipeline != VK_NULL_HANDLE)) {
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

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
}

void AssetPreviewMesh::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline != VK_NULL_HANDLE) {
        return;
    }

    const VkDevice device = m_device;
    const VkFormat colorFormat = renderer.ColorFormat();
    const VkFormat depthFormat = renderer.DepthFormat();

    const std::vector<char> vertSpirv = ReadShaderFile("shaders/MeshPreview.vert.spv");
    const std::vector<char> fragSpirv = ReadShaderFile("shaders/MeshPreview.frag.spv");

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

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(PreviewVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(PreviewVertex, position);
        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(PreviewVertex, normal);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

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
        // No backface culling - an imported .pmx's winding convention isn't
        // guaranteed to match this engine's own, and this preview's whole
        // point is "show me the shape", not enforce a winding convention.
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
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

        // Same push-constant SHAPE as this engine's main Pipeline (model
        // then viewProj, 128 bytes total) - see MeshPreview.vert.
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float) * 32;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("AssetPreviewMesh: vkCreatePipelineLayout failed.");
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
            throw std::runtime_error("AssetPreviewMesh: vkCreateGraphicsPipelines failed.");
        }
    } catch (...) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
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

    const VkPipeline pipeline = m_pipeline;
    const VkPipelineLayout layout = m_pipelineLayout;
    const VkBuffer vertexBuffer = m_vertexBuffer->Native();
    const VkBuffer indexBuffer = m_indexBuffer->Native();
    const std::uint32_t indexCount = m_indexCount;
    const VkExtent2D extent = m_renderTexture->Extent();

    // Phase 4C (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - std::nullopt:
    // this Inspector mesh preview is not one of the Profiler's three named
    // passes, and must never silently share a query slot with (or overwrite
    // the cached timing of) "Game View"/"Scene View" - see
    // Renderer::RenderOffscreen()'s own doc comment.
    renderer.RenderOffscreen(*m_renderTexture, std::nullopt, [&](VkCommandBuffer cmd) {
        VkViewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), &pushConstants);

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
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
