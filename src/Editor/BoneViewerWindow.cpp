#include "BoneViewerWindow.h"

#include "EditorContext.h"
#include "ModelRigCache.h"
#include "ProjectPanelData.h" // Utf8ToPath()
#include "RigidBodyWireframe.h"
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()
#include "../Assets/MeshFile.h" // DecodeMeshDataFromBytes()
#include "../Assets/RigFile.h" // RigFileData
#include "../ECS/Components/MeshAssetSource.h"
#include "../ECS/Registry.h"
#include "../Game/Physics/PhysicsSystem.h" // PhysicsSystem::GetDynamicChainRigCache() - task_manager/verlet-integration-5, Phase 2
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

// One part's worth of "what the viewport overlay needs to project/label/hit-
// test this frame" - the shared shape all four view modes reduce to, so
// the hover/hit-test math and the name-label/search-color drawing loop are
// each written ONCE (see BoneViewerWindow.cpp's Build(), "overlay" section)
// rather than quadrupled per mode.
struct OverlayPart {
    Vec3 position;
    std::string name;
    // task_manager/verlet-integration-5, PHASE0_MASTER_STRATEGY.md, Culprit C
    // - the REAL ModelPartKind partIndex this overlay slot represents. For
    // Bone/RigidBody/Joint this is always identical to this part's own
    // position in overlayParts (i == partIndex) - kept explicit rather than
    // implicit so Verlet mode (where partIndex is a bone index, NOT the
    // overlay slot position - see PHASE1's own "bone index" decision) can
    // share every downstream hover/click/highlight code path unmodified.
    std::int32_t partIndex = -1;
};

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
    m_rigidBodies.clear();
    m_joints.clear();
    m_boneChildren.clear();
    m_rootBoneIndices.clear();
    m_rigidBodyAdjacency.clear();
    m_flatSelectionAnchorIndex = -1;
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

bool BoneViewerWindow::EnsureDataLoaded(
    Renderer& renderer, const std::string& absoluteGtaPath, EditorContext& ctx, ModelRigCache& rigCache)
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
    m_rigidBodies.clear();
    m_joints.clear();

    // A genuine reload is starting (we did not take the mtime short-circuit
    // return above) - whatever Model-Part index Selection may still be
    // holding for THIS window's own m_targetEntity means nothing against the
    // data about to be (re)loaded (mirrors the private m_selectedBoneIndex =
    // -1 reset this same code used to do before Phase 1 moved selection out
    // of this class into the shared Selection object - see
    // task_manager/verlet-integration-2/PHASE0_MASTER_STRATEGY.md's
    // "Revision Notes (v2)", finding #1). Only clears it if it currently
    // belongs to m_targetEntity - a Model-Part selection belonging to some
    // OTHER entity (e.g. the user picked a different entity in Hierarchy
    // since) is correctly left untouched.
    ctx.selection.ClearModelPartIfEntity(m_targetEntity);
    m_flatSelectionAnchorIndex = -1; // Different data, possibly a different rigid-body/joint count/order - see this field's own doc comment.

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

    // Skeleton/rigid-body/joint data lives in the *.gta's METADATA section
    // (see RigFile.h) - loaded through the shared ModelRigCache (Phase 2)
    // rather than this class decoding RigFileData itself (see
    // PHASE0_MASTER_STRATEGY.md, Culprit A/E). A boneless/riggless mesh (or
    // one imported before rig extraction existed) simply resolves to a
    // valid, empty RigFileData (ModelRigCache::GetOrLoad()'s own documented
    // contract), in which case m_bones/m_rigidBodies/m_joints are correctly
    // left empty rather than treated as a failure. `nullptr` instead means
    // "not a loadable Mesh asset at all", which cannot happen here since the
    // AssetType::Mesh check above already passed.
    if (const RigFileData* rig = rigCache.GetOrLoad(absoluteGtaPath)) {
        m_bones.reserve(rig->skeleton.bones.size());
        for (const Bone& bone : rig->skeleton.bones) {
            m_bones.push_back(BoneEntry{ bone.name, bone.position, bone.parentBoneIndex });
        }

        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape,
                body.shapeSize, body.boneIndex, body.group });
        }

        m_joints.reserve(rig->physics.joints.size());
        for (const Joint& joint : rig->physics.joints) {
            m_joints.push_back(JointEntry{ joint.name, joint.translate, joint.rigidBodyAIndex, joint.rigidBodyBIndex });
        }
    }
    RebuildBoneHierarchyIndex(); // Still only walks m_bones - RigidBody/Joint have no tree to build.
    RebuildRigidBodyAdjacencyIndex(); // Derives the rigid-body-centric graph "Select All (Branch)" walks.

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

void BoneViewerWindow::RebuildRigidBodyAdjacencyIndex()
{
    std::vector<RigidBodyJointEdge> edges;
    edges.reserve(m_joints.size());
    for (const JointEntry& joint : m_joints) {
        edges.push_back(RigidBodyJointEdge{ joint.rigidBodyAIndex, joint.rigidBodyBIndex });
    }
    m_rigidBodyAdjacency = BuildRigidBodyAdjacency(edges, m_rigidBodies.size());
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

void BoneViewerWindow::RenderBoneTreeNode(std::int32_t boneIndex, const std::string& lowerFilter, int depth, EditorContext& ctx)
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
    if (ctx.selection.IsModelPartSelected(m_targetEntity, ModelPartKind::Bone, boneIndex)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::string label = bone.name.empty() ? ("Bone " + std::to_string(boneIndex)) : bone.name;

    ImGui::PushID(boneIndex);
    const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        // Ctrl/Shift-click both TOGGLE (add/remove) this one bone in/out of
        // the current selection - Selection's own multi-object support
        // (task_manager/verlet-integration-4/
        // PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md's
        // ToggleModelPartInSelection()). Deliberately NOT a genuine Windows-
        // Explorer-style Shift range-select here (unlike Rigid Body/Joint's
        // flat rows/viewport dot, below) - a bone's raw array index has no
        // meaningful linear "range" to a user looking at an indented
        // hierarchy tree, only its position within whichever branch happens
        // to be expanded right now; correctly supporting a range select here
        // would first require flattening the CURRENTLY VISIBLE/expanded tree
        // rows into a linear order, a meaningfully bigger and differently-
        // shaped piece of work the story never asked for - see
        // task_manager/verlet-integration-4/PHASE0_MASTER_STRATEGY.md's
        // "What We Will NOT Do" (v2). A plain click (neither modifier held)
        // keeps today's exact "replace with just this one" behavior via
        // SelectModelPart().
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl || io.KeyShift) {
            ctx.selection.ToggleModelPartInSelection(m_targetEntity, ModelPartKind::Bone, boneIndex);
        } else {
            ctx.selection.SelectModelPart(m_targetEntity, ModelPartKind::Bone, boneIndex);
        }
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
            RenderBoneTreeNode(child, lowerFilter, depth + 1, ctx);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void BoneViewerWindow::RenderFlatPartRow(ModelPartKind kind, std::int32_t index, const std::string& name,
    const Vec3& position, const std::string& lowerFilter, EditorContext& ctx)
{
    if (!lowerFilter.empty() && ToLower(name).find(lowerFilter) == std::string::npos) {
        return; // Same "search prunes the list" convention as the bone tree.
    }

    const bool isSelected = ctx.selection.IsModelPartSelected(m_targetEntity, kind, index);
    const std::string label = name.empty() ? ("Part " + std::to_string(index)) : name;

    ImGui::PushID(index);
    if (ImGui::Selectable(label.c_str(), isSelected)) {
        // v2 (task_manager/verlet-integration-4/PHASE0_MASTER_STRATEGY.md's
        // Revision Notes, finding #2): Ctrl-click TOGGLES exactly this one
        // row in/out of the current selection (Selection's own
        // ToggleModelPartInSelection()); Shift-click instead performs a
        // genuine Windows-Explorer/Unity-style contiguous RANGE select from
        // m_flatSelectionAnchorIndex (the last plain- or Ctrl-clicked row -
        // see that field's own doc comment, BoneViewerWindow.h) through
        // THIS row, REPLACING the whole selection with exactly that range
        // (never a union with whatever was selected before - a real
        // Explorer Shift-click does the same). A plain click (neither
        // modifier held) keeps today's exact "replace with just this one"
        // behavior via SelectModelPart(), and also moves the range anchor to
        // this row, same as a real Ctrl-click does.
        const ImGuiIO& io = ImGui::GetIO();
        // task_manager/verlet-integration-5, PHASE0_MASTER_STRATEGY.md,
        // Culprit D - Verlet mode's partIndex is a skeleton BONE index, not a
        // dense 0..N-1 flat-list position, so a raw inclusive integer range
        // between two bone indices would silently include bones that are not
        // even Verlet joints at all. Shift-click on a Verlet row therefore
        // falls back to the exact same toggle-only behavior Bone mode's own
        // tree already uses for the identical underlying reason - only Rigid
        // Body/Joint (both genuinely dense, 0..count-1 index spaces) get a
        // real contiguous range select.
        const bool supportsRangeSelect = kind != ModelPartKind::Verlet;
        if (supportsRangeSelect && io.KeyShift) {
            const std::vector<std::int32_t> range = BuildInclusiveIndexRange(m_flatSelectionAnchorIndex, index);
            ctx.selection.SelectModelParts(m_targetEntity, kind, std::vector<int>(range.begin(), range.end()));
        } else if (io.KeyCtrl || io.KeyShift) {
            ctx.selection.ToggleModelPartInSelection(m_targetEntity, kind, index);
            m_flatSelectionAnchorIndex = index;
        } else {
            ctx.selection.SelectModelPart(m_targetEntity, kind, index);
            m_flatSelectionAnchorIndex = index;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_camTarget = position;
        }
    }
    ImGui::PopID();
}

void BoneViewerWindow::RenderVerletChainNode(std::int32_t chainIndex, const DynamicChainDefinition& chain,
    const std::string& lowerFilter, EditorContext& ctx)
{
    // "Search prunes the tree" - hide the WHOLE chain header if a non-empty
    // filter matches none of its joints' own names (mirrors
    // BoneMatchesFilterRecursive()'s own reasoning, just non-recursive since
    // a chain's joints have no further descendants of their own).
    bool anyMatch = lowerFilter.empty();
    for (std::size_t j = 0; j < chain.jointBoneIndices.size() && !anyMatch; ++j) {
        const std::int32_t boneIndex = chain.jointBoneIndices[j];
        if (boneIndex >= 0 && static_cast<std::size_t>(boneIndex) < m_bones.size()
            && ToLower(m_bones[static_cast<std::size_t>(boneIndex)].name).find(lowerFilter) != std::string::npos) {
            anyMatch = true;
        }
    }
    if (!anyMatch) {
        return;
    }

    const char* rootName = (chain.rootBoneIndex >= 0 && static_cast<std::size_t>(chain.rootBoneIndex) < m_bones.size())
        ? m_bones[static_cast<std::size_t>(chain.rootBoneIndex)].name.c_str()
        : "(none)";
    char header[160];
    std::snprintf(header, sizeof(header), "Chain %d - Root: %s (%zu joints)", chainIndex, rootName,
        chain.jointBoneIndices.size());

    ImGui::PushID(chainIndex);
    if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (const std::int32_t boneIndex : chain.jointBoneIndices) {
            if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
                continue; // Defensive - should never happen for a well-formed DynamicChainDefinition.
            }
            const BoneEntry& bone = m_bones[static_cast<std::size_t>(boneIndex)];
            RenderFlatPartRow(ModelPartKind::Verlet, boneIndex, bone.name, bone.position, lowerFilter, ctx);
        }
        if (chain.hasHeadCollider) {
            ImGui::TextDisabled("Head Collider: r=%.3f", chain.headColliderRadius);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void BoneViewerWindow::BuildPartListPane(
    const std::string& lowerFilter, EditorContext& ctx, const DynamicChainRigCache::ModelEntry* verletModel)
{
    switch (m_viewMode) {
    case ModelPartKind::Bone:
        if (m_bones.empty()) {
            ImGui::TextDisabled("(no bones)");
            return;
        }
        for (const std::int32_t root : m_rootBoneIndices) {
            RenderBoneTreeNode(root, lowerFilter, 0, ctx);
        }
        return;
    case ModelPartKind::RigidBody:
        if (m_rigidBodies.empty()) {
            ImGui::TextDisabled("(no rigid bodies)");
            return;
        }
        for (std::size_t i = 0; i < m_rigidBodies.size(); ++i) {
            RenderFlatPartRow(ModelPartKind::RigidBody, static_cast<std::int32_t>(i), m_rigidBodies[i].name,
                m_rigidBodies[i].translate, lowerFilter, ctx);
        }
        return;
    case ModelPartKind::Joint:
        if (m_joints.empty()) {
            ImGui::TextDisabled("(no joints)");
            return;
        }
        for (std::size_t i = 0; i < m_joints.size(); ++i) {
            const JointEntry& joint = m_joints[i];
            const std::string label = joint.name.empty() ? ("Joint " + std::to_string(i)) : joint.name;
            RenderFlatPartRow(ModelPartKind::Joint, static_cast<std::int32_t>(i), label, joint.translate, lowerFilter, ctx);
        }
        return;
    case ModelPartKind::Verlet:
        if (verletModel == nullptr || verletModel->chains.empty()) {
            ImGui::TextDisabled("(no dynamic bone chains)");
            return;
        }
        for (std::size_t i = 0; i < verletModel->chains.size(); ++i) {
            RenderVerletChainNode(static_cast<std::int32_t>(i), verletModel->chains[i], lowerFilter, ctx);
        }
        return;
    }
}

void BoneViewerWindow::Build(
    Registry& registry, Renderer& renderer, EditorContext& ctx, ModelRigCache& rigCache, PhysicsSystem& physicsSystem)
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

    if (!EnsureDataLoaded(renderer, source->gtaPath, ctx, rigCache) || !m_cachedIsValid) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load mesh/skeleton data for:");
        ImGui::TextWrapped("%s", source->gtaPath.c_str());
        ImGui::End();
        return;
    }

    // task_manager/verlet-integration-5, PHASE0_MASTER_STRATEGY.md, Culprit E
    // - fetched once per Build() call, PhysicsSystem's own DynamicChainRigCache
    // is already an in-memory map, so this is a cheap, always-safe-to-repeat
    // lookup; doing it once here and threading the pointer down avoids
    // redundant identical hash lookups per frame. May be nullptr (no entry
    // registered yet for this path) - handled gracefully everywhere below.
    const DynamicChainRigCache::ModelEntry* verletModel = physicsSystem.GetDynamicChainRigCache().TryGet(source->gtaPath);

    if (m_needsFraming) {
        FrameCameraToBounds();
        m_needsFraming = false;
    }

    EnsurePipeline(renderer);

    // --- Toolbar -----------------------------------------------------------
    ImGui::PushItemWidth(240.0f);
    ImGui::InputTextWithHint("##BoneViewerSearch", "Search by name...", m_searchBuffer, sizeof(m_searchBuffer));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        FrameCameraToBounds();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show All Names", &m_showAllNames);
    ImGui::SameLine();
    ImGui::PushItemWidth(140.0f);
    static const char* kViewModeLabels[] = { "Bones", "Rigid Bodies", "Joints", "Verlet" };
    int viewModeIndex = static_cast<int>(m_viewMode);
    if (ImGui::Combo("View", &viewModeIndex, kViewModeLabels, static_cast<int>(std::size(kViewModeLabels)))) {
        m_viewMode = static_cast<ModelPartKind>(viewModeIndex);
        m_flatSelectionAnchorIndex = -1; // Different index space (Rigid Body vs. Joint vs. Bone) - see this field's own doc comment.
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // task_manager/verlet-integration-5 - Verlet mode's "count" is naturally
    // "how many chains/joints", not a single flat count - computed once here,
    // reading the SAME verletModel fetched once per Build() call above.
    std::size_t verletJointCount = 0;
    std::size_t verletChainCount = verletModel != nullptr ? verletModel->chains.size() : 0;
    if (verletModel != nullptr) {
        for (const DynamicChainDefinition& chain : verletModel->chains) {
            verletJointCount += chain.jointBoneIndices.size();
        }
    }

    const std::size_t partCount = m_viewMode == ModelPartKind::Bone ? m_bones.size()
        : m_viewMode == ModelPartKind::RigidBody ? m_rigidBodies.size()
        : m_viewMode == ModelPartKind::Joint      ? m_joints.size()
                                                  : verletJointCount;
    const char* partNoun = m_viewMode == ModelPartKind::Bone ? "bones"
        : m_viewMode == ModelPartKind::RigidBody ? "rigid bodies"
        : m_viewMode == ModelPartKind::Joint      ? "joints"
                                                  : "verlet joints";
    ImGui::TextDisabled("%zu %s - %u verts / %u tris", partCount, partNoun, m_vertexCount, m_indexCount / 3);
    if (m_viewMode == ModelPartKind::Verlet) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu chain%s)", verletChainCount, verletChainCount == 1 ? "" : "s");
    }

    if (m_viewMode == ModelPartKind::Bone && m_bones.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
            "This model has no bone/skeleton data (a boneless mesh, or one imported before rig extraction existed).");
    } else if (m_viewMode == ModelPartKind::RigidBody && m_rigidBodies.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "This model has no rigid-body physics data.");
    } else if (m_viewMode == ModelPartKind::Joint && m_joints.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "This model has no joint physics data.");
    } else if (m_viewMode == ModelPartKind::Verlet && verletJointCount == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
            "This model has no detected dynamic (Verlet) bone-chain physics data.");
    }
    ImGui::Separator();

    // "Select All (Group)"/"Select All (Branch)" - only meaningful in Rigid
    // Body mode (RigidBodyEntry::group and joint-adjacency are both
    // rigid-body-only concepts - see task_manager/verlet-integration-4/
    // PHASE0_MASTER_STRATEGY.md, Step 4). Seeded from whichever ONE rigid
    // body is currently the Model-Part selection on THIS window's own
    // m_targetEntity - both buttons are disabled (not merely a no-op) when
    // there is no such single seed to act from, matching this codebase's
    // existing "grey out an action with nothing valid to act on" convention
    // (see Panels/ProjectPanel.cpp's own "Delete Selected" menu item gated
    // on HasAssetSelection()).
    if (m_viewMode == ModelPartKind::RigidBody && !m_rigidBodies.empty()) {
        // v2 fix (task_manager/verlet-integration-4/PHASE0_MASTER_STRATEGY.md's
        // Revision Notes, finding #1): hasSeed now ALSO requires
        // SelectedModelPartIndices().size() == 1. v1's check below the
        // "&&"s stopped at "the lowest selected index is in range," which
        // stays true even when SEVERAL rigid bodies are already selected
        // (e.g. right after clicking one of these very buttons once
        // already, or after a Shift-range-select) - reseeding from
        // SelectedModelPartIndex() (always just the LOWEST of however many
        // are selected - see Selection.h) in that state is ambiguous and
        // contradicts this button's own "seeded from whichever ONE rigid
        // body is currently selected" contract (Step 1, goal #1) and the
        // story's own singular "pick everything with same group [as THE
        // selected one]" phrasing.
        const bool hasSeed = ctx.selection.Kind() == InspectorSelectionKind::ModelPart
            && ctx.selection.SelectedModelPartEntity() == m_targetEntity
            && ctx.selection.SelectedModelPartKind() == ModelPartKind::RigidBody
            && ctx.selection.SelectedModelPartIndices().size() == 1
            && ctx.selection.SelectedModelPartIndex() >= 0
            && static_cast<std::size_t>(ctx.selection.SelectedModelPartIndex()) < m_rigidBodies.size();

        const std::int32_t seed = hasSeed ? static_cast<std::int32_t>(ctx.selection.SelectedModelPartIndex()) : -1;
        // Only meaningful when hasSeed is true (seed >= 0 and in range) -
        // guarded accordingly at every read site below.
        const bool seedIsBranch = hasSeed && m_rigidBodyAdjacency[static_cast<std::size_t>(seed)].size() >= 3;

        ImGui::BeginDisabled(!hasSeed);
        if (ImGui::Button("Select All (Group)")) {
            std::vector<std::uint8_t> groups;
            groups.reserve(m_rigidBodies.size());
            for (const RigidBodyEntry& body : m_rigidBodies) {
                groups.push_back(body.group);
            }
            const std::vector<std::int32_t> matches = SelectRigidBodiesByGroup(groups, seed);
            ctx.selection.SelectModelParts(
                m_targetEntity, ModelPartKind::RigidBody, std::vector<int>(matches.begin(), matches.end()));
        }
        // v2 QoL addition (task_manager/verlet-integration-4/
        // PHASE0_MASTER_STRATEGY.md's Revision Notes, finding #3): preview
        // the real match count on hover, computed via the exact same Phase 2
        // function the click handler itself calls above - never a
        // second, independently-maintained estimate.
        if (hasSeed && ImGui::IsItemHovered()) {
            std::vector<std::uint8_t> groups;
            groups.reserve(m_rigidBodies.size());
            for (const RigidBodyEntry& body : m_rigidBodies) {
                groups.push_back(body.group);
            }
            const std::size_t count = SelectRigidBodiesByGroup(groups, seed).size();
            const RigidBodyEntry& seedBody = m_rigidBodies[static_cast<std::size_t>(seed)];
            ImGui::SetTooltip("Selects %zu rigid bod%s sharing collision group %u with \"%s\".", count,
                count == 1 ? "y" : "ies", static_cast<unsigned>(seedBody.group), seedBody.name.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Select All (Branch)")) {
            const std::vector<std::int32_t> matches = SelectRigidBodyBranch(m_rigidBodyAdjacency, seed);
            ctx.selection.SelectModelParts(
                m_targetEntity, ModelPartKind::RigidBody, std::vector<int>(matches.begin(), matches.end()));
        }
        if (hasSeed && ImGui::IsItemHovered()) {
            const RigidBodyEntry& seedBody = m_rigidBodies[static_cast<std::size_t>(seed)];
            if (seedIsBranch) {
                // The seed itself is a junction (degree >= 3) - the click
                // handler above will correctly select just `{ seed }` (see
                // Phase 2's own SelectRigidBodyBranchFromABranchNodeItself...
                // test) - tell the user WHY up front instead of letting them
                // discover "nothing visibly changed" by clicking blind.
                ImGui::SetTooltip(
                    "\"%s\" is itself a branch/junction (connected to %zu other rigid bodies) -\n"
                    "there is no single unambiguous chain to select; only itself will be selected.",
                    seedBody.name.c_str(), m_rigidBodyAdjacency[static_cast<std::size_t>(seed)].size());
            } else {
                const std::size_t count = SelectRigidBodyBranch(m_rigidBodyAdjacency, seed).size();
                ImGui::SetTooltip("Selects %zu rigid bod%s in \"%s\"'s own uninterrupted joint chain.", count,
                    count == 1 ? "y" : "ies", seedBody.name.c_str());
            }
        }
        ImGui::EndDisabled();
        if (!hasSeed) {
            ImGui::SameLine();
            if (ctx.selection.SelectedModelPartIndices().size() > 1) {
                ImGui::TextDisabled("(select exactly one rigid body first - %zu are currently selected)",
                    ctx.selection.SelectedModelPartIndices().size());
            } else {
                ImGui::TextDisabled("(select a rigid body first)");
            }
        }
    }

    const std::string lowerFilter = ToLower(std::string(m_searchBuffer));

    // --- Left pane: part list (bone tree, or a flat rigid-body/joint list) --
    // Unity/Omniverse-Inspector-style: a real indented tree for Bones,
    // walked from m_rootBoneIndices down through m_boneChildren (see
    // RebuildBoneHierarchyIndex()) - the same "GetChildren()-based recursive
    // tree" shape as "Hierarchy"'s own entity tree (Panels/HierarchyPanel.cpp),
    // just for bones instead of entities; a flat Selectable list for Rigid
    // Bodies/Joints, which have no bind-pose parent/child tree at all.
    constexpr float kSplitterThickness = 6.0f;
    constexpr float kMinTreeWidth = 150.0f;
    constexpr float kMinViewportWidth = 200.0f;
    const float fullWidth = ImGui::GetContentRegionAvail().x;
    const float maxTreeWidth = std::max(kMinTreeWidth, fullWidth - kSplitterThickness - kMinViewportWidth);
    m_treeWidth = Clamp(m_treeWidth, kMinTreeWidth, maxTreeWidth);

    ImGui::BeginChild("BoneViewerTree", ImVec2(m_treeWidth, 0.0f), true);
    BuildPartListPane(lowerFilter, ctx, verletModel);
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

        // Phase 4C (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) -
        // std::nullopt: this Bone Viewer viewport is not one of the
        // Profiler's three named passes, and must never silently share a
        // query slot with (or overwrite the cached timing of) "Game View"/
        // "Scene View" - see Renderer::RenderOffscreen()'s own doc comment.
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

        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_descriptor)), avail);
        const ImVec2 imageMax(imageMin.x + avail.x, imageMin.y + avail.y);
        const bool hovered = ImGui::IsItemHovered();

        // Build this frame's "parts to project/label/hit-test" list for
        // whichever mode is active - the ONE place per mode that decides
        // what OverlayPart::position/name/partIndex means, so the hover/hit-
        // test math AND the name-label/search-color drawing loop below are
        // each written once and reused for all four modes (see
        // PHASE0_MASTER_STRATEGY.md's "Revision Notes (v2)", finding #2, and
        // task_manager/verlet-integration-5's own Culprit C).
        std::vector<OverlayPart> overlayParts;
        if (m_viewMode == ModelPartKind::Bone) {
            overlayParts.reserve(m_bones.size());
            for (const BoneEntry& bone : m_bones) {
                overlayParts.push_back(OverlayPart{ bone.position, bone.name, static_cast<std::int32_t>(overlayParts.size()) });
            }
        } else if (m_viewMode == ModelPartKind::RigidBody) {
            overlayParts.reserve(m_rigidBodies.size());
            for (std::size_t i = 0; i < m_rigidBodies.size(); ++i) {
                const RigidBodyEntry& body = m_rigidBodies[i];
                overlayParts.push_back(OverlayPart{ body.translate,
                    body.name.empty() ? ("Part " + std::to_string(i)) : body.name, static_cast<std::int32_t>(i) });
            }
        } else if (m_viewMode == ModelPartKind::Joint) {
            overlayParts.reserve(m_joints.size());
            for (std::size_t i = 0; i < m_joints.size(); ++i) {
                const JointEntry& joint = m_joints[i];
                overlayParts.push_back(OverlayPart{ joint.translate,
                    joint.name.empty() ? ("Joint " + std::to_string(i)) : joint.name, static_cast<std::int32_t>(i) });
            }
        } else { // ModelPartKind::Verlet
            if (verletModel != nullptr) {
                for (const DynamicChainDefinition& chain : verletModel->chains) {
                    for (const std::int32_t boneIndex : chain.jointBoneIndices) {
                        if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
                            continue;
                        }
                        const BoneEntry& bone = m_bones[static_cast<std::size_t>(boneIndex)];
                        overlayParts.push_back(OverlayPart{ bone.position, bone.name, boneIndex });
                    }
                }
            }
        }

        // Project every part to screen space up front - shared by hit-
        // testing (hover/click below) AND the overlay drawing pass
        // (further below), computed with THIS frame's viewProj so
        // everything stays pixel-aligned with what was just rendered.
        std::vector<ImVec2> screenPositions(overlayParts.size());
        std::vector<char> onScreen(overlayParts.size(), 0);
        for (std::size_t i = 0; i < overlayParts.size(); ++i) {
            ImVec2 screen;
            if (ProjectToScreen(overlayParts[i].position, viewProj, imageMin, imageMax, screen)) {
                screenPositions[i] = screen;
                onScreen[i] = 1;
            }
        }

        // Nearest on-screen part dot to the mouse cursor (within a small
        // pixel radius) - what both the hover-name-reveal and a direct
        // viewport click (below) hit-test against.
        int hoveredPartIndex = -1;
        if (hovered) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            float hoveredDistSq = 144.0f; // 12px radius.
            for (std::size_t i = 0; i < overlayParts.size(); ++i) {
                if (!onScreen[i]) {
                    continue;
                }
                const float dx = mousePos.x - screenPositions[i].x;
                const float dy = mousePos.y - screenPositions[i].y;
                const float distSq = dx * dx + dy * dy;
                if (distSq < hoveredDistSq) {
                    hoveredDistSq = distSq;
                    hoveredPartIndex = static_cast<int>(i);
                }
            }
        }

        // --- Orbit camera input / direct-click part selection (applied to
        // what NEXT frame renders - see this window's own class comment for
        // why this one-frame lag mirrors Panels/ScenePanel.cpp's
        // EditorCamera handling) ---------------------------------------------
        // `io` moved up here (from further below) since the click handling
        // just below now needs KeyCtrl/KeyShift too - every other pre-
        // existing use of `io` further down in this function keeps working
        // unchanged, it is the exact same local, just declared a few lines
        // earlier now.
        const ImGuiIO& io = ImGui::GetIO();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredPartIndex >= 0) {
                // Clicked directly on a part's gizmo dot - select it
                // (mirrors clicking its row in the tree/list pane) instead
                // of starting an orbit-camera drag. Range-select (Shift)
                // only makes sense for a flat, linearly-ordered list (Rigid
                // Body/Joint mode) - Bone mode keeps the same toggle-only
                // behavior as its own tree rows (RenderBoneTreeNode()) for
                // both Ctrl AND Shift, since a bone's raw array index
                // carries no meaningful "range" to a user (see that
                // function's own updated doc comment above, and
                // task_manager/verlet-integration-4/
                // PHASE0_MASTER_STRATEGY.md's Revision Notes, finding #2).
                // Verlet mode ALSO does not support range-select - its
                // partIndex is a bone index, not a dense flat-list position
                // (task_manager/verlet-integration-5, Culprit D).
                const bool supportsRangeSelect = m_viewMode != ModelPartKind::Bone && m_viewMode != ModelPartKind::Verlet;
                // The REAL partIndex this overlay slot represents (task_manager/
                // verlet-integration-5, Culprit C) - identical to hoveredPartIndex
                // for Bone/RigidBody/Joint, but the joint's own bone index for
                // Verlet mode.
                const std::int32_t realPartIndex = overlayParts[static_cast<std::size_t>(hoveredPartIndex)].partIndex;
                if (supportsRangeSelect && io.KeyShift) {
                    const std::vector<std::int32_t> range = BuildInclusiveIndexRange(m_flatSelectionAnchorIndex, realPartIndex);
                    ctx.selection.SelectModelParts(
                        m_targetEntity, m_viewMode, std::vector<int>(range.begin(), range.end()));
                } else if (io.KeyCtrl || io.KeyShift) {
                    ctx.selection.ToggleModelPartInSelection(m_targetEntity, m_viewMode, realPartIndex);
                    if (supportsRangeSelect) {
                        m_flatSelectionAnchorIndex = realPartIndex;
                    }
                } else {
                    ctx.selection.SelectModelPart(m_targetEntity, m_viewMode, realPartIndex);
                    if (supportsRangeSelect) {
                        m_flatSelectionAnchorIndex = realPartIndex;
                    }
                }
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

        // --- Part gizmo overlay --------------------------------------------
        if (!overlayParts.empty()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(imageMin, imageMax, true);

            if (m_viewMode == ModelPartKind::Bone) {
                // Lines to parent first, so every dot/label below always
                // paints over them. UNCHANGED from before this phase.
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
            } else if (m_viewMode == ModelPartKind::RigidBody) {
                // "Which bone drives this" hint - a dimmer connecting line
                // from each rigid body to its attached bone (if any),
                // symmetrical with a bone's own parent-line above.
                for (std::size_t i = 0; i < m_rigidBodies.size(); ++i) {
                    const std::int32_t boneIndex = m_rigidBodies[i].boneIndex;
                    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size() || !onScreen[i]) {
                        continue;
                    }
                    ImVec2 boneScreen;
                    if (ProjectToScreen(m_bones[static_cast<std::size_t>(boneIndex)].position, viewProj, imageMin, imageMax,
                            boneScreen)) {
                        drawList->AddLine(screenPositions[i], boneScreen, IM_COL32(80, 180, 255, 110), 1.5f);
                    }
                }
            } else if (m_viewMode == ModelPartKind::Joint) {
                // Joint mode - draw both connector lines (joint -> bodyA,
                // joint -> bodyB) whenever the referenced rigid body index
                // is in range.
                for (std::size_t i = 0; i < m_joints.size(); ++i) {
                    if (!onScreen[i]) {
                        continue;
                    }
                    const JointEntry& joint = m_joints[i];
                    for (const std::int32_t bodyIndex : { joint.rigidBodyAIndex, joint.rigidBodyBIndex }) {
                        if (bodyIndex < 0 || static_cast<std::size_t>(bodyIndex) >= m_rigidBodies.size()) {
                            continue;
                        }
                        ImVec2 bodyScreen;
                        if (ProjectToScreen(m_rigidBodies[static_cast<std::size_t>(bodyIndex)].translate, viewProj, imageMin,
                                imageMax, bodyScreen)) {
                            drawList->AddLine(screenPositions[i], bodyScreen, IM_COL32(200, 120, 255, 140), 1.5f);
                        }
                    }
                }
            } else if (m_viewMode == ModelPartKind::Verlet) {
                // task_manager/verlet-integration-5 - chain connector lines,
                // root/anchor markers, and the optional head-collider
                // wireframe, sourced straight from verletModel (the SAME
                // PhysicsSystem-owned data the Inspector's "Dynamic Chain
                // Physics" section already reads/edits).
                if (verletModel != nullptr) {
                    for (const DynamicChainDefinition& chain : verletModel->chains) {
                        // Root/anchor marker - drawn even though it is NOT
                        // part of overlayParts/selectable
                        // (DynamicChainDefinition::rootBoneIndex is never
                        // itself simulated) - a small, visually distinct,
                        // non-interactive square so a user can see exactly
                        // where a chain "hangs from."
                        Vec3 prevPos;
                        bool havePrev = false;
                        if (chain.rootBoneIndex >= 0 && static_cast<std::size_t>(chain.rootBoneIndex) < m_bones.size()) {
                            prevPos = m_bones[static_cast<std::size_t>(chain.rootBoneIndex)].position;
                            havePrev = true;
                            ImVec2 rootScreen;
                            if (ProjectToScreen(prevPos, viewProj, imageMin, imageMax, rootScreen)) {
                                constexpr float kHalf = 4.0f;
                                drawList->AddRectFilled(ImVec2(rootScreen.x - kHalf, rootScreen.y - kHalf),
                                    ImVec2(rootScreen.x + kHalf, rootScreen.y + kHalf), IM_COL32(200, 200, 200, 255));
                            }
                        }
                        // Chain connector lines, root -> joint[0] -> joint[1]
                        // -> ..., reprojected directly from m_bones
                        // (independent of overlayParts/screenPositions -
                        // mirrors how RigidBody mode's own "attached bone"
                        // connector line already reprojects
                        // m_bones[boneIndex].position directly).
                        for (const std::int32_t boneIndex : chain.jointBoneIndices) {
                            if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
                                continue;
                            }
                            const Vec3 jointPos = m_bones[static_cast<std::size_t>(boneIndex)].position;
                            if (havePrev) {
                                ImVec2 prevScreen, jointScreen;
                                if (ProjectToScreen(prevPos, viewProj, imageMin, imageMax, prevScreen)
                                    && ProjectToScreen(jointPos, viewProj, imageMin, imageMax, jointScreen)) {
                                    drawList->AddLine(prevScreen, jointScreen, IM_COL32(255, 90, 170, 160), 2.0f);
                                }
                            }
                            prevPos = jointPos;
                            havePrev = true;
                        }
                        // Optional head-collider wireframe (Sphere shape -
                        // reusing RigidBodyWireframe.h's own existing
                        // per-shape geometry builder exactly like Rigid Body
                        // mode's selected-shape wireframe does) - drawn
                        // unconditionally whenever configured, NOT
                        // selection-gated (it is a debug aid for the whole
                        // chain, not itself a selectable part).
                        if (chain.hasHeadCollider && chain.headColliderBoneIndex >= 0
                            && static_cast<std::size_t>(chain.headColliderBoneIndex) < m_bones.size()
                            && chain.headColliderRadius > 0.0f) {
                            const Vec3 colliderCenter = m_bones[static_cast<std::size_t>(chain.headColliderBoneIndex)].position;
                            const std::vector<WireframeSegment> wireframe = BuildRigidBodyWireframe(
                                RigidBodyShape::Sphere, Vec3(chain.headColliderRadius, 0.0f, 0.0f), colliderCenter, Vec3::Zero());
                            for (const WireframeSegment& segment : wireframe) {
                                ImVec2 screenA, screenB;
                                if (ProjectToScreen(segment.a, viewProj, imageMin, imageMax, screenA)
                                    && ProjectToScreen(segment.b, viewProj, imageMin, imageMax, screenB)) {
                                    drawList->AddLine(screenA, screenB, IM_COL32(255, 90, 170, 90), 1.25f);
                                }
                            }
                        }
                    }
                }
            }

            for (std::size_t i = 0; i < overlayParts.size(); ++i) {
                if (!onScreen[i]) {
                    continue;
                }
                const bool matchesFilter =
                    !lowerFilter.empty() && ToLower(overlayParts[i].name).find(lowerFilter) != std::string::npos;
                const bool isHovered = hovered && (static_cast<int>(i) == hoveredPartIndex);
                const bool isSelected =
                    ctx.selection.IsModelPartSelected(m_targetEntity, m_viewMode, overlayParts[i].partIndex);

                // Base dot color is distinct PER MODE (green bones, cyan
                // rigid bodies, violet joints, hot pink verlet joints) -
                // selected beats hovered beats search-match beats the mode's
                // own plain default, the same layered-priority convention
                // "Hierarchy"'s own selection highlight uses relative to
                // hover, just with one more tier (search match) here.
                ImU32 dotColor = m_viewMode == ModelPartKind::Bone ? IM_COL32(90, 230, 130, 255)
                    : m_viewMode == ModelPartKind::RigidBody ? IM_COL32(80, 180, 255, 255)
                    : m_viewMode == ModelPartKind::Joint      ? IM_COL32(200, 120, 255, 255)
                                                              : IM_COL32(255, 90, 170, 255); // Verlet
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

                // As of task_manager/verlet-integration-3
                // (PHASE2_BONE_VIEWER_SELECT_TO_REVEAL_WIREFRAME_INTEGRATION.md):
                // an UNSELECTED rigid body shows ONLY its plain dot (drawn
                // above, shared with Bone/Joint mode) - exactly a "single
                // selectable point", per that campaign's own requirement.
                // Every CURRENTLY SELECTED rigid body additionally reveals
                // its real, per-shape wireframe (a wire sphere/box/capsule
                // built from its actual shape/size/rotation - see
                // RigidBodyWireframe.h), never a generic screen-space
                // circle. As of task_manager/verlet-integration-4
                // (PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md/
                // PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md),
                // Selection can hold MANY rigid bodies at once (Ctrl-click,
                // Shift-range-select, or the "Select All (Group)"/"Select
                // All (Branch)" toolbar buttons) - `isSelected` here is a
                // per-part membership check, so this loop naturally draws a
                // wireframe for EVERY currently-selected rigid body, not
                // just one; no code in this loop needed to change for that
                // to be correct - only IsModelPartSelected()'s own
                // semantics did (see Selection.h).
                if (m_viewMode == ModelPartKind::RigidBody && isSelected) {
                    const RigidBodyEntry& body = m_rigidBodies[i];
                    const std::vector<WireframeSegment> wireframe =
                        BuildRigidBodyWireframe(body.shape, body.shapeSize, body.translate, body.rotateRadians);
                    for (const WireframeSegment& segment : wireframe) {
                        ImVec2 screenA, screenB;
                        if (ProjectToScreen(segment.a, viewProj, imageMin, imageMax, screenA)
                            && ProjectToScreen(segment.b, viewProj, imageMin, imageMax, screenB)) {
                            drawList->AddLine(screenA, screenB, dotColor, 1.5f);
                        }
                    }
                }

                if (m_showAllNames || matchesFilter || isHovered || isSelected) {
                    const ImVec2 textPos(screenPositions[i].x + 7.0f, screenPositions[i].y - 7.0f);
                    const ImVec2 textSize = ImGui::CalcTextSize(overlayParts[i].name.c_str());
                    drawList->AddRectFilled(ImVec2(textPos.x - 2.0f, textPos.y - 1.0f),
                        ImVec2(textPos.x + textSize.x + 2.0f, textPos.y + textSize.y + 1.0f), IM_COL32(0, 0, 0, 160));
                    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), overlayParts[i].name.c_str());
                }
            }

            drawList->PopClipRect();
        }
    }

    ImGui::EndChild();

    ImGui::End();
}

} // namespace gte
