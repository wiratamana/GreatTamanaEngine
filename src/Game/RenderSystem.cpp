#include "RenderSystem.h"

#include "Renderer/Renderer.h"

namespace gte {

std::vector<DrawCommand> RenderSystem::CollectRenderables(Registry& registry)
{
    std::vector<DrawCommand> commands;

    ComponentStorage<MeshRenderer>& renderers = registry.Storage<MeshRenderer>();
    commands.reserve(renderers.Size());

    for (std::size_t i = 0; i < renderers.Size(); ++i) {
        const Entity entity = renderers.EntityAt(i);
        const MeshRenderer& meshRenderer = renderers.ComponentAt(i);

        Mat4 model = Mat4::Identity();
        if (const Transform* transform = registry.TryGetComponent<Transform>(entity)) {
            model = transform->LocalToWorldMatrix();
        }

        commands.push_back(DrawCommand{ meshRenderer.mesh, meshRenderer.pipeline, model });
    }

    return commands;
}

void RenderSystem::Draw(Registry& registry, Renderer& renderer)
{
    for (const DrawCommand& command : CollectRenderables(registry)) {
        const Mesh* mesh = m_meshes.TryGet(command.mesh);
        const Pipeline* pipeline = m_pipelines.TryGet(command.pipeline);
        if (mesh != nullptr && pipeline != nullptr) {
            renderer.Submit(*pipeline, *mesh, command.model);
        }
    }
}

} // namespace gte
