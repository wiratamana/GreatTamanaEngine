// editor-core-separation-2 campaign, PHASE2 - this file now defines
// FrameDebuggerCaptureContext::RecordFrameDebuggerDraw(), the concrete
// implementation of IFrameDebuggerCaptureRecorder::RecordFrameDebuggerDraw()
// (src/Core/FrameDebuggerCaptureRecorder.h), reachable from gte_core-tier
// RenderSystem::Draw() only through a virtual call on a null-checked
// IFrameDebuggerCaptureRecorder* pointer - never a gte_editor-only
// free-function symbol by name. This closes the exact undefined-reference
// hazard editor-core-separation-1's own PHASE2
// (PHASE2_FRAME_DEBUGGER_CAPTURE_POINTER_SAFETY_FIX.md) left open: that
// phase moved this body OUT of RenderSystem.cpp into a free function
// (RecordFrameDebuggerDraws(), declared in src/Game/RenderSystem.h, defined
// only here) specifically because gte_core would otherwise carry an
// unresolved external symbol only gte_editor defines - but a free function
// called BY NAME from gte_core-tier code is still, itself, exactly that
// same hazard (see editor-core-separation-2's own PHASE0_MASTER_STRATEGY.md,
// "Defect A"). Behavior is UNCHANGED from the old free function's own body -
// only its shape (a member function instead of a free function taking a
// FrameDebuggerCaptureContext& parameter) and its call site
// (RenderSystem::Draw() now calls `capture->RecordFrameDebuggerDraw(...)`
// through the interface pointer, instead of
// `RecordFrameDebuggerDraws(*capture, ...)` by name) changed.

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
// bookkeeping) - see src/Core/FrameDebuggerCaptureRecorder.h's own doc
// comment on RecordFrameDebuggerDraw() for why this method exists at all.
// Behavior is UNCHANGED from what used to run inline inside
// RenderSystem::Draw()'s own `#if GTE_ENABLE_EDITOR` block, and later inside
// the free function this method replaces - only the physical location/shape
// moved.
void FrameDebuggerCaptureContext::RecordFrameDebuggerDraw(Registry& registry, Renderer& /*renderer*/, Entity entity,
    const Mesh& mesh, const Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4& viewProjection)
{
    // editor-core-separation-1 campaign, PHASE4 - names now live entirely on
    // the Editor side (EditorGpuMemoryNameOverlay); `renderer` is kept as a
    // parameter for signature stability with the interface's declaration
    // (and RenderSystem::Draw()'s own call site), even though it is no
    // longer used here.
    const std::string materialTextureDebugName = materialTexture != nullptr
        ? EditorGpuMemoryNameOverlay::GetDebugName(materialTexture->texture.Handle())
        : std::string();
    RecordDraw(pipeline.DebugName(), materialTextureDebugName, viewProjection);

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

    RecordEntityDraw(
        entity.index, entity.generation, displayName, pipeline.DebugName(), materialTextureDebugName, triangleCount);
}

} // namespace gte
