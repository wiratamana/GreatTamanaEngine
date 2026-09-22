#pragma once

#include "../ECS/Entity.h"
#include "../Math/Mat4.h"
#include "../Renderer/MeshHandle.h"
#include "../Renderer/PipelineHandle.h"
#include "../Renderer/TextureHandle.h"

namespace gte {

// One queued draw call's worth of PLAIN data, extracted from the ECS world -
// a MeshHandle/PipelineHandle/TextureHandle triple (never a Mesh&/Pipeline*/
// MaterialTexture* - see ECS/Components/MeshRenderer.h) plus the world
// matrix to draw it with. Carries no live Renderer/Vulkan state at all,
// which is what keeps RenderSystem::CollectRenderables() callable with
// nothing but a Registry - no live GPU device, no Renderer, no ResourcePool
// needed - see AGENTS.md ("Testability & Regression Safety").
//
// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE4 (task_manager/render-pass-5/
// PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md) - this struct used to
// be defined inline inside RenderSystem.h. It was extracted into its own,
// standalone header so that BOTH RenderSystem.h AND the new, pure
// src/Game/RenderBatching.h (which groups a std::vector<DrawCommand> by
// (MeshHandle, PipelineHandle) - see that file) can depend on it without
// creating a circular #include between the two (RenderBatching.h needs
// DrawCommand's full definition for its own RenderBatchGroup::commands
// member; RenderSystem.h needs RenderBatching.h's own types for its new
// CollectGpuDrivenBatches() method). Zero behavior change - every field below
// is byte-for-byte identical to before this extraction.
struct DrawCommand {
    Entity entity; // frame-debugger-6 campaign, PHASE3 - the ECS entity this
                    // draw call came from, so a capture consumer (see
                    // FrameDebuggerCaptureContext::RecordEntityDraw()) can
                    // attribute this exact draw back to a real, selectable
                    // entity (its own Name, if any) rather than only an
                    // anonymous mesh/pipeline/texture triple.
    MeshHandle mesh;
    PipelineHandle pipeline;
    TextureHandle texture; // kInvalidTextureHandle (the default) means "no material texture" - see MeshRenderer::texture.
    Mat4 model = Mat4::Identity(); // Mat4's own default ctor is all-zero, NOT identity - see Math/Mat4.h.
};

} // namespace gte
