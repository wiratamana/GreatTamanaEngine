#include "RenderSystem.h"

#include "ECS/Components/Name.h"
#include "ECS/TransformHierarchy.h"
#include "Profiling/ScopeTimer.h"
#include "Renderer/Renderer.h"

// FrameDebuggerCaptureContext (src/Editor/FrameDebuggerCapture.h) is an
// Editor-only type - RenderSystem.h above only ever forward-declares it
// (see that header's own comment). This real #include, AND every actual
// dereference of a `capture` pointer below, must stay wrapped in
// `#if GTE_ENABLE_EDITOR` - a GTE_ENABLE_EDITOR=OFF build compiles this
// whole file fine either way (the forward declaration is enough), but
// would FAIL TO LINK if an unconditional RecordDraw() call site referenced
// a type/function that's never compiled into that configuration at all.
// See task_manager/frame-debugger-3/
// PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md's own Step 3.1b.
#if GTE_ENABLE_EDITOR
#include "../Editor/FrameDebuggerCapture.h"
#endif

namespace gte {

std::vector<DrawCommand> RenderSystem::CollectRenderables(Registry& registry)
{
    GTE_PROFILE_SCOPE("RenderSystem::CollectRenderables");

    std::vector<DrawCommand> commands;

    ComponentStorage<MeshRenderer>& renderers = registry.Storage<MeshRenderer>();
    commands.reserve(renderers.Size());

    for (std::size_t i = 0; i < renderers.Size(); ++i) {
        const Entity entity = renderers.EntityAt(i);
        const MeshRenderer& meshRenderer = renderers.ComponentAt(i);

        // ComputeWorldMatrix() (ECS/TransformHierarchy.h) walks this
        // entity's whole parent chain, composing parentWorld * local at
        // every level - for an entity with no parent (or no Transform at
        // all) this is exactly transform->LocalToWorldMatrix()/
        // Mat4::Identity(), the same fallback this used to compute inline
        // before parenting existed.
        const Mat4 model = ComputeWorldMatrix(registry, entity);

        commands.push_back(DrawCommand{ entity, meshRenderer.mesh, meshRenderer.pipeline, meshRenderer.texture, model });
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

        // ComputeWorldTransform() (ECS/TransformHierarchy.h) resolves this
        // camera entity's Transform through its whole parent chain first -
        // a Camera parented under a moving entity (e.g. a vehicle) now
        // genuinely follows it, matching Unity's own behavior. Falls back
        // to an identity Transform (origin, no rotation) when this camera
        // entity has no Transform of its own at all, same as before.
        Transform transform;
        if (registry.TryGetComponent<Transform>(entity) != nullptr) {
            transform = ComputeWorldTransform(registry, entity);
        }

        return camera.ProjectionMatrix(aspectWidthOverHeight) * Camera::ViewMatrix(transform);
    }

    // No active Camera anywhere in the Registry - Identity() is the
    // multiplicative no-op, so every draw's clip-space position ends up
    // being exactly its model matrix's output, matching this engine's
    // original (pre-Camera) triangle-demo behavior.
    return Mat4::Identity();
}

void RenderSystem::Draw(Registry& registry, Renderer& renderer, float aspectWidthOverHeight,
    FrameDebuggerCaptureContext* capture, std::optional<std::size_t> maxDrawCount)
{
    Draw(registry, renderer, ResolveActiveCameraViewProjection(registry, aspectWidthOverHeight), capture, maxDrawCount);
}

void RenderSystem::Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection,
    FrameDebuggerCaptureContext* capture, std::optional<std::size_t> maxDrawCount)
{
    GTE_PROFILE_SCOPE("RenderSystem::Draw");

    const std::vector<DrawCommand> commands = CollectRenderables(registry);

    // task_manager/frame-debugger-7 campaign, PHASE3
    // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.2) - `maxDrawCount`, when set, stops iterating after that many
    // COMMANDS have been considered (the loop's own iteration count - see
    // this method's own header doc comment for why this is NOT the same
    // thing as "successfully resolved draws"). std::nullopt (every
    // pre-existing call site) means "no cutoff" - iterate every command,
    // exactly the pre-PHASE3 behavior.
    std::size_t consideredCount = 0;
    for (const DrawCommand& command : commands) {
        if (maxDrawCount.has_value() && consideredCount >= *maxDrawCount) {
            break;
        }
        ++consideredCount;

        const Mesh* mesh = m_meshes.TryGet(command.mesh);
        const Pipeline* pipeline = m_pipelines.TryGet(command.pipeline);
        if (mesh != nullptr && pipeline != nullptr) {
            const MaterialTexture* materialTexture = m_textures.TryGet(command.texture);
            const VkDescriptorSet descriptorSet =
                materialTexture != nullptr ? materialTexture->descriptorSet : VK_NULL_HANDLE;

#if GTE_ENABLE_EDITOR
            // Zero-overhead-when-disarmed: this whole block collapses to
            // one already-taken "is this pointer null" branch when
            // `capture` is nullptr (the common case - every frame until
            // PHASE3 wires a real arming trigger, and every ordinary frame
            // afterward) - no string formatting, no vector work happens.
            // See task_manager/frame-debugger-3/
            // PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md's own Step 2.
            if (capture != nullptr) {
                const std::string materialTextureDebugName = materialTexture != nullptr
                    ? renderer.GetMemoryDebugName(materialTexture->texture.Handle())
                    : std::string();
                capture->RecordDraw(pipeline->DebugName(), materialTextureDebugName, viewProjection);

                // frame-debugger-6 campaign, PHASE3 - additionally record
                // this exact draw's own per-entity attribution facts (never
                // deduplicated, unlike RecordDraw()'s own name lists above -
                // see FrameDebuggerCaptureContext::RecordEntityDraw()'s own
                // doc comment). Triangle count uses the exact same
                // HasIndexBuffer() ? IndexCount()/3 : VertexCount()/3 rule
                // DrawStats.h::AccumulateDrawStats() already uses, so these
                // two counts can never drift apart.
                const std::uint32_t triangleCount =
                    mesh->HasIndexBuffer() ? (mesh->IndexCount() / 3) : (mesh->VertexCount() / 3);

                std::string displayName;
                if (const Name* name = registry.TryGetComponent<Name>(command.entity);
                    name != nullptr && !name->value.empty()) {
                    displayName = name->value;
                } else {
                    // Matches HierarchyPanel::BuildEntityLabel()'s own
                    // synthesized "Entity %u" fallback format exactly (minus
                    // its Camera-only " (Camera)" suffix, which never applies
                    // to a mesh-rendering draw) - see this phase's own Step 2.
                    displayName = "Entity " + std::to_string(command.entity.index);
                }

                capture->RecordEntityDraw(command.entity.index, command.entity.generation, displayName,
                    pipeline->DebugName(), materialTextureDebugName, triangleCount);
            }
#endif

            renderer.Submit(*pipeline, *mesh, command.model, viewProjection, descriptorSet);
        }
    }
}

} // namespace gte
