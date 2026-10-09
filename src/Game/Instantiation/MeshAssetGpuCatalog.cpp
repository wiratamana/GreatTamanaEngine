#include "MeshAssetGpuCatalog.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Assets/AssetTypes.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/MaterialData.h"
#include "../../Assets/MeshFile.h"
#include "../../Assets/RigFile.h"
#include "../../Renderer/MeshVertex.h"
#include "../../Renderer/Culling/CullingTypes.h" // ComputeLocalAABB() - render-pass-5 campaign, PHASE4.
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

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE4 (task_manager/render-pass-5/
// PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md, PHASE0's Locked
// Design Decision 3) - computes a TIGHT local-space AABB for one submesh
// (untextured or textured), i.e. only the subset of `positions` this
// submesh's own `indices` actually reference - not the whole model's
// vertex array, which `positions` itself always is (every submesh's
// `indices` are plain indices into that SAME shared array - see
// MeshData.h). An out-of-range index (should never happen for a
// successfully-decoded MeshData, but this is CPU-side data read from disk)
// is skipped rather than read out of bounds.
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
    RenderSystem& renderSystem, Renderer& renderer, VkDescriptorSetLayout sceneServicesSetLayout)
{
    if (!m_meshPipeline.IsValid()) {
        m_meshPipeline = renderSystem.RegisterPipeline(renderer.CreatePipeline("shaders/Mesh.vert.spv",
            "shaders/Mesh.frag.spv", VertexLayout::PositionNormal, false, "Mesh.vert/Mesh.frag (PositionNormal)",
            /*useInstanceBuffer=*/false, sceneServicesSetLayout));
    }
    return m_meshPipeline;
}

PipelineHandle MeshAssetGpuCatalog::EnsureTexturedMeshPipeline(
    RenderSystem& renderSystem, Renderer& renderer, VkDescriptorSetLayout sceneServicesSetLayout)
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
        // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
        // PHASE4 - real, tight local-space bounds for THIS submesh, computed
        // once here at load time (Locked Design Decision 3) - the one real
        // content path RenderSystem::CollectGpuDrivenBatches()'s eligibility
        // rule can actually select today (untextured, PositionNormal, and -
        // for a non-skinned model - indexed - see IsGpuDrivenEligible()).
        gpuMesh.SetLocalBounds(ComputeSubsetLocalAABB(mesh->positions, untexturedIndices));
        const MeshHandle handle = renderSystem.RegisterMesh(std::move(gpuMesh));
        // `indices` is only worth keeping around for a SKINNED model - see
        // MeshAssetPart::indices' own doc comment (MeshAssetGpuCatalog.h) -
        // a non-skinned model will never need a GPU-skinned counterpart
        // Mesh, so there is no reason to keep a second, redundant CPU-side
        // copy of its index data alive for the rest of this cache entry's
        // lifetime.
        MeshAssetPart part{ handle, kInvalidTextureHandle, std::string() };
        if (skinned) {
            part.indices = untexturedIndices;
        }
        parts.push_back(std::move(part));
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
            // GPU-Driven Frustum Culling + Indirect Draw campaign
            // (render-pass-5), PHASE4 - real, tight local-space bounds for
            // THIS textured submesh (see the untextured branch above's own
            // comment for the full reasoning). Textured (PositionNormalUv)
            // batches are out of scope for this campaign (PHASE0's Locked
            // Design Decision 8) - computed anyway for consistency/future
            // use, at negligible one-time load cost.
            gpuMesh.SetLocalBounds(ComputeSubsetLocalAABB(mesh->positions, sliceIndices));
            const MeshHandle handle = renderSystem.RegisterMesh(std::move(gpuMesh));
            MeshAssetPart part{ handle, slice.texture, slice.name };
            if (skinned) {
                part.indices = sliceIndices;
            }
            parts.push_back(std::move(part));
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
        skinData.physics = rig->physics; // PHASE4 - see SkinnedMeshData's own doc comment (SkeletalRigCache.h).
        skinData.jointPhysicsOverrides = rig->jointPhysicsOverrides; // task_manager/verlet-integration-11, PHASE1.
        m_skinnedMeshCache.insert_or_assign(absoluteGtaPath, std::move(skinData));
    }

    const auto inserted = m_meshAssetCache.emplace(absoluteGtaPath, std::move(parts));
    return inserted.first->second;
}

EntityBlueprint MeshAssetGpuCatalog::Resolve(RenderSystem& renderSystem, Renderer& renderer,
    const std::string& absoluteGtaPath, VkDescriptorSetLayout sceneServicesSetLayout)
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
        return false; // Never cached this session - nothing to refresh (see this method's own header comment).
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return false; // Leave the existing cached entry exactly as it was - never clear/corrupt it on a failed refresh.
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
