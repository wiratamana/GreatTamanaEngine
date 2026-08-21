#pragma once

#include <volk.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gte {

class Renderer;
class Texture2D;

// Decodes an on-disk image file (PNG/JPEG/BMP/TGA/GIF/PSD/HDR/PNM - see
// AssetInspectorData.h's IsSupportedImageExtension()) via stb_image and
// uploads it to a GPU Texture2D (Renderer.h) wrapped in an ImGui descriptor
// set, for the Editor's "Inspector" panel to display via ImGui::Image()
// when a Project-panel image asset is selected (see
// Panels/InspectorPanel.cpp).
//
// src/Assets/StbImageImpl.cpp (not this file) is the ONE place in the
// entire engine that includes stb_image with STB_IMAGE_IMPLEMENTATION
// defined - see its own comment for why that moved out of this Editor-only
// file into an always-compiled module. Matches this project's
// VMA_IMPLEMENTATION/single-definition convention for single-header
// third-party libraries (see cmake/FetchVMA.cmake).
//
// Caches only the SINGLE most-recently-resolved texture (never a full
// path->texture map) - Inspector only ever shows one selection at a time,
// so this mirrors ImGuiEditorLayer's own single-slot m_gameView/m_sceneView
// pattern rather than an unbounded cache. Re-decodes/re-uploads only when
// the requested path OR that file's own last-write-time has changed since
// the previous Resolve() call - switching the Inspector selection back and
// forth to the SAME still-selected asset costs nothing extra.
//
// Owns its GPU Texture2D + ImGui descriptor set for as long as they're the
// currently-cached entry - both are released (Editor-only ImGui backend
// call + this object's own destructor) the moment a different path is
// resolved, or this object itself is destroyed. Deliberately does NOT store
// a Renderer& (see Resolve()'s own parameter) - same "don't hold a
// reference to Renderer itself, only the plain Vulkan handles it hands
// out" convention every other Editor type already follows (see AGENTS.md,
// "Editor Module Structure").
class AssetPreviewTexture {
public:
    AssetPreviewTexture() = default;
    ~AssetPreviewTexture();

    AssetPreviewTexture(const AssetPreviewTexture&) = delete;
    AssetPreviewTexture& operator=(const AssetPreviewTexture&) = delete;

    struct Preview {
        VkDescriptorSet descriptor = VK_NULL_HANDLE; // Ready to pass straight to ImGui::Image().
        int width = 0;
        int height = 0;
    };

    // Returns this asset's decoded/uploaded preview, or std::nullopt if
    // `absolutePath` doesn't currently decode as a supported image at all
    // (missing file, unsupported/corrupt content, ...) - the caller
    // (InspectorPanel) is expected to fall back to plain AssetMetadata in
    // that case, exactly like every other failure mode in this Editor
    // degrades to a status message rather than a crash.
    std::optional<Preview> Resolve(Renderer& renderer, const std::string& absolutePath);

    // Releases the currently-cached GPU texture/ImGui descriptor (if any),
    // waiting for the GPU to be idle first - see Reset()'s own comment in
    // AssetPreviewTexture.cpp for why. Called by the destructor, and MUST
    // also be called explicitly by ImGuiEditorLayer's destructor BEFORE
    // ImGui_ImplVulkan_Shutdown() (member destruction order alone would run
    // too late - see ImGuiEditorLayer.cpp).
    void Reset();

private:
    // Captured from whichever Renderer was passed to the most recent
    // Resolve() call - used only to vkDeviceWaitIdle() before releasing a
    // stale texture/descriptor in Reset(). Deliberately a plain VkDevice,
    // not a Renderer&, so this object never depends on Renderer outliving
    // it (same reasoning as ImGuiEditorLayer's own m_device member).
    VkDevice m_device = VK_NULL_HANDLE;

    std::string m_cachedPath;
    std::filesystem::file_time_type m_cachedWriteTime{};
    bool m_cachedIsValid = false; // True if m_cachedPath decoded successfully last time.
    std::unique_ptr<Texture2D> m_texture;
    VkDescriptorSet m_descriptor = VK_NULL_HANDLE;
    int m_width = 0;
    int m_height = 0;
};

} // namespace gte
