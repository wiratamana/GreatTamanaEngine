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

        commands.push_back(DrawCommand{ meshRenderer.mesh, meshRenderer.pipeline, meshRenderer.texture, model });
    }

    return commands;
}

Mat4 RenderSystem::ResolveActiveCameraViewProjection(Registry& registry, float aspectWidthOverHeight)
{
    ComponentStorage<Camera>& cameras = registry.Storage<Camera>();

    for (std::size_t i = 0; i < cameras.Size(); ++i) {
        const Camera& camera = cameras.ComponentAt(i);
        if (!camera.active) {
            continue;
        }

        const Entity entity = cameras.EntityAt(i);
        Transform transform; // Identity (origin, no rotation) if this camera entity has no Transform of its own.
        if (const Transform* found = registry.TryGetComponent<Transform>(entity)) {
            transform = *found;
        }

        return camera.ProjectionMatrix(aspectWidthOverHeight) * Camera::ViewMatrix(transform);
    }

    // No active Camera anywhere in the Registry - Identity() is the
    // multiplicative no-op, so every draw's clip-space position ends up
    // being exactly its model matrix's output, matching this engine's
    // original (pre-Camera) triangle-demo behavior.
    return Mat4::Identity();
}

void RenderSystem::Draw(Registry& registry, Renderer& renderer, float aspectWidthOverHeight)
{
    Draw(registry, renderer, ResolveActiveCameraViewProjection(registry, aspectWidthOverHeight));
}

void RenderSystem::Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection)
{
    for (const DrawCommand& command : CollectRenderables(registry)) {
        const Mesh* mesh = m_meshes.TryGet(command.mesh);
        const Pipeline* pipeline = m_pipelines.TryGet(command.pipeline);
        if (mesh != nullptr && pipeline != nullptr) {
            const MaterialTexture* materialTexture = m_textures.TryGet(command.texture);
            const VkDescriptorSet descriptorSet =
                materialTexture != nullptr ? materialTexture->descriptorSet : VK_NULL_HANDLE;
            renderer.Submit(*pipeline, *mesh, command.model, viewProjection, descriptorSet);
        }
    }
}

} // namespace gte
