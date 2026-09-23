// task_manager/editor-core-separation-1 campaign, PHASE2
// (PHASE2_FRAME_DEBUGGER_CAPTURE_POINTER_SAFETY_FIX.md) - this file is the
// NEW home for the real body of RenderSystem::Draw()'s own former
// `#if GTE_ENABLE_EDITOR` block (src/Game/RenderSystem.cpp) - the part that
// dereferenced a `capture` pointer and called real methods
// (RecordDraw()/RecordEntityDraw()) on the complete
// FrameDebuggerCaptureContext type. That was a genuine link hazard once
// gte_editor becomes a real, separate CMake target (Phase 9): gte_core
// would otherwise carry an unresolved external symbol only gte_editor
// defines (see task_manager/editor-core-separation-1/
// PHASE0_MASTER_STRATEGY.md, Section 2.5).
//
// RecordFrameDebuggerDraws() is DECLARED in src/Game/RenderSystem.h
// (forward-declared FrameDebuggerCaptureContext& parameter only - legal,
// exactly like the pointer parameter it replaces) and DEFINED for real only
// here. This file lives under src/Editor/ and only ever compiles as part of
// the Editor source list, so it needs no `#if`/`#endif` guard of its own at
// all - its #include of "FrameDebuggerCapture.h" below is therefore
// unconditional.

#include "../Game/RenderSystem.h"

#include "../ECS/Components/Name.h"
#include "../Renderer/MaterialTexture.h"
#include "../Renderer/Mesh.h"
#include "../Renderer/Pipeline.h"
#include "../Renderer/Renderer.h"
#include "EditorGpuMemoryNameOverlay.h"
#include "FrameDebuggerCapture.h"

#include <cstdint>
#include <string>

namespace gte {

// frame-debugger-6 campaign, PHASE3 (per-entity attribution) plus
// task_manager/frame-debugger-3, PHASE1 (the original RecordDraw() name-list
// bookkeeping) - see RenderSystem.h's own doc comment on
// RecordFrameDebuggerDraws() for why this function exists at all. Behavior
// is UNCHANGED from what used to run inline inside RenderSystem::Draw()'s
// own `#if GTE_ENABLE_EDITOR` block - only the physical location moved.
void RecordFrameDebuggerDraws(FrameDebuggerCaptureContext& capture, Registry& registry, Renderer& /*renderer*/,
    Entity entity, const Mesh& mesh, const Pipeline& pipeline, const MaterialTexture* materialTexture,
    const Mat4& viewProjection)
{
    // editor-core-separation-1 campaign, PHASE4 - names now live entirely on
    // the Editor side (EditorGpuMemoryNameOverlay); `renderer` is kept as a
    // parameter for signature stability with RenderSystem.h's declaration
    // (and RenderSystem::Draw()'s own call site), even though it is no
    // longer used here.
    const std::string materialTextureDebugName = materialTexture != nullptr
        ? EditorGpuMemoryNameOverlay::GetDebugName(materialTexture->texture.Handle())
        : std::string();
    capture.RecordDraw(pipeline.DebugName(), materialTextureDebugName, viewProjection);

    // frame-debugger-6 campaign, PHASE3 - additionally record this exact
    // draw's own per-entity attribution facts (never deduplicated, unlike
    // RecordDraw()'s own name lists above - see
    // FrameDebuggerCaptureContext::RecordEntityDraw()'s own doc comment).
    // Triangle count uses the exact same
    // HasIndexBuffer() ? IndexCount()/3 : VertexCount()/3 rule
    // DrawStats.h::AccumulateDrawStats() already uses, so these two counts
    // can never drift apart.
    const std::uint32_t triangleCount = mesh.HasIndexBuffer() ? (mesh.IndexCount() / 3) : (mesh.VertexCount() / 3);

    std::string displayName;
    if (const Name* name = registry.TryGetComponent<Name>(entity); name != nullptr && !name->value.empty()) {
        displayName = name->value;
    } else {
        // Matches HierarchyPanel::BuildEntityLabel()'s own synthesized
        // "Entity %u" fallback format exactly (minus its Camera-only
        // " (Camera)" suffix, which never applies to a mesh-rendering draw).
        displayName = "Entity " + std::to_string(entity.index);
    }

    capture.RecordEntityDraw(
        entity.index, entity.generation, displayName, pipeline.DebugName(), materialTextureDebugName, triangleCount);
}

} // namespace gte
