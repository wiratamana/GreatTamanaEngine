#include "Game.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/Vertex.h"

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

void Game::Render(Renderer& renderer)
{
    renderer.Clear(20, 20, 30, 255);

    // Lazily build the hardcoded "hello triangle" GPU resources on first
    // use - see the member comments in Game.h. Shader source lives at
    // src/Shaders/Triangle.vert/.frag (version-controlled); compiled to
    // SPIR-V at build time by cmake/CompileShaders.cmake into
    // "<exe dir>/shaders/*.spv" (gitignored - see .gitignore).
    if (!m_trianglePipeline.has_value()) {
        m_trianglePipeline =
            renderer.CreatePipeline("shaders/Triangle.vert.spv", "shaders/Triangle.frag.spv");

        // Clip-space positions (Vulkan NDC: +Y is down) - one red, one
        // green, one blue vertex, so the rasterizer's interpolation across
        // the triangle is visible.
        const Vertex vertices[3] = {
            { { 0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
            { { 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f } },
            { { -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
        };
        m_triangleMesh = renderer.CreateMesh(vertices, sizeof(vertices), 3, "TriangleMesh");
    }

    // Queue this frame's draw - Renderer is the only thing that ever turns
    // this into actual vkCmd* calls (see Renderer::Submit()).
    renderer.Submit(*m_trianglePipeline, *m_triangleMesh);
}

} // namespace gte
