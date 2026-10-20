#include "../Game/RenderSystem.h"

#include "../ECS/Components/Name.h"
#include "../Renderer/MaterialImageRole.h"
#include "../Renderer/MaterialTexture.h"
#include "../Renderer/Mesh.h"
#include "../Renderer/Pipeline.h"
#include "../Renderer/Renderer.h"
#include "EditorGpuMemoryNameOverlay.h"
#include "FrameDebuggerCapture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Concrete recorder behind IFrameDebuggerCaptureRecorder: RenderSystem::Draw() reaches it only through the
// interface pointer, so gte_core never references an editor-only symbol.
void FrameDebuggerCaptureContext::RecordFrameDebuggerDraw(Registry& registry, Renderer& /*renderer*/, Entity entity,
    const Mesh& mesh, const Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4& viewProjection)
{
    // One slot per material image; debug names live on the editor side.
    std::vector<FrameDebuggerMaterialSlot> slots;
    if (materialTexture != nullptr) {
        slots.reserve(kMaterialImageRoleCount);
        for (std::size_t i = 0; i < kMaterialImageRoleCount; ++i) {
            const auto& image = materialTexture->images[i];
            slots.push_back({ MaterialImageRoleLabel(static_cast<MaterialImageRole>(i)),
                image ? EditorGpuMemoryNameOverlay::GetDebugName(image->Handle()) : std::string() });
        }
    }
    RecordDraw(pipeline.DebugName(), slots, viewProjection);

    // Same triangle rule as DrawStats.h::AccumulateDrawStats(), so the counts cannot drift apart.
    const std::uint32_t triangleCount = mesh.HasIndexBuffer() ? (mesh.IndexCount() / 3) : (mesh.VertexCount() / 3);

    std::string displayName;
    if (const Name* name = registry.TryGetComponent<Name>(entity); name != nullptr && !name->value.empty()) {
        displayName = name->value;
    } else {
        // Same "Entity <index>" fallback as HierarchyPanel::BuildEntityLabel().
        displayName = "Entity " + std::to_string(entity.index);
    }

    RecordEntityDraw(entity.index, entity.generation, displayName, pipeline.DebugName(), slots, triangleCount);
}

} // namespace gte
