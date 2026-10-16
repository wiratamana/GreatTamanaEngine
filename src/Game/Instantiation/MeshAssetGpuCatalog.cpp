#include "MeshAssetGpuCatalog.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Assets/AssetTypes.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/MaterialData.h"
#include "../../Assets/MeshFile.h"
#include "../../Assets/RigFile.h"
#include "../../Renderer/MeshVertex.h"
#include "../../Renderer/Culling/CullingTypes.h" // ComputeLocalAABB()
#include "../../Renderer/Renderer.h"
#include "MeshMaterialPartitioner.h"
#include "MeshVertexPacking.h"
#include "../RenderSystem.h"

#include <filesystem>
#include <optional>

namespace gte {

namespace {

// UTF-8 game path to filesystem path, via std::u8string.
std::filesystem::path Utf8PathFromGamePath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Tight local-space AABB of the vertices `indices` references (out-of-range indices are skipped).
AABB ComputeSubsetLocalAABB(const std::vector<Vec3>& positions, const std::vector<std::uint32_t>& indices)
{
    std::vector<Vec3> referenced;
    referenced.reserve(indices.size());
    for (const std::uint32_t index : indices) {
        if (index < positions.size()) {
            referenced.push_back(positions[index]);
        }
    }
    return ComputeLocalAABB(referenced);
}

} // namespace

PipelineHandle MeshAssetGpuCatalog::EnsureMeshPipeline(
    RenderSystem& renderSystem, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    if (!m_meshPipeline.IsValid()) {
        m_meshPipeline = renderSystem.RegisterPipeline(renderer.CreatePipeline("shaders/Mesh.vert.spv",
            "shaders/Mesh.frag.spv", VertexLayout::PositionNormal, false, "Mesh.vert/Mesh.frag (PositionNormal)",
            /*useInstanceBuffer=*/false, sceneServicesSetLayout));
    }
    return m_meshPipeline;
}

PipelineHandle MeshAssetGpuCatalog::EnsureTexturedMeshPipeline(
    RenderSystem& renderSystem, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    if (!m_texturedMeshPipeline.IsValid()) {
        m_texturedMeshPipeline = renderSystem.RegisterPipeline(
            renderer.CreatePipeline("shaders/TexturedMesh.vert.spv", "shaders/TexturedMesh.frag.spv",
                VertexLayout::PositionNormalUv, true, "TexturedMesh.vert/TexturedMesh.frag (PositionNormalUv)",
                /*useInstanceBuffer=*/false, sceneServicesSetLayout));
    }
    return m_texturedMeshPipeline;
}

const std::vector<MeshAssetPart>& MeshAssetGpuCatalog::EnsureMeshAsset(
    RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath)
{
    static const std::vector<MeshAssetPart> kEmpty;

    if (const auto found = m_meshAssetCache.find(absoluteGtaPath); found != m_meshAssetCache.end()) {
        return found->second;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return kEmpty; // Missing file, bad magic, or not a Mesh asset.
    }

    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty() || mesh->indices.size() < 3) {
        return kEmpty; // Corrupt/truncated payload, or an empty mesh.
    }

    // Materials/textures and rig are optional metadata (absent for materialless/boneless models).
    MaterialData materials;
    std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    if (rig.has_value()) {
        materials = rig->materials;
    }

    // Skinned = a bone hierarchy AND a skin weight for every vertex.
    const bool skinned = rig.has_value() && !rig->skeleton.bones.empty()
        && rig->skinWeights.size() == mesh->positions.size();

    // Textures are referenced by Guid; resolve them via a local scan of the mesh's own directory
    // (which also covers its sibling "..._Textures" folder).
    AssetDatabase textureDatabase;
    if (!materials.textures.empty()) {
        textureDatabase.RefreshFromDirectory(Utf8PathFromGamePath(absoluteGtaPath).parent_path());
    }

    // Pure index-range partitioning; whether a slice ends up textured depends on the texture cache below.
    const std::vector<MeshMaterialSlice> slices = PartitionMeshMaterials(mesh->indices.size(), materials.materials);

    std::vector<std::uint32_t> untexturedIndices;
    struct TexturedSlice {
        std::size_t start = 0;
        std::size_t count = 0;
        MaterialTextureHandle texture;
        std::string name;
    };
    std::vector<TexturedSlice> texturedSlices;

    for (const MeshMaterialSlice& slice : slices) {
        MaterialTextureHandle texture = kInvalidMaterialTextureHandle;
        if (slice.materialIndex >= 0 && static_cast<std::size_t>(slice.materialIndex) < materials.materials.size()) {
            const Material& material = materials.materials[static_cast<std::size_t>(slice.materialIndex)];
            if (material.textureIndex >= 0 && static_cast<std::size_t>(material.textureIndex) < materials.textures.size()) {
                const Guid& textureGuid = materials.textures[static_cast<std::size_t>(material.textureIndex)].guid;
                texture = m_materialTextureCache.Resolve(renderSystem, renderer, textureDatabase, textureGuid);
            }
        }

        if (texture.IsValid()) {
            texturedSlices.push_back(TexturedSlice{ slice.start, slice.count, texture, slice.name });
        } else {
            untexturedIndices.insert(untexturedIndices.end(), mesh->indices.begin() + static_cast<std::ptrdiff_t>(slice.start),
                mesh->indices.begin() + static_cast<std::ptrdiff_t>(slice.start + slice.count));
        }
    }

    std::vector<MeshAssetPart> parts;

    // Untextured combined submesh (position+normal), packed with the same helper AnimationSystem re-uploads with.
    if (!untexturedIndices.empty()) {
        const std::vector<MeshVertex> vertices = PackMeshVertices(mesh->positions, mesh->normals);

        Mesh gpuMesh = skinned
            ? renderer.CreateSkinnedMesh(vertices.data(), vertices.size() * sizeof(MeshVertex),
                  static_cast<std::uint32_t>(vertices.size()), untexturedIndices.data(),
                  untexturedIndices.size() * sizeof(std::uint32_t),
                  static_cast<std::uint32_t>(untexturedIndices.size()), "ImportedMesh")
            : renderer.CreateMesh(vertices.data(), vertices.size() * sizeof(MeshVertex),
                  static_cast<std::uint32_t>(vertices.size()), untexturedIndices.data(),
                  untexturedIndices.size() * sizeof(std::uint32_t),
                  static_cast<std::uint32_t>(untexturedIndices.size()), "ImportedMesh");
        // Tight local-space bounds, computed once at load; used by GPU-driven culling.
        gpuMesh.SetLocalBounds(ComputeSubsetLocalAABB(mesh->positions, untexturedIndices));
        const MeshHandle handle = renderSystem.RegisterMesh(std::move(gpuMesh));
        // Index copy is kept only for skinned models (needed to build a GPU-skinned counterpart).
        MeshAssetPart part{ handle, kInvalidMaterialTextureHandle, std::string() };
        if (skinned) {
            part.indices = untexturedIndices;
        }
        parts.push_back(std::move(part));
    }

    // Textured submeshes (position+normal+UV): one shared vertex buffer is uploaded once; each submesh Mesh
    // points at it and differs only in its own index buffer/range.
    if (!texturedSlices.empty()) {
        const std::vector<MeshVertexUv> texturedVertices = PackMeshVertexUvs(mesh->positions, mesh->normals, mesh->uvs);
        const uint64_t texturedVertexDataSize = texturedVertices.size() * sizeof(MeshVertexUv);
        const std::uint32_t texturedVertexCount = static_cast<std::uint32_t>(texturedVertices.size());

        const std::shared_ptr<Buffer> sharedTexturedVertexBuffer = skinned
            ? renderer.CreateSharedSkinnedMeshVertexBuffer(
                  texturedVertices.data(), texturedVertexDataSize, "ImportedTexturedMeshShared")
            : renderer.CreateSharedMeshVertexBuffer(
                  texturedVertices.data(), texturedVertexDataSize, "ImportedTexturedMeshShared");

        for (const TexturedSlice& slice : texturedSlices) {
            const std::vector<std::uint32_t> sliceIndices(
                mesh->indices.begin() + static_cast<std::ptrdiff_t>(slice.start),
                mesh->indices.begin() + static_cast<std::ptrdiff_t>(slice.start + slice.count));

            Mesh gpuMesh = renderer.CreateMeshFromSharedVertexBuffer(sharedTexturedVertexBuffer, texturedVertexCount,
                sliceIndices.data(), sliceIndices.size() * sizeof(std::uint32_t),
                static_cast<std::uint32_t>(sliceIndices.size()), "ImportedTexturedMesh");
            // Tight local-space bounds for this submesh; textured batches are not GPU-driven yet.
            gpuMesh.SetLocalBounds(ComputeSubsetLocalAABB(mesh->positions, sliceIndices));
            const MeshHandle handle = renderSystem.RegisterMesh(std::move(gpuMesh));
            MeshAssetPart part{ handle, slice.texture, slice.name };
            if (skinned) {
                part.indices = sliceIndices;
            }
            parts.push_back(std::move(part));
        }
    }

    // Keep the bind-pose CPU data and skeleton for the explicit hand-off to AnimationSystem (see TryGetSkinnedMeshData()).
    if (skinned) {
        SkinnedMeshData skinData;
        skinData.bindPositions = mesh->positions;
        skinData.bindNormals.resize(mesh->positions.size());
        skinData.uvs.resize(mesh->positions.size());
        const bool hasNormals = mesh->normals.size() == mesh->positions.size();
        const bool hasUvs = mesh->uvs.size() == mesh->positions.size();
        for (std::size_t i = 0; i < mesh->positions.size(); ++i) {
            skinData.bindNormals[i] = hasNormals ? mesh->normals[i] : Vec3::Up();
            skinData.uvs[i] = hasUvs ? mesh->uvs[i] : Vec2::Zero();
        }
        skinData.skinWeights = rig->skinWeights;
        skinData.skeleton = rig->skeleton;
        skinData.physics = rig->physics;
        skinData.jointPhysicsOverrides = rig->jointPhysicsOverrides;
        m_skinnedMeshCache.insert_or_assign(absoluteGtaPath, std::move(skinData));
    }

    const auto inserted = m_meshAssetCache.emplace(absoluteGtaPath, std::move(parts));
    return inserted.first->second;
}

EntityBlueprint MeshAssetGpuCatalog::Resolve(RenderSystem& renderSystem, Renderer& renderer,
    const std::string& absoluteGtaPath, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    const std::vector<MeshAssetPart>& parts = EnsureMeshAsset(renderSystem, renderer, absoluteGtaPath);

    EntityBlueprint root; // children left empty => caller treats this as a failure.
    if (parts.empty()) {
        return root;
    }

    // Root node: a bare hierarchy node named after the file stem ("Miku.gta" -> "Miku"), tagged with the source path.
    root.name = PathToUtf8(Utf8PathFromGamePath(absoluteGtaPath).stem());
    root.meshAssetSourcePath = absoluteGtaPath;

    for (const MeshAssetPart& part : parts) {
        const PipelineHandle pipeline = part.texture.IsValid()
            ? EnsureTexturedMeshPipeline(renderSystem, renderer, sceneServicesSetLayout)
            : EnsureMeshPipeline(renderSystem, renderer, sceneServicesSetLayout);

        EntityBlueprintNode child;
        child.mesh = part.mesh;
        child.pipeline = pipeline;
        child.texture = part.texture;
        child.name = part.name;
        root.children.push_back(std::move(child));
    }

    return root;
}

const std::vector<MeshAssetPart>* MeshAssetGpuCatalog::TryGetParts(const std::string& absoluteGtaPath) const
{
    const auto found = m_meshAssetCache.find(absoluteGtaPath);
    return found != m_meshAssetCache.end() ? &found->second : nullptr;
}

const SkinnedMeshData* MeshAssetGpuCatalog::TryGetSkinnedMeshData(const std::string& absoluteGtaPath) const
{
    const auto found = m_skinnedMeshCache.find(absoluteGtaPath);
    return found != m_skinnedMeshCache.end() ? &found->second : nullptr;
}

bool MeshAssetGpuCatalog::RefreshCachedJointPhysicsOverridesFromDisk(const std::string& absoluteGtaPath)
{
    const auto found = m_skinnedMeshCache.find(absoluteGtaPath);
    if (found == m_skinnedMeshCache.end()) {
        return false; // Never cached - nothing to refresh.
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return false; // Keep the existing cached entry on a failed refresh.
    }
    const std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    if (!rig.has_value()) {
        return false;
    }

    found->second.jointPhysicsOverrides = rig->jointPhysicsOverrides;
    return true;
}

void MeshAssetGpuCatalog::InvalidateCachedMeshAsset(const std::string& absoluteGtaPath)
{
    m_meshAssetCache.erase(absoluteGtaPath);
    m_skinnedMeshCache.erase(absoluteGtaPath);
}

} // namespace gte
