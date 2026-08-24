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
#include "../../Assets/AssetTypes.h" // AssetType, AssetFlags, Guid
#include "../../Assets/GtaFile.h" // ReadGtaHeader()
#include "../../Renderer/Renderer.h"
#endif

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

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

// Human-readable label for AssetType - the *.gta header's own record of
// what kind of payload it wraps (see AssetTypes.h). Purely a display
// helper; never round-tripped back into a numeric value anywhere.
const char* AssetTypeLabel(AssetType type)
{
    switch (type) {
    case AssetType::Unknown: return "Unknown";
    case AssetType::Texture: return "Texture";
    case AssetType::Mesh: return "Mesh";
    case AssetType::Material: return "Material";
    case AssetType::Shader: return "Shader";
    case AssetType::Audio: return "Audio";
    case AssetType::Scene: return "Scene";
    case AssetType::Text: return "Text";
    case AssetType::Font: return "Font";
    case AssetType::Animation: return "Animation";
    case AssetType::Prefab: return "Prefab";
    case AssetType::Other: return "Other";
    default: return "Unknown";
    }
}

// Comma-joined label for whichever AssetFlags bits are set on a *.gta
// header (see AssetTypes.h) - "None" if none are set, matching
// AssetTypeLabel()'s "always produce something displayable" convention.
std::string AssetFlagsLabel(AssetFlags flags)
{
    std::string result;
    if (HasFlag(flags, AssetFlags::Compressed)) {
        result += "Compressed";
    }
    if (HasFlag(flags, AssetFlags::Encrypted)) {
        if (!result.empty()) {
            result += ", ";
        }
        result += "Encrypted";
    }
    return result.empty() ? "None" : result;
}

// Shows the metadata actually recorded INSIDE the *.gta file itself
// (GtaHeader's fields, plus the metadata/payload byte ranges that follow
// it - see GtaFile.h) - as opposed to BuildPlainFileMetadata() below, which
// is only ever plain OS filesystem info (name/size/last-write-time) that
// knows nothing about the asset FORMAT. This is what actually answers "what
// is this asset" (its stable Guid, its declared AssetType, the exact byte
// size of the KTX2 payload libktx produced) rather than just "what does the
// OS say about this file", the same distinction Unity draws between an
// asset's own Import Settings and its raw file properties.
//
// `preview` is only used for its decoded pixel width/height (already paid
// for by the caller's AssetPreviewTexture::Resolve() call, so this never
// re-decodes anything) - passed as std::nullopt when the pixel preview
// itself failed, in which case every OTHER field here (still read straight
// from the 64-byte header, no decode required) is shown anyway.
void BuildGtaTextureMetadata(
    const GtaHeader& header, std::uintmax_t fileSizeBytes, const std::optional<AssetPreviewTexture::Preview>& preview)
{
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "GTA Asset Metadata");
    ImGui::Text("Asset Type: %s", AssetTypeLabel(header.Type()));
    ImGui::Text("Format Version: %llu", static_cast<unsigned long long>(header.version));
    ImGui::Text("GUID: %s", header.Id().ToString().c_str());
    ImGui::Text("Flags: %s", AssetFlagsLabel(header.Flags()).c_str());

    // payloadOffset is always >= sizeof(GtaHeader) for a well-formed file
    // (see GtaHeader's own doc comment) - ReadGtaHeader() already validated
    // the magic, but guard the subtraction anyway rather than trust that.
    const std::uint64_t metadataSize
        = header.payloadOffset >= sizeof(GtaHeader) ? header.payloadOffset - sizeof(GtaHeader) : 0;
    const std::uint64_t payloadSize = fileSizeBytes >= header.payloadOffset ? fileSizeBytes - header.payloadOffset : 0;

    ImGui::Separator();
    if (preview.has_value()) {
        ImGui::Text("Dimensions: %d x %d px", preview->width, preview->height);
    }
    // Every *.gta AssetType::Texture payload today is the exact same
    // container EncodeImageBytesToKtx2() (Ktx2Encoder.h) produces - a
    // single-mip, single-layer, single-face, uncompressed
    // VK_FORMAT_R8G8B8A8_UNORM KTX2 - so this label is a true fact read
    // from how the format is actually produced/decoded (Ktx2Decoder.cpp
    // rejects anything else), not a guess. Update this the moment real
    // block-compression/supercompression lands (see TODO.md).
    ImGui::Text("Texture Format: RGBA8, Uncompressed (KTX2)");
    ImGui::Text("Payload Size (KTX2): %s", FormatBytes(payloadSize).c_str());
    if (metadataSize > 0) {
        ImGui::Text("Metadata Size: %s", FormatBytes(metadataSize).c_str());
    }
    ImGui::Text("On-disk Size (.gta): %s", FormatBytes(fileSizeBytes).c_str());
}

// Plain OS filesystem metadata (AssetInspectorData.h) - name/extension say
// nothing about how a *.gta's own payload is structured, only what the
// filesystem itself reports.
void BuildPlainFileMetadata(const AssetMetadata& metadata, const std::string& absolutePath)
{
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

// The Unity-style "texture viewer" strip anchored to the BOTTOM of the
// Inspector's content area: a small title bar (the asset's name) followed
// by the actual image, contain-fit (scaled to fit entirely inside the
// available area, centered, aspect preserved - never cropped/stretched)
// against a dark backdrop, with a small dimensions overlay in the corner -
// the same layout Unity's own texture Inspector uses below its Import
// Settings list. Fills whatever height the caller's BeginChild() already
// reserved for it; see BuildAssetInspector()'s splitter for how that
// height is chosen/resized.
void BuildTextureViewer(const AssetMetadata& metadata, const AssetPreviewTexture::Preview& preview)
{
    ImGui::BeginChild(
        "InspectorPreviewViewer", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextUnformatted(metadata.name.c_str());
    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x > 1.0f && avail.y > 1.0f) {
        const float aspect
            = preview.height > 0 ? static_cast<float>(preview.width) / static_cast<float>(preview.height) : 1.0f;

        float displayWidth = avail.x;
        float displayHeight = aspect > 0.0f ? displayWidth / aspect : displayWidth;
        if (displayHeight > avail.y) {
            displayHeight = avail.y;
            displayWidth = displayHeight * aspect;
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        // Dark backdrop across the whole viewer area (same spirit as
        // Unity's own texture preview background), so a non-square/
        // non-viewer-shaped image is clearly letterboxed rather than
        // looking like it's floating on the panel's normal background.
        drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(35, 35, 35, 255));

        const float offsetX = (avail.x - displayWidth) * 0.5f;
        const float offsetY = (avail.y - displayHeight) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + offsetX, origin.y + offsetY));
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(preview.descriptor)),
            ImVec2(displayWidth, displayHeight));

        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%d x %d", preview.width, preview.height);
        drawList->AddText(ImVec2(origin.x + 6.0f, origin.y + avail.y - 20.0f), IM_COL32(255, 255, 255, 255), overlay);
    }

    ImGui::EndChild();
}

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

    // Peek this file's *.gta header (cheap - see GtaFile.h's
    // ReadGtaHeader()) whenever the extension is ".gta" at all, regardless
    // of whether it turns out to be a texture - this is what lets the
    // metadata section below show real GTA-format fields (GUID/AssetType/
    // flags/payload size) for ANY *.gta asset, not only ones that also
    // happen to preview as an image.
    const bool isGta = !metadata.isDirectory && metadata.extension == ".gta";
    const std::optional<GtaHeader> gtaHeader = isGta ? ReadGtaHeader(Utf8ToPath(absolutePath)) : std::nullopt;
    const bool isGtaTexture = gtaHeader.has_value() && gtaHeader->Type() == AssetType::Texture;

    // Attempt a live texture preview for a FILE whose extension is either a
    // format AssetPreviewTexture/stb_image can decode directly, OR a *.gta
    // confirmed above to actually wrap AssetType::Texture (the result of
    // AssetImporter::ImportAssetFile() gating a dropped PNG/JPG through the
    // KTX2 import pipeline - see src/Assets/AssetImporter.h). A *.gta
    // wrapping something other than a texture (a future Mesh/Scene/...
    // asset) is deliberately NOT treated as "should have previewed but
    // failed" - it just falls through to plain metadata below, exactly
    // like any other non-image extension, with no spurious "failed to
    // load" message.
    const bool attemptPreview = !metadata.isDirectory && (IsSupportedImageExtension(metadata.extension) || isGtaTexture);

    std::optional<AssetPreviewTexture::Preview> preview;
    if (attemptPreview) {
        preview = assetPreview.Resolve(renderer, absolutePath);
    }

    if (preview.has_value()) {
        // Unity-style split layout: a scrollable metadata list on top, a
        // user-draggable splitter, then the texture viewer pinned to the
        // BOTTOM of the panel. ctx.inspectorPreviewHeight (EditorContext.h)
        // is the persisted (across frames/selections) pixel height of the
        // bottom viewer, adjusted live below by dragging the splitter -
        // exactly like Unity's own Inspector preview pane.
        constexpr float kSplitterThickness = 6.0f;
        constexpr float kMinPreviewHeight = 100.0f;
        constexpr float kMinMetadataHeight = 80.0f;

        const float totalAvail = ImGui::GetContentRegionAvail().y;
        const float maxPreviewHeight = std::max(kMinPreviewHeight, totalAvail - kSplitterThickness - kMinMetadataHeight);
        ctx.inspectorPreviewHeight = std::clamp(ctx.inspectorPreviewHeight, kMinPreviewHeight, maxPreviewHeight);
        const float metadataHeight = std::max(0.0f, totalAvail - ctx.inspectorPreviewHeight - kSplitterThickness);

        ImGui::BeginChild("InspectorMetadataRegion", ImVec2(0, metadataHeight), false);
        if (isGtaTexture && gtaHeader.has_value()) {
            BuildGtaTextureMetadata(*gtaHeader, metadata.sizeBytes, preview);
            ImGui::Separator();
        }
        BuildPlainFileMetadata(metadata, absolutePath);
        ImGui::EndChild();

        // The draggable splitter itself - a thin full-width button styled
        // like a scrollbar grip. Dragging it up/down adjusts
        // inspectorPreviewHeight (subtracting the mouse's vertical delta,
        // since the viewer is anchored to the BOTTOM: dragging the
        // splitter UP must grow the viewer, i.e. increase its height).
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrab));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabActive));
        ImGui::Button("##InspectorPreviewSplitter", ImVec2(-1.0f, kSplitterThickness));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemActive()) {
            ctx.inspectorPreviewHeight -= ImGui::GetIO().MouseDelta.y;
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }

        BuildTextureViewer(metadata, *preview);
        return;
    }

    if (attemptPreview) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load image preview.");
        ImGui::Separator();
    }
    if (isGtaTexture && gtaHeader.has_value()) {
        // The pixel preview failed (corrupt/truncated KTX2 payload, etc.)
        // but the header itself is still valid - show what the header
        // alone can tell us rather than nothing at all.
        BuildGtaTextureMetadata(*gtaHeader, metadata.sizeBytes, std::nullopt);
        ImGui::Separator();
    }
    BuildPlainFileMetadata(metadata, absolutePath);
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
