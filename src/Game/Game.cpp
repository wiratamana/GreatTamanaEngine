#include "Game.h"

#include "../Assets/AssetTypes.h"
#include "../Assets/GtaFile.h"
#include "../Assets/MeshFile.h"
#include "../Renderer/MeshVertex.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/Vertex.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"

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

} // namespace

void Game::OnEvent(const Event& /*event*/)
{
    // Discrete/one-shot event handling goes here (react to a single key
    // press, window resized, etc). event.type tells you which alternative of
    // event.data is active - see Event.h.
}

void Game::Update(double /*deltaSeconds*/, const InputState& /*input*/)
{
    // Game/simulation logic goes here. Poll `input` for continuous state,
    // e.g. `if (input.IsKeyDown(KeyCode::W)) { ... }` for held-key movement.
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

MeshHandle Game::EnsureMeshAsset(Renderer& renderer, const std::string& absoluteGtaPath)
{
    if (const auto found = m_meshAssetCache.find(absoluteGtaPath); found != m_meshAssetCache.end()) {
        return found->second;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return kInvalidMeshHandle; // Missing file, bad magic, or not a Mesh asset - see CreateMeshEntityFromGtaFile().
    }

    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty() || mesh->indices.size() < 3) {
        return kInvalidMeshHandle; // Corrupt/truncated payload, or an empty mesh.
    }

    // Build the GPU-side MeshVertex array (position+normal) from the
    // decoded MeshData - substituting Vec3::Up() for any vertex whose
    // normal is missing, same defensive fallback
    // AssetPreviewMesh::EnsureMeshUploaded() already uses for the
    // Inspector's own mesh preview (see TODO.md's own note on a smarter
    // future fallback).
    std::vector<MeshVertex> vertices(mesh->positions.size());
    const bool hasNormals = mesh->normals.size() == mesh->positions.size();
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

    const MeshHandle handle = m_renderSystem.RegisterMesh(renderer.CreateMesh(vertices.data(),
        vertices.size() * sizeof(MeshVertex), static_cast<std::uint32_t>(vertices.size()), mesh->indices.data(),
        mesh->indices.size() * sizeof(std::uint32_t), static_cast<std::uint32_t>(mesh->indices.size()),
        "ImportedMesh"));

    m_meshAssetCache.emplace(absoluteGtaPath, handle);
    return handle;
}

Entity Game::CreateMeshEntityFromGtaFile(Renderer& renderer, const std::string& absoluteGtaPath)
{
    const MeshHandle mesh = EnsureMeshAsset(renderer, absoluteGtaPath);
    if (!mesh.IsValid()) {
        return kInvalidEntity;
    }

    const PipelineHandle pipeline = EnsureMeshPipeline(renderer);

    const Entity entity = m_registry.CreateEntity();
    m_registry.AddComponent<Transform>(entity); // Identity Transform - spawns at the world origin, like Unity.
    m_registry.AddComponent<MeshRenderer>(entity, MeshRenderer{ mesh, pipeline });
    return entity;
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
