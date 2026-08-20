#include "Game.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/Vertex.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"

namespace gte {

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

void Game::EnsureDemoSceneBuilt(Renderer& renderer)
{
    if (m_demoSceneBuilt) {
        return;
    }
    m_demoSceneBuilt = true;

    // Shader source lives at src/Shaders/Triangle.vert/.frag (version-
    // controlled); compiled to SPIR-V at build time by
    // cmake/CompileShaders.cmake into "<exe dir>/shaders/*.spv" (gitignored
    // - see .gitignore). Registered with RenderSystem (not kept as a raw
    // Pipeline member here) so a MeshRenderer component can reference it by
    // handle - see RenderSystem.h.
    const PipelineHandle trianglePipeline = m_renderSystem.RegisterPipeline(
        renderer.CreatePipeline("shaders/Triangle.vert.spv", "shaders/Triangle.frag.spv"));

    // Mesh-local positions on the XY plane (z=0) - one red, one green, one
    // blue vertex, so the rasterizer's interpolation across the triangle is
    // visible. Shared by every demo entity below - each one only differs in
    // its Transform, proving the push-constant model matrix (see
    // Renderer/Pipeline.cpp, Shaders/Triangle.vert) actually moves the SAME
    // mesh data to a different place in the world, all seen through the one
    // Camera entity created below (see Shaders/Triangle.vert's
    // `pc.viewProj * pc.model * vec4(inPosition, 0.0, 1.0)`).
    const Vertex vertices[3] = {
        { { 0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f } },
        { { -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
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
