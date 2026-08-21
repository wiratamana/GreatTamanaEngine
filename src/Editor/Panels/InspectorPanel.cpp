#include "InspectorPanel.h"

#include "../EditorContext.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/MeshRenderer.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"

#if GTE_ENABLE_PROJECT_PANEL
#include "../AssetInspectorData.h"
#include "../AssetPreviewTexture.h"
#include "../MemoryPanelData.h" // FormatBytes() - reused for the asset size field below.
#include "../ProjectPanelData.h" // Utf8ToPath()
#include "../../Assets/AssetTypes.h" // AssetType
#include "../../Assets/GtaFile.h" // ReadGtaHeader()
#include "../../Renderer/Renderer.h"
#endif

#include <imgui.h>

#include <cstdint>

namespace gte {

namespace {

void BuildEntityInspector(Registry& registry, EditorContext& ctx)
{
    const Entity entity = ctx.selection.SelectedEntity();

    if (!registry.IsAlive(entity)) {
        ImGui::TextDisabled("No entity selected.");
        return;
    }

    ImGui::Text("Entity %u (generation %u)", entity.index, entity.generation);
    ImGui::Separator();

    if (Transform* transform = registry.TryGetComponent<Transform>(entity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Position", &transform->position.x, 0.01f);

            Vec3 eulerDegrees = transform->rotation.ToEulerDegrees();
            if (ImGui::DragFloat3("Rotation", &eulerDegrees.x, 0.1f)) {
                transform->rotation = Quat::FromEulerDegrees(eulerDegrees.x, eulerDegrees.y, eulerDegrees.z);
            }

            ImGui::DragFloat3("Scale", &transform->scale.x, 0.01f);
        }
    }

    if (MeshRenderer* meshRenderer = registry.TryGetComponent<MeshRenderer>(entity)) {
        if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginDisabled();
            ImGui::Text("Mesh handle:     index %u, generation %u",
                meshRenderer->mesh.index, meshRenderer->mesh.generation);
            ImGui::Text("Pipeline handle: index %u, generation %u",
                meshRenderer->pipeline.index, meshRenderer->pipeline.generation);
            ImGui::EndDisabled();
        }
    }

    if (Camera* camera = registry.TryGetComponent<Camera>(entity)) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Active", &camera->active);
            ImGui::DragFloat("Field of View (Y)", &camera->fovYDegrees, 0.5f, 1.0f, 179.0f);
            ImGui::DragFloat("Near Z", &camera->nearZ, 0.01f, 0.001f, camera->farZ - 0.01f);
            ImGui::DragFloat("Far Z", &camera->farZ, 1.0f, camera->nearZ + 0.01f);
        }
    }
}

#if GTE_ENABLE_PROJECT_PANEL
void BuildAssetInspector(EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview)
{
    const std::string& absolutePath = ctx.selection.SelectedAssetAbsolutePath();
    const std::string& relativePath = ctx.selection.SelectedAssetRelativePath();

    const AssetMetadata metadata = BuildAssetMetadata(Utf8ToPath(absolutePath));

    ImGui::Text("%s", metadata.name.empty() ? "(Project root)" : metadata.name.c_str());
    ImGui::TextDisabled("%s", relativePath.empty() ? "(root)" : relativePath.c_str());
    ImGui::Separator();

    if (!metadata.exists) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "This item no longer exists on disk.");
        return;
    }

    // Attempt a live texture preview first - for a FILE whose extension is
    // either a format AssetPreviewTexture/stb_image can decode directly, OR
    // a *.gta whose own header (peeked here, cheaply - see
    // GtaFile.h's ReadGtaHeader()) confirms it's actually AssetType::Texture
    // (the result of AssetImporter::ImportAssetFile() gating a dropped
    // PNG/JPG through the KTX2 pipeline - see src/Assets/AssetImporter.h).
    // A *.gta wrapping something other than a texture (a future Mesh/
    // Scene/... asset) is deliberately NOT treated as "should have
    // previewed but failed" - it just falls through to plain metadata
    // below, exactly like any other non-image extension, with no spurious
    // "failed to load" message. Falls back to plain metadata below for a
    // folder, an unsupported extension, or a file that fails to decode
    // (corrupt/truncated/...) - exactly like every other failure mode in
    // this Editor degrades to a status/message rather than a crash.
    bool attemptPreview = !metadata.isDirectory && IsSupportedImageExtension(metadata.extension);
    if (!attemptPreview && !metadata.isDirectory && metadata.extension == ".gta") {
        const std::optional<GtaHeader> header = ReadGtaHeader(Utf8ToPath(absolutePath));
        attemptPreview = header.has_value() && header->Type() == AssetType::Texture;
    }

    if (attemptPreview) {
        if (const std::optional<AssetPreviewTexture::Preview> preview = assetPreview.Resolve(renderer, absolutePath)) {
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float aspect = preview->height > 0
                ? static_cast<float>(preview->width) / static_cast<float>(preview->height)
                : 1.0f;
            const float displayWidth = availableWidth;
            const float displayHeight = aspect > 0.0f ? displayWidth / aspect : displayWidth;

            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(preview->descriptor)),
                ImVec2(displayWidth, displayHeight));
            ImGui::Text("%d x %d pixels", preview->width, preview->height);
            ImGui::Separator();
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load image preview.");
            ImGui::Separator();
        }
    }

    ImGui::Text("Type: %s",
        metadata.isDirectory ? "Folder" : (metadata.extension.empty() ? "File" : metadata.extension.c_str()));
    if (!metadata.isDirectory) {
        ImGui::Text("Size: %s", FormatBytes(metadata.sizeBytes).c_str());
    }
    if (metadata.hasLastWriteTime) {
        ImGui::Text("Last modified: %s", metadata.lastWriteTimeText.c_str());
    }
    ImGui::TextWrapped("Path: %s", absolutePath.c_str());
}
#endif

} // namespace

#if GTE_ENABLE_PROJECT_PANEL
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview)
#else
void BuildInspectorPanel(Registry& registry, EditorContext& ctx)
#endif
{
    ImGui::Begin("Inspector");

#if GTE_ENABLE_PROJECT_PANEL
    if (ctx.selection.Kind() == InspectorSelectionKind::Asset && !ctx.selection.SelectedAssetAbsolutePath().empty()) {
        BuildAssetInspector(ctx, renderer, assetPreview);
        ImGui::End();
        return;
    }
#endif

    BuildEntityInspector(registry, ctx);

    ImGui::End();
}

} // namespace gte
