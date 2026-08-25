#include "Game.h"

#include "../Animation/IkSolver.h"
#include "../Animation/MotionSampler.h"
#include "../Animation/SkeletonPose.h"
#include "../Animation/VertexSkinning.h"
#include "../Assets/AssetDatabase.h"
#include "../Assets/AssetTypes.h"
#include "../Assets/GtaFile.h"
#include "../Assets/Ktx2Decoder.h"
#include "../Assets/MaterialData.h"
#include "../Assets/MeshFile.h"
#include "../Assets/MotionFile.h"
#include "../Assets/RigFile.h"
#include "../Renderer/MeshVertex.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/Vertex.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshAssetSource.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Name.h"
#include "ECS/Components/SkeletalAnimator.h"
#include "ECS/Components/Transform.h"
#include "ECS/TransformHierarchy.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <optional>

namespace gte {

namespace {

// std::filesystem::path(const std::string&) goes through the OS's native
// narrow encoding (the current ANSI codepage on Windows), NOT UTF-8 - the
// same pitfall src/Editor/ProjectPanelData.h's PathToUtf8()/Utf8ToPath()
// helpers already exist to avoid for the Editor. Game.cpp can't include
// that Editor-only header (see AGENTS.md, Clean Architecture - src/Assets/
// and src/Game/ must never depend on src/Editor/), so this is that exact
// same std::u8string round-trip, duplicated here rather than shared, purely
// so `absoluteGtaPath` (always UTF-8 - it comes from an ImGui drag-and-drop
// payload built from ProjectPanel's own PathToUtf8() call, see
// Panels/ProjectPanel.cpp/HierarchyPanel.cpp) resolves to the correct file
// on disk even when the Project folder (or the asset's own filename)
// contains non-ASCII characters.
std::filesystem::path Utf8PathFromGamePath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// The exact inverse of Utf8PathFromGamePath() above - same std::u8string
// round-trip PmxLoader.cpp's own PathToUtf8() uses, duplicated here for the
// same "src/Assets/ and src/Game/ never depend on src/Editor/" reasoning.
// Used by CreateMeshEntityFromGtaFile() below to turn `absoluteGtaPath`'s
// own filename-minus-extension back into a UTF-8 std::string for the
// spawned root entity's Name component.
std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// VMD's own fixed frame grid - see MotionData.h's own file comment.
constexpr float kVmdFramesPerSecond = 30.0f;

} // namespace

void Game::OnEvent(const Event& /*event*/)
{
    // Discrete/one-shot event handling goes here (react to a single key
    // press, window resized, etc). event.type tells you which alternative of
    // event.data is active - see Event.h.
}

void Game::Update(double deltaSeconds, const InputState& /*input*/)
{
    // Game/simulation logic goes here. Poll `input` for continuous state,
    // e.g. `if (input.IsKeyDown(KeyCode::W)) { ... }` for held-key movement.

    UpdateSkeletalAnimators(deltaSeconds);
}

PipelineHandle Game::EnsureDefaultPipeline(Renderer& renderer)
{
    if (!m_defaultPipeline.IsValid()) {
        // Shader source lives at src/Shaders/Triangle.vert/.frag (version-
        // controlled); compiled to SPIR-V at build time by
        // cmake/CompileShaders.cmake into "<exe dir>/shaders/*.spv" (gitignored
        // - see .gitignore). Registered with RenderSystem (not kept as a raw
        // Pipeline member here) so a MeshRenderer component can reference it
        // by handle - see RenderSystem.h.
        m_defaultPipeline = m_renderSystem.RegisterPipeline(
            renderer.CreatePipeline("shaders/Triangle.vert.spv", "shaders/Triangle.frag.spv"));
    }
    return m_defaultPipeline;
}

MeshHandle Game::EnsurePrimitiveMesh(Renderer& renderer, PrimitiveType type)
{
    MeshHandle& cached = m_primitiveMeshes[static_cast<std::size_t>(type)];
    if (!cached.IsValid()) {
        const std::vector<Vertex> vertices = PrimitiveMeshGenerator::Generate(type);
        cached = m_renderSystem.RegisterMesh(renderer.CreateMesh(vertices.data(),
            vertices.size() * sizeof(Vertex), static_cast<std::uint32_t>(vertices.size()), ToString(type)));
    }
    return cached;
}

Entity Game::CreatePrimitiveEntity(Renderer& renderer, PrimitiveType type)
{
    const PipelineHandle pipeline = EnsureDefaultPipeline(renderer);
    const MeshHandle mesh = EnsurePrimitiveMesh(renderer, type);

    const Entity entity = m_registry.CreateEntity();
    m_registry.AddComponent<Transform>(entity); // Identity Transform - spawns at the world origin, like Unity.
    m_registry.AddComponent<MeshRenderer>(entity, MeshRenderer{ mesh, pipeline });
    return entity;
}

PipelineHandle Game::EnsureMeshPipeline(Renderer& renderer)
{
    if (!m_meshPipeline.IsValid()) {
        // Shader source lives at src/Shaders/Mesh.vert/.frag - see
        // VertexLayout::PositionNormal's own comment in Pipeline.h for why
        // this can't just reuse EnsureDefaultPipeline()'s Triangle.vert/
        // .frag pipeline (a different, incompatible vertex layout).
        m_meshPipeline = m_renderSystem.RegisterPipeline(renderer.CreatePipeline(
            "shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", VertexLayout::PositionNormal));
    }
    return m_meshPipeline;
}

PipelineHandle Game::EnsureTexturedMeshPipeline(Renderer& renderer)
{
    if (!m_texturedMeshPipeline.IsValid()) {
        // Shader source lives at src/Shaders/TexturedMesh.vert/.frag - see
        // VertexLayout::PositionNormalUv's own comment in Pipeline.h.
        // `useMaterialTexture = true` wires this Pipeline's VkPipelineLayout
        // up with GpuResourceFactory::MaterialDescriptorSetLayout(), so any
        // MaterialTexture's descriptor set (see EnsureMaterialTexture()
        // below) can be bound against it.
        m_texturedMeshPipeline = m_renderSystem.RegisterPipeline(renderer.CreatePipeline(
            "shaders/TexturedMesh.vert.spv", "shaders/TexturedMesh.frag.spv", VertexLayout::PositionNormalUv, true));
    }
    return m_texturedMeshPipeline;
}

TextureHandle Game::EnsureMaterialTexture(Renderer& renderer, const AssetDatabase& database, const Guid& textureGuid)
{
    if (!textureGuid.IsValid()) {
        return kInvalidTextureHandle;
    }

    if (const auto found = m_materialTextureCache.find(textureGuid); found != m_materialTextureCache.end()) {
        return found->second;
    }

    const AssetRecord* record = database.FindByGuid(textureGuid);
    if (record == nullptr || record->type != AssetType::Texture) {
        // Not currently tracked as a real Texture asset (moved/deleted
        // since import, or this *.gta predates texture-import support) -
        // see MaterialData::textures' own doc comment. Deliberately NOT
        // cached as a failure, so a texture that shows up later (or is
        // fixed) can succeed on a subsequent spawn without restarting the
        // process.
        return kInvalidTextureHandle;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(record->gtaPath));
    if (!gta.has_value()) {
        return kInvalidTextureHandle;
    }

    const std::optional<Ktx2DecodeResult> decoded = DecodeKtx2ToRgba8(gta->payload);
    if (!decoded.has_value()) {
        return kInvalidTextureHandle;
    }

    const TextureHandle handle = m_renderSystem.RegisterTexture(renderer.CreateMaterialTexture2D(
        decoded->rgba8Pixels.data(), static_cast<int>(decoded->width), static_cast<int>(decoded->height),
        "MaterialTexture"));
    m_materialTextureCache.emplace(textureGuid, handle);
    return handle;
}

const std::vector<Game::MeshAssetPart>& Game::EnsureMeshAsset(Renderer& renderer, const std::string& absoluteGtaPath)
{
    static const std::vector<MeshAssetPart> kEmpty;

    if (const auto found = m_meshAssetCache.find(absoluteGtaPath); found != m_meshAssetCache.end()) {
        return found->second;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return kEmpty; // Missing file, bad magic, or not a Mesh asset - see CreateMeshEntityFromGtaFile().
    }

    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty() || mesh->indices.size() < 3) {
        return kEmpty; // Corrupt/truncated payload, or an empty mesh.
    }

    // Materials/textures AND rig (skeleton/skin-weights) are optional
    // metadata (see RigFile.h) - absent (decode failure) for a *.gta
    // imported before this engine supported them (an old "GTERIG01"/
    // "GTERIG02" blob - see RigFile.h's own magic-version comment), or a
    // materialless/boneless .pmx (decodes fine, just with empty
    // MaterialData/SkeletonData). Kept in this OUTER scope (unlike the
    // original single-purpose `if` this replaced) so both `materials`
    // (below) and the skeleton/skin-weight extraction further down can
    // both read from the same decoded `rig`.
    MaterialData materials;
    std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    if (rig.has_value()) {
        materials = rig->materials;
    }

    // A model is "skinned" (animatable via Game::PlayAnimationOnEntity())
    // only when it carries BOTH a real bone hierarchy AND a per-vertex skin
    // weight for every vertex - see SkinnedMeshData's own doc comment
    // (Game.h). A boneless/riggless mesh (or one imported before rig
    // extraction existed) renders exactly as it always did: a plain,
    // immutable, device-local Mesh built via Renderer::CreateMesh().
    const bool skinned = rig.has_value() && !rig->skeleton.bones.empty()
        && rig->skinWeights.size() == mesh->positions.size();

    // Every material's texture is referenced purely by Guid (see
    // MaterialTextureRef, MaterialData.h) - resolving one to an actual
    // *.gta Texture asset's absolute path needs a real AssetDatabase scan.
    // A fresh, purely local AssetDatabase (never persisted/shared with the
    // Editor's own ProjectPanel) scanned over this mesh *.gta's own parent
    // directory is enough: AssetImporter.cpp always writes every material
    // texture this model references into a "<meshFileStem>_Textures"
    // sibling folder right next to the mesh's own *.gta (see
    // AssetImporter.cpp's ImportPmxMaterialTextures()), so a recursive scan
    // rooted there is guaranteed to find them regardless of how deep inside
    // the Project this particular model happens to live.
    AssetDatabase textureDatabase;
    if (!materials.textures.empty()) {
        textureDatabase.RefreshFromDirectory(Utf8PathFromGamePath(absoluteGtaPath).parent_path());
    }

    // --- Partition MeshData::indices into "textured" (one submesh per
    // material with a resolvable diffuse texture) vs. "untextured" (every
    // other material, merged into ONE combined submesh) index ranges - see
    // Material::indexCount's own doc comment (MaterialData.h) for why
    // materials always own a contiguous run of the index list.
    struct TexturedSlice {
        std::size_t start = 0;
        std::size_t count = 0;
        TextureHandle texture;
        // Straight copy of the originating Material::name - see
        // MeshAssetPart::name's own doc comment (Game.h) for what an empty
        // value here ends up meaning for the spawned entity.
        std::string name;
    };
    std::vector<std::uint32_t> untexturedIndices;
    std::vector<TexturedSlice> texturedSlices;

    std::size_t cursor = 0;
    for (const Material& material : materials.materials) {
        std::size_t start = cursor;
        std::size_t count = material.indexCount;
        if (start > mesh->indices.size()) {
            start = mesh->indices.size();
            count = 0;
        } else if (start + count > mesh->indices.size()) {
            // A corrupt/mismatched *.gta whose material index counts don't
            // actually sum to the mesh's own index count - clamp rather
            // than read out of range.
            count = mesh->indices.size() - start;
        }
        cursor = start + count;

        TextureHandle texture = kInvalidTextureHandle;
        if (material.textureIndex >= 0
            && static_cast<std::size_t>(material.textureIndex) < materials.textures.size()) {
            const Guid& textureGuid = materials.textures[static_cast<std::size_t>(material.textureIndex)].guid;
            texture = EnsureMaterialTexture(renderer, textureDatabase, textureGuid);
        }

        if (texture.IsValid() && count > 0) {
            texturedSlices.push_back(TexturedSlice{ start, count, texture, material.name });
        } else if (count > 0) {
            untexturedIndices.insert(untexturedIndices.end(), mesh->indices.begin() + static_cast<std::ptrdiff_t>(start),
                mesh->indices.begin() + static_cast<std::ptrdiff_t>(start + count));
        }
    }
    // Anything past the last material's own run (normally nothing when
    // materials cover the whole mesh - the common PMX case; everything,
    // when `materials.materials` is empty - a materialless mesh, or a
    // *.gta imported before material support existed) is untextured too.
    if (cursor < mesh->indices.size()) {
        untexturedIndices.insert(
            untexturedIndices.end(), mesh->indices.begin() + static_cast<std::ptrdiff_t>(cursor), mesh->indices.end());
    }

    std::vector<MeshAssetPart> parts;

    const bool hasNormals = mesh->normals.size() == mesh->positions.size();
    const bool hasUvs = mesh->uvs.size() == mesh->positions.size();

    // --- Untextured combined submesh (position+normal only, exactly this
    // engine's original pre-material-import Mesh/Pipeline shape) - built as
    // a CPU-writable Mesh (Renderer::CreateSkinnedMesh()) instead of the
    // usual immutable one whenever `skinned` is true, so
    // Game::UpdateSkeletalAnimators() can re-upload it every frame.
    if (!untexturedIndices.empty()) {
        std::vector<MeshVertex> vertices(mesh->positions.size());
        for (std::size_t i = 0; i < mesh->positions.size(); ++i) {
            const Vec3& p = mesh->positions[i];
            vertices[i].position[0] = p.x;
            vertices[i].position[1] = p.y;
            vertices[i].position[2] = p.z;
            const Vec3 n = hasNormals ? mesh->normals[i] : Vec3::Up();
            vertices[i].normal[0] = n.x;
            vertices[i].normal[1] = n.y;
            vertices[i].normal[2] = n.z;
        }

        Mesh gpuMesh = skinned
            ? renderer.CreateSkinnedMesh(vertices.data(), vertices.size() * sizeof(MeshVertex),
                  static_cast<std::uint32_t>(vertices.size()), untexturedIndices.data(),
                  untexturedIndices.size() * sizeof(std::uint32_t),
                  static_cast<std::uint32_t>(untexturedIndices.size()), "ImportedMesh")
            : renderer.CreateMesh(vertices.data(), vertices.size() * sizeof(MeshVertex),
                  static_cast<std::uint32_t>(vertices.size()), untexturedIndices.data(),
                  untexturedIndices.size() * sizeof(std::uint32_t),
                  static_cast<std::uint32_t>(untexturedIndices.size()), "ImportedMesh");
        const MeshHandle handle = m_renderSystem.RegisterMesh(std::move(gpuMesh));
        parts.push_back(MeshAssetPart{ handle, kInvalidTextureHandle, std::string() });
    }

    // --- Textured submeshes (position+normal+UV) - one shared vertex
    // buffer built once here, re-uploaded per submesh Mesh (see Game.h's
    // own doc comment on CreateMeshEntityFromGtaFile() for why this
    // duplication is an acceptable, simple trade-off for now). Same
    // skinned/CreateSkinnedMesh() choice as the untextured branch above.
    if (!texturedSlices.empty()) {
        std::vector<MeshVertexUv> texturedVertices(mesh->positions.size());
        for (std::size_t i = 0; i < mesh->positions.size(); ++i) {
            const Vec3& p = mesh->positions[i];
            texturedVertices[i].position[0] = p.x;
            texturedVertices[i].position[1] = p.y;
            texturedVertices[i].position[2] = p.z;
            const Vec3 n = hasNormals ? mesh->normals[i] : Vec3::Up();
            texturedVertices[i].normal[0] = n.x;
            texturedVertices[i].normal[1] = n.y;
            texturedVertices[i].normal[2] = n.z;
            const Vec2 uv = hasUvs ? mesh->uvs[i] : Vec2::Zero();
            texturedVertices[i].uv[0] = uv.x;
            texturedVertices[i].uv[1] = uv.y;
        }

        for (const TexturedSlice& slice : texturedSlices) {
            const std::vector<std::uint32_t> sliceIndices(
                mesh->indices.begin() + static_cast<std::ptrdiff_t>(slice.start),
                mesh->indices.begin() + static_cast<std::ptrdiff_t>(slice.start + slice.count));

            Mesh gpuMesh = skinned
                ? renderer.CreateSkinnedMesh(texturedVertices.data(), texturedVertices.size() * sizeof(MeshVertexUv),
                      static_cast<std::uint32_t>(texturedVertices.size()), sliceIndices.data(),
                      sliceIndices.size() * sizeof(std::uint32_t), static_cast<std::uint32_t>(sliceIndices.size()),
                      "ImportedTexturedMesh")
                : renderer.CreateMesh(texturedVertices.data(), texturedVertices.size() * sizeof(MeshVertexUv),
                      static_cast<std::uint32_t>(texturedVertices.size()), sliceIndices.data(),
                      sliceIndices.size() * sizeof(std::uint32_t), static_cast<std::uint32_t>(sliceIndices.size()),
                      "ImportedTexturedMesh");
            const MeshHandle handle = m_renderSystem.RegisterMesh(std::move(gpuMesh));
            parts.push_back(MeshAssetPart{ handle, slice.texture, slice.name });
        }
    }

    // Keep the bind-pose CPU data (+ skeleton) around for
    // Game::UpdateSkeletalAnimators() to re-skin every frame - see
    // SkinnedMeshData's own doc comment (Game.h). `uvs` is populated
    // regardless of whether any textured part actually exists (cheap, and
    // keeps this shape uniform) - see its own doc comment.
    if (skinned) {
        SkinnedMeshData skinData;
        skinData.bindPositions = mesh->positions;
        skinData.bindNormals.resize(mesh->positions.size());
        skinData.uvs.resize(mesh->positions.size());
        for (std::size_t i = 0; i < mesh->positions.size(); ++i) {
            skinData.bindNormals[i] = hasNormals ? mesh->normals[i] : Vec3::Up();
            skinData.uvs[i] = hasUvs ? mesh->uvs[i] : Vec2::Zero();
        }
        skinData.skinWeights = rig->skinWeights;
        skinData.skeleton = rig->skeleton;
        m_meshSkinningCache.emplace(absoluteGtaPath, std::move(skinData));
    }

    const auto inserted = m_meshAssetCache.emplace(absoluteGtaPath, std::move(parts));
    return inserted.first->second;
}

Entity Game::CreateMeshEntityFromGtaFile(Renderer& renderer, const std::string& absoluteGtaPath)
{
    const std::vector<MeshAssetPart>& parts = EnsureMeshAsset(renderer, absoluteGtaPath);
    if (parts.empty()) {
        return kInvalidEntity;
    }

    // Root entity: a plain Transform-only entity (no MeshRenderer of its
    // own, so RenderSystem::CollectRenderables() never draws it directly -
    // see Game.h's own doc comment on this function) named after the asset
    // FILE itself - `absoluteGtaPath`'s own filename, minus its extension
    // (e.g. "Miku.gta" -> "Miku"). Every part below becomes its CHILD, so
    // moving/rotating/scaling THIS entity moves the whole multi-part model
    // together. Also carries a MeshAssetSource (see ECS/Components/
    // MeshAssetSource.h) recording exactly which *.gta this model came
    // from - PlayAnimationOnEntity() looks this back up to find the
    // model's own cached skinning/rig data.
    const Entity root = m_registry.CreateEntity();
    m_registry.AddComponent<Transform>(root); // Identity Transform - spawns at the world origin, like Unity.
    m_registry.AddComponent<Name>(root, Name{ PathToUtf8(Utf8PathFromGamePath(absoluteGtaPath).stem()) });
    m_registry.AddComponent<MeshAssetSource>(root, MeshAssetSource{ absoluteGtaPath });

    for (const MeshAssetPart& part : parts) {
        const PipelineHandle pipeline =
            part.texture.IsValid() ? EnsureTexturedMeshPipeline(renderer) : EnsureMeshPipeline(renderer);

        const Entity entity = m_registry.CreateEntity();
        // Identity LOCAL Transform, relative to `root` above (set as parent
        // right below via SetParent()) - renders at exactly the same place
        // it always did, since `root` itself starts out at the world
        // origin with no rotation/scale.
        m_registry.AddComponent<Transform>(entity);
        m_registry.AddComponent<MeshRenderer>(entity, MeshRenderer{ part.mesh, pipeline, part.texture });
        if (!part.name.empty()) {
            // Named after the originating PMX material (see
            // MeshAssetPart::name's own doc comment, Game.h) - an empty
            // name (the combined untextured submesh, or a material PMX
            // itself left unnamed) deliberately leaves this entity without
            // a Name component at all, so "Hierarchy" falls back to its
            // usual synthesized "Entity %u" label for it.
            m_registry.AddComponent<Name>(entity, Name{ part.name });
        }

        // worldPositionStays=true is a no-op here in practice (both `root`
        // and `entity` start out at an identity world transform), but is
        // still the semantically correct call - this is a real attach, not
        // just field assignment, and SetParent() is also what appends this
        // part as `root`'s next sibling-ordered child (MoveToLastSibling())
        // so "Hierarchy" lists every part in a stable, deterministic order.
        SetParent(m_registry, entity, root, /*worldPositionStays=*/true);
    }

    return root;
}

const MotionData* Game::EnsureAnimationClip(const std::string& absoluteAnimationGtaPath)
{
    if (const auto found = m_animationClipCache.find(absoluteAnimationGtaPath);
        found != m_animationClipCache.end()) {
        return &found->second;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteAnimationGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Animation) {
        return nullptr; // Missing file, bad magic, or not an Animation asset.
    }

    std::optional<MotionData> motion = DecodeMotionDataFromBytes(gta->payload);
    if (!motion.has_value()) {
        return nullptr; // Corrupt/truncated payload.
    }

    const auto inserted = m_animationClipCache.emplace(absoluteAnimationGtaPath, std::move(*motion));
    return &inserted.first->second;
}

bool Game::PlayAnimationOnEntity(Entity targetEntity, const std::string& absoluteAnimationGtaPath)
{
    if (!m_registry.IsAlive(targetEntity)) {
        return false;
    }

    const MeshAssetSource* source = m_registry.TryGetComponent<MeshAssetSource>(targetEntity);
    if (source == nullptr) {
        return false; // Not a model root spawned by CreateMeshEntityFromGtaFile().
    }

    const auto skinIt = m_meshSkinningCache.find(source->gtaPath);
    if (skinIt == m_meshSkinningCache.end() || skinIt->second.skeleton.bones.empty()) {
        return false; // A boneless/riggless model - nothing to animate.
    }

    if (EnsureAnimationClip(absoluteAnimationGtaPath) == nullptr) {
        return false;
    }

    SkeletalAnimator& animator = m_registry.AddComponent<SkeletalAnimator>(targetEntity);
    animator.meshGtaPath = source->gtaPath;
    animator.animationGtaPath = absoluteAnimationGtaPath;
    animator.frame = 0.0f;
    animator.speed = 1.0f;
    animator.playing = true;
    animator.loop = true;
    return true;
}

void Game::UpdateSkeletalAnimators(double deltaSeconds)
{
    ComponentStorage<SkeletalAnimator>& animators = m_registry.Storage<SkeletalAnimator>();

    for (std::size_t i = 0; i < animators.Size(); ++i) {
        SkeletalAnimator& animator = animators.ComponentAt(i);
        if (!animator.playing || animator.animationGtaPath.empty()) {
            continue;
        }

        const auto meshIt = m_meshSkinningCache.find(animator.meshGtaPath);
        if (meshIt == m_meshSkinningCache.end()) {
            continue; // Its model's own skinning data isn't (or is no longer) cached - nothing to do.
        }
        const SkinnedMeshData& skinData = meshIt->second;

        const auto animIt = m_animationClipCache.find(animator.animationGtaPath);
        if (animIt == m_animationClipCache.end()) {
            continue; // Its clip isn't (or is no longer) cached.
        }
        const MotionData& motion = animIt->second;

        // Resolved once per distinct (mesh, animation) pair, then reused
        // every frame afterwards - see ResolvedAnimationBinding's own doc
        // comment (Animation/MotionSampler.h) for the actual bone-NAME
        // resolution/mismatch-tolerance logic.
        const std::string bindingKey = animator.meshGtaPath + '\x1F' + animator.animationGtaPath;
        auto bindingIt = m_resolvedAnimationBindingCache.find(bindingKey);
        if (bindingIt == m_resolvedAnimationBindingCache.end()) {
            bindingIt = m_resolvedAnimationBindingCache
                            .emplace(bindingKey, ResolveBoneTracksToSkeleton(skinData.skeleton, motion))
                            .first;
        }
        const ResolvedAnimationBinding& binding = bindingIt->second;

        animator.frame += static_cast<float>(deltaSeconds) * kVmdFramesPerSecond * animator.speed;
        if (binding.lastFrame > 0) {
            const float loopLength = static_cast<float>(binding.lastFrame) + 1.0f;
            if (animator.loop) {
                animator.frame = std::fmod(animator.frame, loopLength);
                if (animator.frame < 0.0f) {
                    animator.frame += loopLength;
                }
            } else if (animator.frame > static_cast<float>(binding.lastFrame)) {
                animator.frame = static_cast<float>(binding.lastFrame);
                animator.playing = false;
            }
        }

        std::vector<BoneLocalOffset> pose = SampleAnimationPose(binding, animator.frame);
        // A VMD dance motion keyframes an invisible IK TARGET bone at the
        // foot (MMD's own 左足ＩＫ/右足ＩＫ), never the thigh/knee bones
        // directly - without this pass those bones would stay at bind pose
        // for the whole clip (see Animation/IkSolver.h's own file comment
        // for the full "why" and how this CCD solve works). Must run AFTER
        // SampleAnimationPose() (so `pose` holds the IK bone's own animated
        // position to solve toward) and BEFORE ComputeSkinningMatrices()
        // (so the final skinning matrices reflect the solved thigh/knee
        // rotations too).
        SolveIkChains(skinData.skeleton, pose);
        const std::vector<Mat4> skinningMatrices = ComputeSkinningMatrices(skinData.skeleton, pose);

        std::vector<Vec3> skinnedPositions;
        std::vector<Vec3> skinnedNormals;
        SkinVertices(skinData.bindPositions, skinData.bindNormals, skinData.skinWeights, skinningMatrices,
            skinnedPositions, skinnedNormals);

        const auto partsIt = m_meshAssetCache.find(animator.meshGtaPath);
        if (partsIt == m_meshAssetCache.end()) {
            continue;
        }

        // Re-upload EVERY one of this model's mesh parts - each part's own
        // GPU vertex buffer holds a full copy of the whole model's vertex
        // data (see EnsureMeshAsset()'s own doc comments), so all of them
        // need the same freshly-skinned positions/normals, just reformatted
        // per part's own vertex layout (MeshVertex for an untextured part,
        // MeshVertexUv - with `skinData.uvs` folded back in - for a
        // textured one, chosen purely by MeshAssetPart::texture validity,
        // exactly like CreateMeshEntityFromGtaFile() already picks a
        // part's Pipeline).
        for (const MeshAssetPart& part : partsIt->second) {
            Mesh* gpuMesh = m_renderSystem.TryGetMesh(part.mesh);
            if (gpuMesh == nullptr) {
                continue;
            }

            if (part.texture.IsValid()) {
                std::vector<MeshVertexUv> vertices(skinnedPositions.size());
                for (std::size_t v = 0; v < skinnedPositions.size(); ++v) {
                    vertices[v].position[0] = skinnedPositions[v].x;
                    vertices[v].position[1] = skinnedPositions[v].y;
                    vertices[v].position[2] = skinnedPositions[v].z;
                    vertices[v].normal[0] = skinnedNormals[v].x;
                    vertices[v].normal[1] = skinnedNormals[v].y;
                    vertices[v].normal[2] = skinnedNormals[v].z;
                    const Vec2& uv = skinData.uvs[v];
                    vertices[v].uv[0] = uv.x;
                    vertices[v].uv[1] = uv.y;
                }
                gpuMesh->UpdateVertexData(vertices.data(), vertices.size() * sizeof(MeshVertexUv));
            } else {
                std::vector<MeshVertex> vertices(skinnedPositions.size());
                for (std::size_t v = 0; v < skinnedPositions.size(); ++v) {
                    vertices[v].position[0] = skinnedPositions[v].x;
                    vertices[v].position[1] = skinnedPositions[v].y;
                    vertices[v].position[2] = skinnedPositions[v].z;
                    vertices[v].normal[0] = skinnedNormals[v].x;
                    vertices[v].normal[1] = skinnedNormals[v].y;
                    vertices[v].normal[2] = skinnedNormals[v].z;
                }
                gpuMesh->UpdateVertexData(vertices.data(), vertices.size() * sizeof(MeshVertex));
            }
        }
    }
}

void Game::EnsureDemoSceneBuilt(Renderer& renderer)
{
    if (m_demoSceneBuilt) {
        return;
    }
    m_demoSceneBuilt = true;

    const PipelineHandle trianglePipeline = EnsureDefaultPipeline(renderer);

    // Mesh-local positions on the XY plane (z=0) - one red, one green, one
    // blue vertex, so the rasterizer's interpolation across the triangle is
    // visible. Shared by every demo entity below - each one only differs in
    // its Transform, proving the push-constant model matrix (see
    // Renderer/Pipeline.cpp, Shaders/Triangle.vert) actually moves the SAME
    // mesh data to a different place in the world, all seen through the one
    // Camera entity created below (see Shaders/Triangle.vert's
    // `pc.viewProj * pc.model * vec4(inPosition, 1.0)`).
    const Vertex vertices[3] = {
        { { 0.0f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.5f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { -0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
    };
    const MeshHandle triangleMesh =
        m_renderSystem.RegisterMesh(renderer.CreateMesh(vertices, sizeof(vertices), 3, "TriangleMesh"));

    // Three entities sharing the one mesh/pipeline above, spaced left/
    // center/right purely via Transform.position - proves RenderSystem
    // actually iterates every MeshRenderer (not just redrawing a single
    // hardcoded thing) and that each entity's world matrix independently
    // affects where it ends up on screen.
    const float positions[3] = { -0.6f, 0.0f, 0.6f };
    for (const float x : positions) {
        const Entity entity = m_registry.CreateEntity();

        Transform& transform = m_registry.AddComponent<Transform>(entity);
        transform.position = Vec3{ x, 0.0f, 0.0f };
        transform.scale = Vec3{ 0.4f, 0.4f, 0.4f };

        m_registry.AddComponent<MeshRenderer>(entity, MeshRenderer{ triangleMesh, trianglePipeline });
    }

    // One Camera entity, sitting back along -Z (behind the triangles above,
    // which all sit at z=0) with an identity rotation - Quat::Identity()
    // rotates Vec3::Forward() to (0,0,1), so this looks straight down +Z at
    // the origin, exactly where the triangles are (see Camera::ViewMatrix()
    // in ECS/Components/Camera.h). Proves RenderSystem actually resolves a
    // real view-projection matrix from an ECS entity rather than the
    // Mat4::Identity() fallback for "no Camera in the scene at all".
    const Entity cameraEntity = m_registry.CreateEntity();
    Transform& cameraTransform = m_registry.AddComponent<Transform>(cameraEntity);
    cameraTransform.position = Vec3{ 0.0f, 0.0f, -5.0f };
    m_registry.AddComponent<Camera>(cameraEntity);
}

void Game::Render(Renderer& renderer, float aspectWidthOverHeight, const Mat4* viewProjectionOverride)
{
    renderer.Clear(20, 20, 30, 255);

    EnsureDemoSceneBuilt(renderer);

    if (viewProjectionOverride != nullptr) {
        m_renderSystem.Draw(m_registry, renderer, *viewProjectionOverride);
    } else {
        m_renderSystem.Draw(m_registry, renderer, aspectWidthOverHeight);
    }
}

} // namespace gte
