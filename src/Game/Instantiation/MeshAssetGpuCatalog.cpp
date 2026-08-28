#include "MeshAssetGpuCatalog.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Assets/AssetTypes.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/MaterialData.h"
#include "../../Assets/MeshFile.h"
#include "../../Assets/RigFile.h"
#include "../../Renderer/MeshVertex.h"
#include "../../Renderer/Renderer.h"
#include "MeshMaterialPartitioner.h"
#include "MeshVertexPacking.h"
#include "../RenderSystem.h"

#include <filesystem>
#include <optional>

namespace gte {

namespace {

// Same std::u8string round-trip Game.cpp's own Utf8PathFromGamePath() uses -
// see that function's doc comment for the full "why".
std::filesystem::path Utf8PathFromGamePath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

} // namespace

PipelineHandle MeshAssetGpuCatalog::EnsureMeshPipeline(RenderSystem& renderSystem, Renderer& renderer)
{
    if (!m_meshPipeline.IsValid()) {
        m_meshPipeline = renderSystem.RegisterPipeline(
            renderer.CreatePipeline("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", VertexLayout::PositionNormal));
    }
    return m_meshPipeline;
}

PipelineHandle MeshAssetGpuCatalog::EnsureTexturedMeshPipeline(RenderSystem& renderSystem, Renderer& renderer)
{
    if (!m_texturedMeshPipeline.IsValid()) {
        m_texturedMeshPipeline = renderSystem.RegisterPipeline(renderer.CreatePipeline("shaders/TexturedMesh.vert.spv",
            "shaders/TexturedMesh.frag.spv", VertexLayout::PositionNormalUv, true));
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

    // Materials/textures AND rig (skeleton/skin-weights) are optional
    // metadata (see RigFile.h) - absent for a *.gta imported before this
    // engine supported them, or a materialless/boneless .pmx.
    MaterialData materials;
    std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    if (rig.has_value()) {
        materials = rig->materials;
    }

    // A model is "skinned" only when it carries BOTH a real bone hierarchy
    // AND a per-vertex skin weight for every vertex - see SkinnedMeshData's
    // own doc comment (SkeletalRigCache.h).
    const bool skinned = rig.has_value() && !rig->skeleton.bones.empty()
        && rig->skinWeights.size() == mesh->positions.size();

    // Every material's texture is referenced purely by Guid (see
    // MaterialTextureRef, MaterialData.h) - resolving one to an actual
    // *.gta Texture asset's absolute path needs a real AssetDatabase scan, a
    // fresh, purely local one scanned over this mesh *.gta's own parent
    // directory (guaranteed to also cover its sibling "..._Textures"
    // folder - see AssetImporter.cpp's ImportPmxMaterialTextures()).
    AssetDatabase textureDatabase;
    if (!materials.textures.empty()) {
        textureDatabase.RefreshFromDirectory(Utf8PathFromGamePath(absoluteGtaPath).parent_path());
    }

    // Pure index-range partitioning (MeshMaterialPartitioner.h) - deciding
    // whether a slice ends up in the untextured merged bucket or gets its
    // own textured submesh is the one impure decision left to this
    // function (it needs the texture cache above).
    const std::vector<MeshMaterialSlice> slices = PartitionMeshMaterials(mesh->indices.size(), materials.materials);

    std::vector<std::uint32_t> untexturedIndices;
    struct TexturedSlice {
        std::size_t start = 0;
        std::size_t count = 0;
        TextureHandle texture;
        std::string name;
    };
    std::vector<TexturedSlice> texturedSlices;

    for (const MeshMaterialSlice& slice : slices) {
        TextureHandle texture = kInvalidTextureHandle;
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

    // --- Untextured combined submesh (position+normal only) - built via
    // the SHARED PackMeshVertices() helper (MeshVertexPacking.h), the exact
    // same function AnimationSystem's per-frame re-upload uses.
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
        const MeshHandle handle = renderSystem.RegisterMesh(std::move(gpuMesh));
        parts.push_back(MeshAssetPart{ handle, kInvalidTextureHandle, std::string() });
    }

    // --- Textured submeshes (position+normal+UV) - one shared vertex
    // buffer built once here (via the SHARED PackMeshVertexUvs() helper) AND
    // uploaded to the GPU exactly ONCE (see Renderer::CreateSharedMeshVertexBuffer()/
    // CreateSharedSkinnedMeshVertexBuffer()) - every textured submesh Mesh
    // below points at that SAME underlying vertex buffer, differing only in
    // its own index buffer/range. This is Stage 1 of
    // task_manager/optimizing_multi_thread_cpu_skinning/
    // MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md: a model with N
    // textured materials used to get N full, independent copies of this
    // same vertex data (both at load time AND, far more expensively, on
    // every single animated frame afterwards) - now it gets exactly one.
    if (!texturedSlices.empty()) {
        const std::vector<MeshVertexUv> texturedVertices = PackMeshVertexUvs(mesh->positions, mesh->normals, mesh->uvs);
        const VkDeviceSize texturedVertexDataSize = texturedVertices.size() * sizeof(MeshVertexUv);
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
            const MeshHandle handle = renderSystem.RegisterMesh(std::move(gpuMesh));
            parts.push_back(MeshAssetPart{ handle, slice.texture, slice.name });
        }
    }

    // Keep the bind-pose CPU data (+ skeleton) around so it can be handed
    // off to AnimationSystem's SkeletalRigCache explicitly (see this
    // class's own doc comment, and TryGetSkinnedMeshData() below) - never
    // pushed into any animation-owned cache from inside this function.
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
        m_skinnedMeshCache.insert_or_assign(absoluteGtaPath, std::move(skinData));
    }

    const auto inserted = m_meshAssetCache.emplace(absoluteGtaPath, std::move(parts));
    return inserted.first->second;
}

EntityBlueprint MeshAssetGpuCatalog::Resolve(RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath)
{
    const std::vector<MeshAssetPart>& parts = EnsureMeshAsset(renderSystem, renderer, absoluteGtaPath);

    EntityBlueprint root; // children left empty => caller treats this as a failure.
    if (parts.empty()) {
        return root;
    }

    // Root node: a bare hierarchy node (no mesh of its own), named after the
    // asset FILE itself - absoluteGtaPath's own filename, minus its
    // extension (e.g. "Miku.gta" -> "Miku") - and tagged with the source
    // path so a MeshAssetSource component gets attached to it.
    root.name = PathToUtf8(Utf8PathFromGamePath(absoluteGtaPath).stem());
    root.meshAssetSourcePath = absoluteGtaPath;

    for (const MeshAssetPart& part : parts) {
        const PipelineHandle pipeline =
            part.texture.IsValid() ? EnsureTexturedMeshPipeline(renderSystem, renderer) : EnsureMeshPipeline(renderSystem, renderer);

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

} // namespace gte
