#include "MeshAssetGpuCatalog.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Assets/AssetTypes.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/MaterialData.h"
#include "../../Assets/MeshFile.h"
#include "../../Assets/RigFile.h"
#include "../../Core/Logging.h"
#include "../../Encoding/Utf8Path.h"
#include "../../Renderer/MeshVertex.h"
#include "../../Renderer/Culling/CullingTypes.h" // ComputeLocalAABB()
#include "../../Renderer/Renderer.h"
#include "MaterialLifetimeRules.h"
#include "../../Assets/MeshMaterialPartitioner.h"
#include "MeshVertexPacking.h"
#include "../RenderSystem.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>

namespace gte {

namespace {

constexpr const char* kLogCategory = "MeshAssetGpuCatalog";

// One draw pipeline per MeshAssetPipelineKind, in enum order.
struct PipelineRow {
    const char* vertexShader;
    const char* fragmentShader;
    VertexLayout layout;
    bool useMaterialTexture;
    const char* debugName;
};

constexpr std::array<PipelineRow, 5> kPipelineRows{ {
    { "shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", VertexLayout::PositionNormal, false,
        "Mesh.vert/Mesh.frag (PositionNormal)" },
    { "shaders/TexturedMesh.vert.spv", "shaders/TexturedMesh.frag.spv", VertexLayout::PositionNormalUv, true,
        "TexturedMesh.vert/TexturedMesh.frag (PositionNormalUv)" },
    { "shaders/TexturedMesh.vert.spv", "shaders/TexturedMeshAlphaTest.frag.spv", VertexLayout::PositionNormalUv, true,
        "TexturedMesh.vert/TexturedMeshAlphaTest.frag (PositionNormalUv)" },
    { "shaders/PbrMesh.vert.spv", "shaders/PbrMesh.frag.spv", VertexLayout::PositionNormalUvTangent, true,
        "PbrMesh.vert/PbrMesh.frag (PositionNormalUvTangent)" },
    { "shaders/PbrMesh.vert.spv", "shaders/PbrMeshAlphaTest.frag.spv", VertexLayout::PositionNormalUvTangent, true,
        "PbrMesh.vert/PbrMeshAlphaTest.frag (PositionNormalUvTangent)" },
} };

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

bool IsValidMaterialIndex(std::int32_t index, std::size_t count) noexcept
{
    return index >= 0 && static_cast<std::size_t>(index) < count;
}

// Texture Guids of one material (Guid::Invalid() for an unused or out-of-range slot).
MaterialTextureSet TextureSetOf(const Material& m, const MaterialData& data, bool withDataImages)
{
    const auto guidAt = [&](std::int32_t index) {
        return IsValidMaterialIndex(index, data.textures.size()) ? data.textures[static_cast<std::size_t>(index)].guid
                                                                 : Guid::Invalid();
    };
    MaterialTextureSet set;
    set.baseColor = guidAt(m.textureIndex);
    if (withDataImages) {
        set.normal = guidAt(m.normalTextureIndex);
        set.metallicRoughness = guidAt(m.metallicRoughnessTextureIndex);
        set.occlusion = guidAt(m.occlusionTextureIndex);
    }
    return set;
}

} // namespace

PipelineHandle MeshAssetGpuCatalog::EnsurePipeline(MeshAssetPipelineKind kind, RenderSystem& renderSystem,
    Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    const std::size_t index = static_cast<std::size_t>(kind);
    PipelineHandle& slot = m_pipelines[index];
    if (!slot.IsValid()) {
        const PipelineRow& row = kPipelineRows[index];
        slot = renderSystem.RegisterPipeline(renderer.CreatePipeline(row.vertexShader, row.fragmentShader, row.layout,
            row.useMaterialTexture, row.debugName, /*useInstanceBuffer=*/false, sceneServicesSetLayout));
    }
    return slot;
}

const std::vector<MeshAssetPart>& MeshAssetGpuCatalog::EnsureMeshAsset(
    RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath)
{
    static const std::vector<MeshAssetPart> kEmpty;

    if (const auto found = m_meshAssetCache.find(absoluteGtaPath); found != m_meshAssetCache.end()) {
        return found->second;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return kEmpty; // Missing file, bad magic, or not a Mesh asset.
    }

    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty() || mesh->indices.size() < 3) {
        return kEmpty; // Corrupt/truncated payload, or an empty mesh.
    }

    // A call that fails half way can leave unused materials behind; the next sweep frees them.
    m_releasePending = true;

    // Materials/textures and rig are optional metadata (absent for materialless/boneless models).
    MaterialData materials;
    std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    if (rig.has_value()) {
        materials = rig->materials;
    } else if (!gta->metadata.empty()) {
        GTE_LOG_WARNING(kLogCategory,
            "Material block of " + absoluteGtaPath + " is corrupt; the mesh is drawn without materials.");
    }

    // Skinned = a bone hierarchy AND a skin weight for every vertex.
    const bool skinned = rig.has_value() && !rig->skeleton.bones.empty()
        && rig->skinWeights.size() == mesh->positions.size();

    // Textures are referenced by Guid; resolve them via a local scan of the mesh's own directory
    // (which also covers its sibling "..._Textures" folder).
    AssetDatabase textureDatabase;
    if (!materials.textures.empty()) {
        textureDatabase.RefreshFromDirectory(PathFromUtf8(absoluteGtaPath).parent_path());
    }

    // Pure index-range partitioning; whether a slice ends up textured depends on the texture cache below.
    const std::vector<MeshMaterialSlice> slices = PartitionMeshMaterials(mesh->indices.size(), materials.materials);

    // All textured slices share one vertex buffer, so the layout is decided for the whole mesh.
    bool everyTexturedSliceIsPbr = true;
    for (const MeshMaterialSlice& slice : slices) {
        if (IsValidMaterialIndex(slice.materialIndex, materials.materials.size())
            && !UsesPbrShading(materials.materials[static_cast<std::size_t>(slice.materialIndex)])) {
            everyTexturedSliceIsPbr = false;
        }
    }
    const bool tangentLayout = CanUseTangentLayout(
        skinned, mesh->positions.size(), mesh->tangents.size(), everyTexturedSliceIsPbr);

    // Upload every image of the mesh in batched submits before the slice loop resolves materials.
    std::vector<ImageRequest> requests;
    for (const MeshMaterialSlice& slice : slices) {
        if (IsValidMaterialIndex(slice.materialIndex, materials.materials.size())) {
            AppendImageRequests(
                TextureSetOf(materials.materials[static_cast<std::size_t>(slice.materialIndex)], materials, tangentLayout),
                requests);
        }
    }
    m_materialTextureCache.PrepareImages(renderer, textureDatabase, requests);

    std::vector<std::uint32_t> untexturedIndices;
    struct TexturedSlice {
        std::size_t start = 0;
        std::size_t count = 0;
        MaterialTextureHandle texture;
        std::string name;
        bool alphaTested = false;
    };
    std::vector<TexturedSlice> texturedSlices;

    for (const MeshMaterialSlice& slice : slices) {
        MaterialTextureHandle texture = kInvalidMaterialTextureHandle;
        bool alphaTested = false;
        if (IsValidMaterialIndex(slice.materialIndex, materials.materials.size())) {
            const Material& material = materials.materials[static_cast<std::size_t>(slice.materialIndex)];
            MaterialTextureSet set = TextureSetOf(material, materials, tangentLayout);
            if (UsesPbrShading(material)) {
                alphaTested = material.alphaMode == AlphaMode::Mask;
                const MaterialParams params = MakeMaterialParams(material.diffuse.x, material.diffuse.y,
                    material.diffuse.z, material.diffuse.w, material.alphaCutoff, material.metallicFactor,
                    material.roughnessFactor, material.normalScale, material.occlusionStrength);
                texture = m_materialTextureCache.Resolve(renderSystem, renderer, textureDatabase, set, params);
                if (!texture.IsValid() && set.baseColor.IsValid()) {
                    set.baseColor = Guid::Invalid(); // Base image unavailable: flat factor color.
                    texture = m_materialTextureCache.Resolve(renderSystem, renderer, textureDatabase, set, params);
                }
            } else if (set.baseColor.IsValid()) {
                texture = m_materialTextureCache.Resolve(
                    renderSystem, renderer, textureDatabase, set, MaterialParams{});
            }
        }

        if (texture.IsValid()) {
            texturedSlices.push_back(TexturedSlice{ slice.start, slice.count, texture, slice.name, alphaTested });
        } else {
            // No material, or a non-glTF material without a usable texture: flat neutral grey.
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

    // Textured submeshes: one shared vertex buffer is uploaded once; each submesh Mesh
    // points at it and differs only in its own index buffer/range.
    if (!texturedSlices.empty()) {
        std::shared_ptr<Buffer> sharedTexturedVertexBuffer;
        std::uint32_t texturedVertexCount = 0;
        if (tangentLayout) {
            const std::vector<MeshVertexTangent> vertices =
                PackMeshVertexTangents(mesh->positions, mesh->normals, mesh->uvs, mesh->tangents);
            sharedTexturedVertexBuffer = renderer.CreateSharedMeshVertexBuffer(
                vertices.data(), vertices.size() * sizeof(MeshVertexTangent), "ImportedPbrMeshShared");
            texturedVertexCount = static_cast<std::uint32_t>(vertices.size());
        } else {
            const std::vector<MeshVertexUv> vertices = PackMeshVertexUvs(mesh->positions, mesh->normals, mesh->uvs);
            const std::uint64_t dataSize = vertices.size() * sizeof(MeshVertexUv);
            sharedTexturedVertexBuffer = skinned
                ? renderer.CreateSharedSkinnedMeshVertexBuffer(vertices.data(), dataSize, "ImportedTexturedMeshShared")
                : renderer.CreateSharedMeshVertexBuffer(vertices.data(), dataSize, "ImportedTexturedMeshShared");
            texturedVertexCount = static_cast<std::uint32_t>(vertices.size());
        }

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
            part.kind = ChooseMeshAssetPipelineKind(part.texture.IsValid(), tangentLayout, slice.alphaTested);
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

    std::unordered_set<MaterialTextureHandle> distinctMaterials;
    for (const TexturedSlice& slice : texturedSlices) {
        distinctMaterials.insert(slice.texture);
    }
    std::unordered_set<Guid> distinctImages;
    for (const ImageRequest& request : requests) {
        distinctImages.insert(request.guid);
    }
    GTE_LOG_INFO(kLogCategory,
        absoluteGtaPath + ": " + std::to_string(parts.size()) + " parts, layout "
            + (tangentLayout && !texturedSlices.empty() ? "PositionNormalUvTangent" : "PositionNormalUv") + ", "
            + std::to_string(distinctMaterials.size()) + " materials, " + std::to_string(distinctImages.size())
            + " images");

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
    root.name = PathToUtf8(PathFromUtf8(absoluteGtaPath).stem());
    root.meshAssetSourcePath = absoluteGtaPath;

    for (const MeshAssetPart& part : parts) {
        EntityBlueprintNode child;
        child.mesh = part.mesh;
        child.pipeline = EnsurePipeline(part.kind, renderSystem, renderer, sceneServicesSetLayout);
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

    const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(absoluteGtaPath));
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
    m_materialTextureCache.ForgetFailedImages();
    m_releasePending = true;
}

MaterialReleaseResult MeshAssetGpuCatalog::ReleaseUnusedMaterials(
    Registry& registry, RenderSystem& renderSystem, Renderer& renderer)
{
    std::unordered_set<MaterialTextureHandle> keep;
    for (const auto& [path, parts] : m_meshAssetCache) {
        for (const MeshAssetPart& part : parts) {
            if (part.texture.IsValid()) {
                keep.insert(part.texture);
            }
        }
    }

    // Cache-unused materials that an entity still draws with keep the sweep pending.
    const std::unordered_set<MaterialTextureHandle> inUse = CollectMaterialTexturesInUse(registry);
    const std::vector<MaterialTextureHandle> candidates = SelectUnusedMaterials(m_materialTextureCache.Materials(), keep);
    m_releasePending = std::any_of(candidates.begin(), candidates.end(),
        [&inUse](const MaterialTextureHandle handle) { return inUse.contains(handle); });

    keep.insert(inUse.begin(), inUse.end());
    const MaterialReleaseResult result = m_materialTextureCache.ReleaseUnused(renderSystem, renderer, keep);
    if (result.materialsFreed > 0 || result.imagesFreed > 0) {
        const MaterialCacheStats stats = m_materialTextureCache.GetStats();
        GTE_LOG_INFO(kLogCategory,
            "Released " + std::to_string(result.materialsFreed) + " materials and " + std::to_string(result.imagesFreed)
                + " images (" + std::to_string(stats.images) + " images, " + std::to_string(stats.materials)
                + " materials stay)");
    }
    return result;
}

} // namespace gte
