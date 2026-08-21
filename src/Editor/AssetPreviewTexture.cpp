#include "AssetPreviewTexture.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/Texture2D.h"

// The ONE place in the entire engine that compiles stb_image's
// implementation - see this file's own doc comment in AssetPreviewTexture.h
// and cmake/FetchSTB.cmake. Every other translation unit that needs image
// decoding must go through THIS class rather than including stb_image.h
// with STB_IMAGE_IMPLEMENTATION itself (that would be an ODR violation -
// multiple definitions of every stbi_* function across translation units).
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <backends/imgui_impl_vulkan.h>

#include <exception>
#include <system_error>

namespace gte {

AssetPreviewTexture::~AssetPreviewTexture()
{
    Reset();
}

void AssetPreviewTexture::Reset()
{
    // A previous frame's command buffer might still be executing on the
    // GPU and could still reference the descriptor/texture about to be
    // destroyed below - stall until it's definitely done, exactly the same
    // reasoning (and same blunt fix) ImGuiEditorLayer::GameViewTarget()/
    // SceneViewTarget() already use before resizing/releasing their own
    // descriptors on a user-driven (rather than per-frame) event. Switching
    // the Inspector's selected asset is just as rare/user-driven, so an
    // occasional full stall here is a fine trade for correctness. Skipped
    // entirely when nothing is actually cached yet (e.g. the very first
    // call), so a fresh AssetPreviewTexture never pays this cost for
    // nothing.
    if ((m_texture || m_descriptor != VK_NULL_HANDLE) && m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    if (m_descriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_descriptor);
        m_descriptor = VK_NULL_HANDLE;
    }
    m_texture.reset();
    m_cachedPath.clear();
    m_cachedWriteTime = std::filesystem::file_time_type{};
    m_cachedIsValid = false;
    m_width = 0;
    m_height = 0;
}

std::optional<AssetPreviewTexture::Preview> AssetPreviewTexture::Resolve(
    Renderer& renderer, const std::string& absolutePath)
{
    m_device = renderer.GetVulkanContextInfo().device;

    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime =
        std::filesystem::last_write_time(std::filesystem::path(absolutePath), timeEc);

    if (!absolutePath.empty() && absolutePath == m_cachedPath && !timeEc && writeTime == m_cachedWriteTime) {
        // Same file, unchanged since last time - reuse whatever was
        // resolved last call (a valid preview, OR a remembered "this isn't
        // a decodable image"), without touching the filesystem/GPU again.
        if (!m_cachedIsValid) {
            return std::nullopt;
        }
        return Preview{ m_descriptor, m_width, m_height };
    }

    Reset();
    m_cachedPath = absolutePath;
    if (!timeEc) {
        m_cachedWriteTime = writeTime;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    unsigned char* pixels = stbi_load(absolutePath.c_str(), &width, &height, &sourceChannels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        // m_cachedIsValid stays false (set by Reset() above) - remembered
        // as "not a decodable image" so re-selecting the same broken/
        // unsupported file doesn't re-attempt a decode every frame.
        return std::nullopt;
    }

    try {
        m_texture = std::make_unique<Texture2D>(renderer.CreateTexture2D(pixels, width, height, "AssetPreview"));
    } catch (const std::exception&) {
        stbi_image_free(pixels);
        m_texture.reset();
        return std::nullopt;
    }
    stbi_image_free(pixels);

    m_descriptor =
        ImGui_ImplVulkan_AddTexture(m_texture->Sampler(), m_texture->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_width = width;
    m_height = height;
    m_cachedIsValid = true;

    return Preview{ m_descriptor, m_width, m_height };
}

} // namespace gte
