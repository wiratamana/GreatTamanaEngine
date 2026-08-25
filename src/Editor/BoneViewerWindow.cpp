#include "BoneViewerWindow.h"

#include "ProjectPanelData.h" // Utf8ToPath()
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()
#include "../Assets/MeshFile.h" // DecodeMeshDataFromBytes()
#include "../Assets/RigFile.h" // DecodeRigDataFromBytes()
#include "../ECS/Components/MeshAssetSource.h"
#include "../ECS/Registry.h"
#include "../Math/Mat4.h"
#include "../Math/Vec4.h"
#include "../Renderer/Buffer.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderTexture.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace gte {

namespace {

// Per-vertex layout this window's own pipeline expects - position + normal,
// tightly packed (24 bytes/vertex). Identical shape to (and reuses the same
// compiled shader pair as) AssetPreviewMesh.cpp's own PreviewVertex - see
// BoneViewerWindow.h's class comment for why sharing MeshPreview.vert/.frag
// between the two is deliberate rather than needing a new shader pair.
struct PreviewVertex {
    float position[3];
    float normal[3];
};

std::vector<char> ReadShaderFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "BoneViewerWindow: failed to open shader file '" + path + "' - was it compiled? See cmake/CompileShaders.cmake.");
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
        throw std::runtime_error("BoneViewerWindow: vkCreateShaderModule failed.");
    }
    return module;
}

bool DepthFormatHasStencil(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT;
}

// Projects a model-space point (the loaded mesh/skeleton uses an identity
// model matrix - see BoneViewerWindow.h's class comment - so "model-space"
// and "world-space" are the same thing here) through `viewProj` into a
// pixel coordinate inside [rectMin, rectMax) - the SAME rect the rendered
// RenderTexture is displayed at via ImGui::Image(), so a bone gizmo drawn
// at the returned position always lines up with the mesh actually visible
// underneath it. Returns false (does not write outScreen) for a point
// behind the camera (clip.w <= 0), which should simply not be drawn at all
// rather than plotted at some nonsensical mirrored position.
bool ProjectToScreen(const Vec3& modelPos, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectMax, ImVec2& outScreen)
{
    const Vec4 clip = viewProj * Vec4(modelPos, 1.0f);
    if (clip.w <= 0.0001f) {
        return false;
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float width = rectMax.x - rectMin.x;
    const float height = rectMax.y - rectMin.y;
    outScreen.x = rectMin.x + (ndcX * 0.5f + 0.5f) * width;
    outScreen.y = rectMin.y + (ndcY * 0.5f + 0.5f) * height;
    return true;
}

std::string ToLower(const std::string& s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace

BoneViewerWindow::~BoneViewerWindow()
{
    Reset();
}

void BoneViewerWindow::Open(Entity rootEntity) noexcept
{
    m_open = true;
    m_targetEntity = rootEntity;
}

void BoneViewerWindow::Reset()
{
    if (m_device != VK_NULL_HANDLE
        && (m_vertexBuffer || m_indexBuffer || m_renderTexture || m_descriptor != VK_NULL_HANDLE
            || m_pipeline != VK_NULL_HANDLE)) {
        // Same "stall before releasing, this is a rare user-driven event
        // not a per-frame cost" reasoning as AssetPreviewMesh::Reset().
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
    m_bones.clear();
    m_boneChildren.clear();
    m_rootBoneIndices.clear();
    m_selectedBoneIndex = -1;
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

void BoneViewerWindow::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline != VK_NULL_HANDLE) {
        return;
    }

    const VkDevice device = m_device;
    const VkFormat colorFormat = renderer.ColorFormat();
    const VkFormat depthFormat = renderer.DepthFormat();

    // Reuses the exact same compiled shader pair as AssetPreviewMesh - see
    // BoneViewerWindow.h's class comment.
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
        // No backface culling - same reasoning as AssetPreviewMesh: an
        // imported .pmx's winding convention isn't guaranteed to match this
        // engine's own, and this viewer's whole point is "show me the
        // bones against the shape", not enforce a winding convention.
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

        // Same push-constant shape as AssetPreviewMesh/this engine's main
        // Pipeline (model then viewProj, 128 bytes total).
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float) * 32;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("BoneViewerWindow: vkCreatePipelineLayout failed.");
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
            throw std::runtime_error("BoneViewerWindow: vkCreateGraphicsPipelines failed.");
        }
    } catch (...) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

bool BoneViewerWindow::EnsureDataLoaded(Renderer& renderer, const std::string& absoluteGtaPath)
{
    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(Utf8ToPath(absoluteGtaPath), timeEc);

    if (!absoluteGtaPath.empty() && absoluteGtaPath == m_cachedPath && !timeEc && writeTime == m_cachedWriteTime) {
        return true; // Unchanged since last call - whatever m_cachedIsValid says still holds.
    }

    if (m_device != VK_NULL_HANDLE && (m_vertexBuffer || m_indexBuffer)) {
        vkDeviceWaitIdle(m_device);
    }
    m_vertexBuffer.reset();
    m_indexBuffer.reset();
    m_vertexCount = 0;
    m_indexCount = 0;
    m_bones.clear();
    m_boneChildren.clear();
    m_rootBoneIndices.clear();
    m_selectedBoneIndex = -1; // A bone index from whatever was loaded before means nothing for a new asset.

    m_cachedPath = absoluteGtaPath;
    m_cachedIsValid = false;
    if (!timeEc) {
        m_cachedWriteTime = writeTime;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8ToPath(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return true; // Not a (valid) Mesh *.gta - m_cachedIsValid stays false.
    }

    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty() || mesh->indices.size() < 3) {
        return true;
    }

    // Skeleton/bone data lives in the *.gta's METADATA section (see
    // RigFile.h) - a boneless/riggless mesh (or one imported before rig
    // extraction existed) simply has an empty metadata blob, in which case
    // m_bones is correctly left empty rather than treated as a failure.
    if (!gta->metadata.empty()) {
        if (const std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata); rig.has_value()) {
            m_bones.reserve(rig->skeleton.bones.size());
            for (const Bone& bone : rig->skeleton.bones) {
                m_bones.push_back(BoneEntry{ bone.name, bone.position, bone.parentBoneIndex });
            }
        }
    }
    RebuildBoneHierarchyIndex();

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
            "BoneViewerVertices"));
        m_indexBuffer = std::make_unique<Buffer>(renderer.CreateDeviceLocalBuffer(mesh->indices.data(),
            mesh->indices.size() * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "BoneViewerIndices"));
    } catch (const std::exception&) {
        m_vertexBuffer.reset();
        m_indexBuffer.reset();
        return true; // m_cachedIsValid stays false.
    }

    m_vertexCount = static_cast<std::uint32_t>(vertices.size());
    m_indexCount = static_cast<std::uint32_t>(mesh->indices.size());
    m_cachedIsValid = true;
    m_needsFraming = true; // A newly (re)loaded model - reframe the camera next Build() call.
    return true;
}

void BoneViewerWindow::EnsureRenderTexture(Renderer& renderer, int width, int height)
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
            renderer.CreateRenderTexture(width, height, VK_FORMAT_UNDEFINED, "BoneViewer", "BoneViewerDepth"));
    } else {
        m_renderTexture->Resize(width, height);
    }
    m_texWidth = width;
    m_texHeight = height;

    m_descriptor = ImGui_ImplVulkan_AddTexture(
        m_renderTexture->Sampler(), m_renderTexture->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void BoneViewerWindow::FrameCameraToBounds() noexcept
{
    m_camTarget = m_boundsCenter;
    m_camYawDeg = 20.0f;
    m_camPitchDeg = 12.0f;
    const float fovYRadians = DegToRad(45.0f);
    m_camDistance = std::max(0.01f, (m_boundsRadius / std::tan(fovYRadians * 0.5f)) * 1.6f);
}

Vec3 BoneViewerWindow::ComputeEyePosition() const noexcept
{
    const float yawRad = DegToRad(m_camYawDeg);
    const float pitchRad = DegToRad(m_camPitchDeg);
    const Vec3 dirFromTargetToEye(
        std::sin(yawRad) * std::cos(pitchRad), std::sin(pitchRad), -std::cos(yawRad) * std::cos(pitchRad));
    return m_camTarget + dirFromTargetToEye * m_camDistance;
}

void BoneViewerWindow::RebuildBoneHierarchyIndex()
{
    m_boneChildren.assign(m_bones.size(), {});
    m_rootBoneIndices.clear();
    for (std::size_t i = 0; i < m_bones.size(); ++i) {
        const std::int32_t parent = m_bones[i].parentIndex;
        if (parent >= 0 && static_cast<std::size_t>(parent) < m_bones.size()) {
            m_boneChildren[static_cast<std::size_t>(parent)].push_back(static_cast<std::int32_t>(i));
        } else {
            m_rootBoneIndices.push_back(static_cast<std::int32_t>(i));
        }
    }
}

bool BoneViewerWindow::BoneMatchesFilterRecursive(std::int32_t boneIndex, const std::string& lowerFilter, int depth) const
{
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()
        || depth > static_cast<int>(m_bones.size())) {
        return false;
    }
    if (ToLower(m_bones[static_cast<std::size_t>(boneIndex)].name).find(lowerFilter) != std::string::npos) {
        return true;
    }
    for (const std::int32_t child : m_boneChildren[static_cast<std::size_t>(boneIndex)]) {
        if (BoneMatchesFilterRecursive(child, lowerFilter, depth + 1)) {
            return true;
        }
    }
    return false;
}

void BoneViewerWindow::RenderBoneTreeNode(std::int32_t boneIndex, const std::string& lowerFilter, int depth)
{
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()
        || depth > static_cast<int>(m_bones.size())) {
        return;
    }

    // Unity-style search-box filtering: a bone with neither a matching name
    // NOR any matching descendant is hidden from the tree entirely, rather
    // than merely un-highlighted - keeps a deep search result actually
    // findable instead of buried under hundreds of unrelated rows.
    if (!lowerFilter.empty() && !BoneMatchesFilterRecursive(boneIndex, lowerFilter, 0)) {
        return;
    }

    const BoneEntry& bone = m_bones[static_cast<std::size_t>(boneIndex)];
    const std::vector<std::int32_t>& children = m_boneChildren[static_cast<std::size_t>(boneIndex)];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (m_selectedBoneIndex == boneIndex) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::string label = bone.name.empty() ? ("Bone " + std::to_string(boneIndex)) : bone.name;

    ImGui::PushID(boneIndex);
    const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        m_selectedBoneIndex = boneIndex;
        // Double-clicking a row re-centers the orbit camera on that bone
        // (keeping the current distance/angle) - a quick way to jump to a
        // bone buried deep in a large skeleton without hunting for its dot
        // in the 3D view first.
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_camTarget = bone.position;
        }
    }

    if (opened && !children.empty()) {
        for (const std::int32_t child : children) {
            RenderBoneTreeNode(child, lowerFilter, depth + 1);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void BoneViewerWindow::BuildBoneTreePane(const std::string& lowerFilter)
{
    if (m_bones.empty()) {
        ImGui::TextDisabled("(no bones)");
        return;
    }
    for (const std::int32_t root : m_rootBoneIndices) {
        RenderBoneTreeNode(root, lowerFilter, 0);
    }
}


void BoneViewerWindow::Build(Registry& registry, Renderer& renderer)
{
    if (!m_open) {
        return;
    }

    m_device = renderer.GetVulkanContextInfo().device;

    ImGui::SetNextWindowSize(ImVec2(900.0f, 650.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Bone Viewer", &m_open)) {
        ImGui::End();
        return;
    }

    if (!registry.IsAlive(m_targetEntity)) {
        ImGui::TextDisabled("The selected entity no longer exists.");
        ImGui::End();
        return;
    }

    const MeshAssetSource* source = registry.TryGetComponent<MeshAssetSource>(m_targetEntity);
    if (source == nullptr || source->gtaPath.empty()) {
        ImGui::TextDisabled("This entity has no associated mesh asset (MeshAssetSource) to inspect.");
        ImGui::End();
        return;
    }

    if (!EnsureDataLoaded(renderer, source->gtaPath) || !m_cachedIsValid) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load mesh/skeleton data for:");
        ImGui::TextWrapped("%s", source->gtaPath.c_str());
        ImGui::End();
        return;
    }

    if (m_needsFraming) {
        FrameCameraToBounds();
        m_needsFraming = false;
    }

    EnsurePipeline(renderer);

    // --- Toolbar -----------------------------------------------------------
    ImGui::PushItemWidth(240.0f);
    ImGui::InputTextWithHint("##BoneViewerSearch", "Search bones by name...", m_searchBuffer, sizeof(m_searchBuffer));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        FrameCameraToBounds();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show All Names", &m_showAllNames);
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%zu bones - %u verts / %u tris", m_bones.size(), m_vertexCount, m_indexCount / 3);
    if (m_bones.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
            "This model has no bone/skeleton data (a boneless mesh, or one imported before rig extraction existed).");
    }
    ImGui::Separator();

    const std::string lowerFilter = ToLower(std::string(m_searchBuffer));

    // --- Left pane: bone hierarchy tree, "starting from root" ---------------
    // Unity/Omniverse-Inspector-style: a real indented tree, walked from
    // m_rootBoneIndices down through m_boneChildren (see
    // RebuildBoneHierarchyIndex()) - the same "GetChildren()-based recursive
    // tree" shape as "Hierarchy"'s own entity tree (Panels/HierarchyPanel.cpp),
    // just for bones instead of entities.
    constexpr float kSplitterThickness = 6.0f;
    constexpr float kMinTreeWidth = 150.0f;
    constexpr float kMinViewportWidth = 200.0f;
    const float fullWidth = ImGui::GetContentRegionAvail().x;
    const float maxTreeWidth = std::max(kMinTreeWidth, fullWidth - kSplitterThickness - kMinViewportWidth);
    m_treeWidth = Clamp(m_treeWidth, kMinTreeWidth, maxTreeWidth);

    ImGui::BeginChild("BoneViewerTree", ImVec2(m_treeWidth, 0.0f), true);
    BuildBoneTreePane(lowerFilter);
    ImGui::EndChild();

    // The draggable splitter itself - same thin scrollbar-grip-styled button
    // as Panels/InspectorPanel.cpp's own preview splitter, just resizing
    // horizontally instead of vertically.
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrab));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabActive));
    ImGui::Button("##BoneViewerTreeSplitter", ImVec2(kSplitterThickness, -1.0f));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActive()) {
        m_treeWidth += ImGui::GetIO().MouseDelta.x;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImGui::SameLine();

    // --- Right pane: the live 3D viewport -------------------------------------
    ImGui::BeginChild(
        "BoneViewerViewport", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x >= 1.0f && avail.y >= 1.0f) {
        const int width = std::max(1, static_cast<int>(avail.x));
        const int height = std::max(1, static_cast<int>(avail.y));
        EnsureRenderTexture(renderer, width, height);

        const Vec3 eye = ComputeEyePosition();
        const Mat4 view = Mat4::LookAtLH(eye, m_camTarget, Vec3::Up());
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const float fovYRadians = DegToRad(45.0f);
        const float nearZ = std::max(0.01f, m_boundsRadius * 0.01f);
        const float farZ = m_camDistance + m_boundsRadius * 6.0f + 10.0f;
        const Mat4 proj = Mat4::PerspectiveFovLH_ZO(fovYRadians, aspect, nearZ, farZ, /*flipY=*/true);
        const Mat4 viewProj = proj * view;

        struct PushConstants {
            float model[16];
            float viewProj[16];
        } pushConstants;
        std::memcpy(pushConstants.model, Mat4::Identity().Data(), sizeof(pushConstants.model));
        std::memcpy(pushConstants.viewProj, viewProj.Data(), sizeof(pushConstants.viewProj));

        // Neutral dark backdrop - same reasoning as AssetPreviewMesh's own
        // Clear() call (this only affects THIS RenderOffscreen() call, see
        // that class's own comment for why sharing Renderer's one Clear()
        // color is safe).
        renderer.Clear(30, 32, 38, 255);

        const VkPipeline pipeline = m_pipeline;
        const VkPipelineLayout layout = m_pipelineLayout;
        const VkBuffer vertexBuffer = m_vertexBuffer->Native();
        const VkBuffer indexBuffer = m_indexBuffer->Native();
        const std::uint32_t indexCount = m_indexCount;
        const VkExtent2D extent = m_renderTexture->Extent();

        renderer.RenderOffscreen(*m_renderTexture, [&](VkCommandBuffer cmd) {
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

        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_descriptor)), avail);
        const ImVec2 imageMax(imageMin.x + avail.x, imageMin.y + avail.y);
        const bool hovered = ImGui::IsItemHovered();

        // Project every bone to screen space up front - shared by hit-
        // testing (hover/click below) AND the overlay drawing pass
        // (further below), computed with THIS frame's viewProj so
        // everything stays pixel-aligned with what was just rendered.
        std::vector<ImVec2> screenPositions(m_bones.size());
        std::vector<char> onScreen(m_bones.size(), 0);
        for (std::size_t i = 0; i < m_bones.size(); ++i) {
            ImVec2 screen;
            if (ProjectToScreen(m_bones[i].position, viewProj, imageMin, imageMax, screen)) {
                screenPositions[i] = screen;
                onScreen[i] = 1;
            }
        }

        // Nearest on-screen bone dot to the mouse cursor (within a small
        // pixel radius) - what both the hover-name-reveal and a direct
        // viewport click (below) hit-test against.
        int hoveredBoneIndex = -1;
        if (hovered) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            float hoveredDistSq = 144.0f; // 12px radius.
            for (std::size_t i = 0; i < m_bones.size(); ++i) {
                if (!onScreen[i]) {
                    continue;
                }
                const float dx = mousePos.x - screenPositions[i].x;
                const float dy = mousePos.y - screenPositions[i].y;
                const float distSq = dx * dx + dy * dy;
                if (distSq < hoveredDistSq) {
                    hoveredDistSq = distSq;
                    hoveredBoneIndex = static_cast<int>(i);
                }
            }
        }

        // --- Orbit camera input / direct-click bone selection (applied to
        // what NEXT frame renders - see this window's own class comment for
        // why this one-frame lag mirrors Panels/ScenePanel.cpp's
        // EditorCamera handling) ---------------------------------------------
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredBoneIndex >= 0) {
                // Clicked directly on a bone's gizmo dot - select it
                // (mirrors clicking its row in the tree pane) instead of
                // starting an orbit-camera drag.
                m_selectedBoneIndex = hoveredBoneIndex;
            } else {
                m_rotating = true;
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_rotating = false;
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            m_panning = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
            m_panning = false;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (m_rotating) {
            m_camYawDeg += io.MouseDelta.x * 0.3f;
            m_camPitchDeg = Clamp(m_camPitchDeg + io.MouseDelta.y * 0.3f, -85.0f, 85.0f);
        }
        if (m_panning) {
            const Vec3 forward = Normalize(m_camTarget - eye);
            Vec3 right = Normalize(Cross(Vec3::Up(), forward));
            if (LengthSquared(right) < kEpsilon) {
                right = Vec3::Right();
            }
            const Vec3 camUp = Cross(forward, right);
            const float panSpeed = m_camDistance * 0.0015f;
            m_camTarget += right * (-io.MouseDelta.x * panSpeed) + camUp * (io.MouseDelta.y * panSpeed);
        }
        if (hovered && io.MouseWheel != 0.0f) {
            m_camDistance = std::max(m_boundsRadius * 0.05f, m_camDistance - io.MouseWheel * (m_camDistance * 0.15f));
        }

        // --- Bone gizmo overlay ------------------------------------------------
        if (!m_bones.empty()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(imageMin, imageMax, true);

            // Lines to parent first, so every dot/label below always paints
            // over them.
            for (std::size_t i = 0; i < m_bones.size(); ++i) {
                const std::int32_t parent = m_bones[i].parentIndex;
                if (parent < 0 || static_cast<std::size_t>(parent) >= m_bones.size()) {
                    continue;
                }
                if (!onScreen[i] || !onScreen[static_cast<std::size_t>(parent)]) {
                    continue;
                }
                drawList->AddLine(screenPositions[static_cast<std::size_t>(parent)], screenPositions[i],
                    IM_COL32(70, 200, 100, 200), 2.0f);
            }

            for (std::size_t i = 0; i < m_bones.size(); ++i) {
                if (!onScreen[i]) {
                    continue;
                }
                const bool matchesFilter =
                    !lowerFilter.empty() && ToLower(m_bones[i].name).find(lowerFilter) != std::string::npos;
                const bool isHovered = hovered && (static_cast<int>(i) == hoveredBoneIndex);
                const bool isSelected = (static_cast<int>(i) == m_selectedBoneIndex);

                // Selected beats hovered beats search-match beats the
                // plain default - the same layered-priority convention
                // "Hierarchy"'s own selection highlight uses relative to
                // hover, just with one more tier (search match) here.
                ImU32 dotColor = IM_COL32(90, 230, 130, 255);
                if (matchesFilter) {
                    dotColor = IM_COL32(255, 215, 60, 255);
                }
                if (isHovered) {
                    dotColor = IM_COL32(255, 255, 255, 255);
                }
                if (isSelected) {
                    dotColor = IM_COL32(255, 140, 0, 255);
                }
                drawList->AddCircleFilled(screenPositions[i], isSelected ? 6.0f : (isHovered ? 5.0f : 3.5f), dotColor);
                if (isSelected) {
                    drawList->AddCircle(screenPositions[i], 9.0f, IM_COL32(255, 140, 0, 255), 0, 2.0f);
                }

                if (m_showAllNames || matchesFilter || isHovered || isSelected) {
                    const ImVec2 textPos(screenPositions[i].x + 7.0f, screenPositions[i].y - 7.0f);
                    const ImVec2 textSize = ImGui::CalcTextSize(m_bones[i].name.c_str());
                    drawList->AddRectFilled(ImVec2(textPos.x - 2.0f, textPos.y - 1.0f),
                        ImVec2(textPos.x + textSize.x + 2.0f, textPos.y + textSize.y + 1.0f), IM_COL32(0, 0, 0, 160));
                    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), m_bones[i].name.c_str());
                }
            }

            drawList->PopClipRect();
        }
    }

    ImGui::EndChild();

    ImGui::End();
}

} // namespace gte
