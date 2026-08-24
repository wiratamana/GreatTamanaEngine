#include "InspectorPanel.h"

#include "../EditorContext.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/MeshRenderer.h"
#include "../../ECS/Components/Name.h"
#include "../../ECS/Components/SkeletalAnimator.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"

#if GTE_ENABLE_PROJECT_PANEL
#include "../AssetInspectorData.h"
#include "../AssetPreviewMesh.h"
#include "../AssetPreviewTexture.h"
#include "../MemoryPanelData.h" // FormatBytes() - reused for the asset size field below.
#include "../ProjectPanelData.h" // Utf8ToPath()
#include "../../Assets/AssetTypes.h" // AssetType, AssetFlags, Guid
#include "../../Assets/GtaFile.h" // ReadGtaHeader()/ReadGtaFile()
#include "../../Assets/MotionFile.h" // DecodeMotionDataFromBytes()
#include "../../Renderer/Renderer.h"
#endif

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

    // Optional editable display Name (ECS/Components/Name.h) - an entity
    // with none yet gets one lazily the moment its name is actually edited
    // here, rather than every entity paying for one up front.
    {
        Name* name = registry.TryGetComponent<Name>(entity);
        char nameBuffer[256];
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", name != nullptr ? name->value.c_str() : "");
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
            if (name != nullptr) {
                name->value = nameBuffer;
            } else {
                registry.AddComponent<Name>(entity, Name{ std::string(nameBuffer) });
            }
        }
    }

    ImGui::Separator();

    if (Transform* transform = registry.TryGetComponent<Transform>(entity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Position", &transform->position.x, 0.01f);

            Vec3 eulerDegrees = transform->rotation.ToEulerDegrees();
            if (ImGui::DragFloat3("Rotation", &eulerDegrees.x, 0.1f)) {
                transform->rotation = Quat::FromEulerDegrees(eulerDegrees.x, eulerDegrees.y, eulerDegrees.z);
            }

            ImGui::DragFloat3("Scale", &transform->scale.x, 0.01f);

            // Read-only "Parent" info + a one-click "Unparent" (Unity's own
            // Transform.SetParent(null)) - the Inspector-side complement to
            // "Hierarchy"'s own drag-and-drop attach/detach (see
            // Panels/HierarchyPanel.h). Reparenting itself (choosing a NEW
            // parent) is still drag-and-drop-only in "Hierarchy" - there's
            // no entity picker widget in this engine yet to pick one from
            // here.
            if (transform->parent.IsValid() && registry.IsAlive(transform->parent)) {
                ImGui::Text("Parent: Entity %u", transform->parent.index);
                ImGui::SameLine();
                if (ImGui::SmallButton("Unparent")) {
                    SetParent(registry, entity, kInvalidEntity);
                }
            } else {
                ImGui::TextDisabled("Parent: (none)");
            }
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

    // Shown only for a model root entity currently playing back a motion
    // (see Game::PlayAnimationOnEntity(), src/Game/Game.cpp) - a plain
    // playback-state readout/control panel, Unity's own Animator component
    // inspector in spirit (though far simpler - no state machine here, just
    // a single clip path + play/loop/speed/frame).
    if (SkeletalAnimator* animator = registry.TryGetComponent<SkeletalAnimator>(entity)) {
        if (ImGui::CollapsingHeader("Skeletal Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("Clip: %s",
                animator->animationGtaPath.empty() ? "(none)" : animator->animationGtaPath.c_str());
            ImGui::Checkbox("Playing", &animator->playing);
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &animator->loop);
            ImGui::DragFloat("Speed", &animator->speed, 0.01f, 0.0f, 5.0f);
            ImGui::Text("Frame: %.1f", animator->frame);
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

// The Mesh-asset equivalent of BuildGtaTextureMetadata() above - shown when
// the selected *.gta wraps AssetType::Mesh (the result of importing a .pmx
// model - see src/Assets/AssetImporter.h). `preview` (AssetPreviewMesh's own
// result) supplies the vertex/triangle counts; std::nullopt when the live
// 3D preview itself failed to render (a corrupt/truncated payload despite a
// valid header), in which case every OTHER field here is still shown.
void BuildGtaMeshMetadata(
    const GtaHeader& header, std::uintmax_t fileSizeBytes, const std::optional<AssetPreviewMesh::Preview>& preview)
{
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "GTA Asset Metadata");
    ImGui::Text("Asset Type: %s", AssetTypeLabel(header.Type()));
    ImGui::Text("Format Version: %llu", static_cast<unsigned long long>(header.version));
    ImGui::Text("GUID: %s", header.Id().ToString().c_str());
    ImGui::Text("Flags: %s", AssetFlagsLabel(header.Flags()).c_str());

    const std::uint64_t metadataSize
        = header.payloadOffset >= sizeof(GtaHeader) ? header.payloadOffset - sizeof(GtaHeader) : 0;
    const std::uint64_t payloadSize = fileSizeBytes >= header.payloadOffset ? fileSizeBytes - header.payloadOffset : 0;

    ImGui::Separator();
    if (preview.has_value()) {
        ImGui::Text("Vertices: %llu", static_cast<unsigned long long>(preview->vertexCount));
        ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(preview->triangleCount));
    }
    // Every *.gta AssetType::Mesh payload today is the exact same layout
    // MeshFile.h's EncodeMeshDataToBytes() produces (see that file) - plain
    // positions/normals/UVs/indices, no materials/bones/morphs yet.
    ImGui::Text("Mesh Format: Positions + Normals + UVs + Indices (GTEMESH1)");
    ImGui::Text("Payload Size: %s", FormatBytes(payloadSize).c_str());
    if (metadataSize > 0) {
        ImGui::Text("Metadata Size: %s", FormatBytes(metadataSize).c_str());
    }
    ImGui::Text("On-disk Size (.gta): %s", FormatBytes(fileSizeBytes).c_str());
}

// The Animation-asset equivalent of BuildGtaTextureMetadata()/
// BuildGtaMeshMetadata() above - shown when the selected *.gta wraps
// AssetType::Animation (the result of importing a .vmd motion file - see
// src/Assets/VmdLoader.h/AssetImporter.h). Unlike the Texture/Mesh cases,
// there is no live GPU preview for a motion (a flat keyframe list has
// nothing to rasterize/render), so this is always the FULL story for an
// Animation asset - no separate "viewer" pane ever exists alongside it (see
// BuildAssetInspector() below, which never puts an Animation selection
// through the preview/splitter layout at all). `motion` is decoded straight
// from the *.gta's own PAYLOAD bytes (MotionFile.h's
// DecodeMotionDataFromBytes()) by the caller - std::nullopt when that
// payload is corrupt/truncated despite a valid *.gta header, in which case
// every OTHER field here (still read straight from the 64-byte header, no
// decode required) is shown anyway.
void BuildGtaAnimationMetadata(const GtaHeader& header, std::uintmax_t fileSizeBytes, const std::optional<MotionData>& motion)
{
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "GTA Asset Metadata");
    ImGui::Text("Asset Type: %s", AssetTypeLabel(header.Type()));
    ImGui::Text("Format Version: %llu", static_cast<unsigned long long>(header.version));
    ImGui::Text("GUID: %s", header.Id().ToString().c_str());
    ImGui::Text("Flags: %s", AssetFlagsLabel(header.Flags()).c_str());

    const std::uint64_t metadataSize
        = header.payloadOffset >= sizeof(GtaHeader) ? header.payloadOffset - sizeof(GtaHeader) : 0;
    const std::uint64_t payloadSize = fileSizeBytes >= header.payloadOffset ? fileSizeBytes - header.payloadOffset : 0;

    ImGui::Separator();
    if (motion.has_value()) {
        if (!motion->modelName.empty()) {
            ImGui::Text("Target Model Name: %s", motion->modelName.c_str());
        }

        // Frame range across every track combined (bone/morph/camera/
        // light/shadow/IK - whichever ones this particular .vmd actually
        // populated) - a quick "how long is this motion" hint without
        // needing a full playback/scrubbing UI (see TODO.md: no
        // interpolation evaluation/keyframe playback exists anywhere in
        // this engine yet).
        bool hasAnyFrame = false;
        std::uint32_t minFrame = 0;
        std::uint32_t maxFrame = 0;
        auto scanFrames = [&](const auto& list) {
            for (const auto& kf : list) {
                if (!hasAnyFrame) {
                    minFrame = kf.frame;
                    maxFrame = kf.frame;
                    hasAnyFrame = true;
                } else {
                    minFrame = std::min(minFrame, kf.frame);
                    maxFrame = std::max(maxFrame, kf.frame);
                }
            }
        };
        scanFrames(motion->boneKeyframes);
        scanFrames(motion->morphKeyframes);
        scanFrames(motion->cameraKeyframes);
        scanFrames(motion->lightKeyframes);
        scanFrames(motion->shadowKeyframes);
        scanFrames(motion->ikKeyframes);
        if (hasAnyFrame) {
            ImGui::Text("Frame Range: %u - %u (VMD's fixed 30fps grid)", minFrame, maxFrame);
        }

        // Distinct bone names this motion actually drives - matched by
        // NAME against a target model's own SkeletonData::bones at
        // playback time (see MotionData.h's own doc comment); nothing here
        // resolves that yet, but the plain name list is still useful to
        // eyeball which rig a motion expects.
        std::vector<std::string> uniqueBoneNames;
        uniqueBoneNames.reserve(motion->boneKeyframes.size());
        for (const auto& kf : motion->boneKeyframes) {
            uniqueBoneNames.push_back(kf.boneName);
        }
        std::sort(uniqueBoneNames.begin(), uniqueBoneNames.end());
        uniqueBoneNames.erase(std::unique(uniqueBoneNames.begin(), uniqueBoneNames.end()), uniqueBoneNames.end());

        ImGui::Text("Bone Keyframes: %llu (%llu unique bones)",
            static_cast<unsigned long long>(motion->boneKeyframes.size()),
            static_cast<unsigned long long>(uniqueBoneNames.size()));
        ImGui::Text("Morph Keyframes: %llu", static_cast<unsigned long long>(motion->morphKeyframes.size()));
        ImGui::Text("Camera Keyframes: %llu", static_cast<unsigned long long>(motion->cameraKeyframes.size()));
        ImGui::Text("Light Keyframes: %llu", static_cast<unsigned long long>(motion->lightKeyframes.size()));
        ImGui::Text("Shadow Keyframes: %llu", static_cast<unsigned long long>(motion->shadowKeyframes.size()));
        ImGui::Text("IK Keyframes: %llu", static_cast<unsigned long long>(motion->ikKeyframes.size()));

        // A small, scrollable, collapsible detail section listing every
        // distinct bone name this motion drives - same "CollapsingHeader/
        // TreeNode for optional detail" shape as BuildEntityInspector()'s
        // own component sections above. Collapsed by default so a motion
        // with hundreds of bones doesn't dominate the metadata list.
        if (!uniqueBoneNames.empty() && ImGui::TreeNode("Bone Names")) {
            ImGui::BeginChild(
                "InspectorAnimationBoneNames", ImVec2(0, 150.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& name : uniqueBoneNames) {
                ImGui::TextUnformatted(name.c_str());
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }

        // Same idea for morph names, when present (a facial/expression
        // motion rather than a body motion).
        std::vector<std::string> uniqueMorphNames;
        uniqueMorphNames.reserve(motion->morphKeyframes.size());
        for (const auto& kf : motion->morphKeyframes) {
            uniqueMorphNames.push_back(kf.morphName);
        }
        std::sort(uniqueMorphNames.begin(), uniqueMorphNames.end());
        uniqueMorphNames.erase(std::unique(uniqueMorphNames.begin(), uniqueMorphNames.end()), uniqueMorphNames.end());
        if (!uniqueMorphNames.empty() && ImGui::TreeNode("Morph Names")) {
            ImGui::BeginChild(
                "InspectorAnimationMorphNames", ImVec2(0, 100.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& name : uniqueMorphNames) {
                ImGui::TextUnformatted(name.c_str());
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }

        ImGui::Separator();
    }
    // Every *.gta AssetType::Animation payload today is the exact same
    // layout MotionFile.h's EncodeMotionDataToBytes() produces (see that
    // file) - model name + bone/morph/camera/light/shadow/IK keyframe
    // tracks, no metadata section used (unlike AssetType::Mesh).
    ImGui::Text("Motion Format: Bone/Morph/Camera/Light/Shadow/IK Keyframes (GTEMOTN1)");
    ImGui::Text("Payload Size: %s", FormatBytes(payloadSize).c_str());
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

// The Mesh-asset equivalent of BuildTextureViewer() above - same contain-fit
// layout against the same dark backdrop, but the "image" is a LIVE,
// continuously-rerendered, auto-rotating 3D view of the mesh (see
// AssetPreviewMesh.h) rather than a static decoded texture - so this is
// called fresh every frame the viewer is visible, unlike the texture
// preview (which just redisplays whatever AssetPreviewTexture cached).
// Shows a vertex/triangle-count overlay in the corner instead of pixel
// dimensions, matching this asset type's own metadata (see
// BuildGtaMeshMetadata() above).
void BuildMeshViewer(const AssetMetadata& metadata, const AssetPreviewMesh::Preview& preview)
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
        drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(35, 35, 35, 255));

        const float offsetX = (avail.x - displayWidth) * 0.5f;
        const float offsetY = (avail.y - displayHeight) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + offsetX, origin.y + offsetY));
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(preview.descriptor)),
            ImVec2(displayWidth, displayHeight));

        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%llu verts, %llu tris", static_cast<unsigned long long>(preview.vertexCount),
            static_cast<unsigned long long>(preview.triangleCount));
        drawList->AddText(ImVec2(origin.x + 6.0f, origin.y + avail.y - 20.0f), IM_COL32(255, 255, 255, 255), overlay);
    }

    ImGui::EndChild();
}

void BuildAssetInspector(
    EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview, AssetPreviewMesh& assetPreviewMesh)
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
    // of whether it turns out to be a texture/mesh - this is what lets the
    // metadata section below show real GTA-format fields (GUID/AssetType/
    // flags/payload size) for ANY *.gta asset, not only ones that also
    // happen to preview as an image or a mesh.
    const bool isGta = !metadata.isDirectory && metadata.extension == ".gta";
    const std::optional<GtaHeader> gtaHeader = isGta ? ReadGtaHeader(Utf8ToPath(absolutePath)) : std::nullopt;
    const bool isGtaTexture = gtaHeader.has_value() && gtaHeader->Type() == AssetType::Texture;
    const bool isGtaMesh = gtaHeader.has_value() && gtaHeader->Type() == AssetType::Mesh;
    const bool isGtaAnimation = gtaHeader.has_value() && gtaHeader->Type() == AssetType::Animation;

    // Decode the motion data straight out of the *.gta's own PAYLOAD bytes
    // whenever the header confirms AssetType::Animation - there is no GPU
    // preview to gate this behind (unlike the texture/mesh cases below),
    // this is just a plain binary decode (MotionFile.h's
    // DecodeMotionDataFromBytes()), so it always runs up front. std::nullopt
    // if the full *.gta can't be read at all or its payload is corrupt/
    // truncated despite a valid header - BuildGtaAnimationMetadata() still
    // shows every OTHER (header-derived) field in that case.
    std::optional<MotionData> motionData;
    if (isGtaAnimation) {
        if (const std::optional<GtaFileData> gtaFile = ReadGtaFile(Utf8ToPath(absolutePath)); gtaFile.has_value()) {
            motionData = DecodeMotionDataFromBytes(gtaFile->payload);
        }
    }

    // Attempt a live texture preview for a FILE whose extension is either a
    // format AssetPreviewTexture/stb_image can decode directly, OR a *.gta
    // confirmed above to actually wrap AssetType::Texture (the result of
    // AssetImporter::ImportAssetFile() gating a dropped PNG/JPG through the
    // KTX2 import pipeline - see src/Assets/AssetImporter.h). A *.gta
    // wrapping something other than a texture (Mesh/Scene/...) is
    // deliberately NOT treated as "should have previewed but failed" - it
    // just falls through to plain metadata below, exactly like any other
    // non-image extension, with no spurious "failed to load" message.
    const bool attemptTexturePreview = !metadata.isDirectory && (IsSupportedImageExtension(metadata.extension) || isGtaTexture);

    std::optional<AssetPreviewTexture::Preview> preview;
    if (attemptTexturePreview) {
        preview = assetPreview.Resolve(renderer, absolutePath);
    }

    // Attempt a live 3D mesh preview whenever the *.gta header confirms
    // AssetType::Mesh - rendered at (roughly) the Inspector's current
    // remaining content size, well before the exact bottom-viewer height is
    // known (see AssetPreviewMesh.h's own comment: unlike a static texture,
    // this is a genuine render, but the result is still just displayed
    // contain-fit/scaled afterwards by BuildMeshViewer() below, exactly
    // like BuildTextureViewer() already does regardless of a texture's own
    // native resolution - so an approximate render target size here is
    // perfectly fine, no second render needed once the final split layout
    // is known).
    std::optional<AssetPreviewMesh::Preview> meshPreview;
    if (isGtaMesh && !metadata.isDirectory) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        meshPreview = assetPreviewMesh.Render(
            renderer, absolutePath, static_cast<int>(avail.x), static_cast<int>(avail.y));
    }

    if (preview.has_value() || meshPreview.has_value()) {
        // Unity-style split layout: a scrollable metadata list on top, a
        // user-draggable splitter, then the texture/mesh viewer pinned to
        // the BOTTOM of the panel. ctx.inspectorPreviewHeight
        // (EditorContext.h) is the persisted (across frames/selections)
        // pixel height of the bottom viewer, adjusted live below by
        // dragging the splitter - exactly like Unity's own Inspector
        // preview pane. Shared between the texture and mesh preview cases -
        // only one of the two is ever non-null for a given selection.
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
        } else if (isGtaMesh && gtaHeader.has_value()) {
            BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes, meshPreview);
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

        if (preview.has_value()) {
            BuildTextureViewer(metadata, *preview);
        } else {
            BuildMeshViewer(metadata, *meshPreview);
        }
        return;
    }

    if (attemptTexturePreview) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load image preview.");
        ImGui::Separator();
    } else if (isGtaMesh) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load mesh preview.");
        ImGui::Separator();
    } else if (isGtaAnimation && !motionData.has_value()) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to decode motion data.");
        ImGui::Separator();
    }
    if (isGtaTexture && gtaHeader.has_value()) {
        // The pixel preview failed (corrupt/truncated KTX2 payload, etc.)
        // but the header itself is still valid - show what the header
        // alone can tell us rather than nothing at all.
        BuildGtaTextureMetadata(*gtaHeader, metadata.sizeBytes, std::nullopt);
        ImGui::Separator();
    } else if (isGtaMesh && gtaHeader.has_value()) {
        BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes, std::nullopt);
        ImGui::Separator();
    } else if (isGtaAnimation && gtaHeader.has_value()) {
        BuildGtaAnimationMetadata(*gtaHeader, metadata.sizeBytes, motionData);
        ImGui::Separator();
    }
    BuildPlainFileMetadata(metadata, absolutePath);
}
#endif

} // namespace

#if GTE_ENABLE_PROJECT_PANEL
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview,
    AssetPreviewMesh& assetPreviewMesh)
#else
void BuildInspectorPanel(Registry& registry, EditorContext& ctx)
#endif
{
    ImGui::Begin("Inspector");

#if GTE_ENABLE_PROJECT_PANEL
    if (ctx.selection.Kind() == InspectorSelectionKind::Asset && !ctx.selection.SelectedAssetAbsolutePath().empty()) {
        BuildAssetInspector(ctx, renderer, assetPreview, assetPreviewMesh);
        ImGui::End();
        return;
    }
#endif

    BuildEntityInspector(registry, ctx);

    ImGui::End();
}

} // namespace gte
