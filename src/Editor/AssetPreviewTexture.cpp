#include "AssetPreviewTexture.h"

#include "ProjectPanelData.h" // Utf8ToPath()/PathToUtf8() - see ReadFileBytes() below.
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()
#include "../Assets/Ktx2Decoder.h" // DecodeKtx2ToRgba8()
#include "../Renderer/Renderer.h"
#include "../Renderer/Texture2D.h"

// stb_image's actual implementation (STB_IMAGE_IMPLEMENTATION) lives in
// src/Assets/StbImageImpl.cpp instead - an always-compiled module (no
// GTE_ENABLE_EDITOR dependency), since the PNG/JPEG -> KTX2 import pipeline
// (src/Assets/Ktx2Encoder.h) needs real image decoding in every build
// configuration, not just an Editor build. This file only needs stb_image's
// DECLARATIONS (no implementation macro) - defining STB_IMAGE_IMPLEMENTATION
// here too would be an ODR violation (multiple definitions of every stbi_*
// function across translation units).
#include <stb_image.h>

#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <system_error>
#include <vector>

namespace gte {

namespace {

// Reads `absolutePath` (UTF-8) fully into memory, or returns std::nullopt
// on any failure (missing file, permissions, ...) - never throws.
//
// Deliberately NOT stbi_load(path.c_str(), ...): stb_image's own file-path
// entry point opens the file via the C runtime's narrow-char fopen(),
// which on Windows interprets that byte string using the process's
// current ANSI CODE PAGE, not UTF-8 - so any non-ASCII byte sequence (e.g.
// a Japanese filename, UTF-8-encoded by PathToUtf8() - see
// ProjectPanelData.h) is misinterpreted and fopen() silently fails to
// resolve the file, even though the file genuinely exists (this is exactly
// why AssetMetadata's size/last-write-time - built via std::filesystem,
// which DOES go through the wide-char Win32 API under MSVC's STL -
// resolve correctly while stbi_load() itself reported "failed to load").
// Constructing an std::ifstream from a std::filesystem::path (rather than
// a raw std::string/const char*) sidesteps this entirely - MSVC's STL
// overload for that specific constructor already opens the file via the
// wide-char API - then handing stb_image the raw bytes via
// stbi_load_from_memory() (see Resolve() below) needs no path/fopen
// involvement on stb_image's side at all.
std::optional<std::vector<unsigned char>> ReadFileBytes(const std::string& absolutePathUtf8)
{
    std::ifstream file(Utf8ToPath(absolutePathUtf8), std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }

    const std::streamoff size = file.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        return std::nullopt;
    }
    return bytes;
}

// True if `absolutePathUtf8` names a *.gta file (case-insensitive
// extension match) - the same convention IsImportableAsKtx2Texture()/
// AssetInspectorData's own extension checks use elsewhere.
bool HasGtaExtension(const std::string& absolutePathUtf8)
{
    std::string extension = PathToUtf8(Utf8ToPath(absolutePathUtf8).extension());
    std::transform(
        extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".gta";
}

// A plain, ownership-agnostic view over decoded RGBA8 pixels - either
// stb_image's own heap allocation (freed via stbi_image_free()) or a
// std::vector owned by the *.gta/KTX2 path below - so DecodePixels() (and
// Resolve()'s single CreateTexture2D() call site) don't need two near-
// identical code paths just to get a `const void*` + width/height to
// upload.
struct DecodedPixels {
    const void* data = nullptr;
    int width = 0;
    int height = 0;
};

// Decodes `absolutePath`'s pixels, from whichever of the two supported
// sources it actually is (see AssetPreviewTexture.h's class comment):
//   - A *.gta file (AssetType::Texture only) - read via ReadGtaFile() and
//     decoded via Ktx2Decoder.h's DecodeKtx2ToRgba8(). A *.gta that isn't a
//     Texture asset, or otherwise fails to decode, returns std::nullopt.
//   - Anything else - read via ReadFileBytes() and decoded via stb_image's
//     stbi_load_from_memory(), exactly as before.
// `stbOwnedPixels` receives stb_image's own allocation (so the caller can
// stbi_image_free() it after uploading) when the stb_image path was taken;
// left untouched (nullptr) for the *.gta/KTX2 path, whose pixels instead
// live in `gtaOwnedPixels` (owned by the caller for the same reason).
std::optional<DecodedPixels> DecodePixels(
    const std::string& absolutePath, unsigned char*& stbOwnedPixels, std::vector<std::uint8_t>& gtaOwnedPixels)
{
    if (HasGtaExtension(absolutePath)) {
        const std::optional<GtaFileData> gta = ReadGtaFile(Utf8ToPath(absolutePath));
        if (!gta.has_value() || gta->header.Type() != AssetType::Texture) {
            // Either not a valid *.gta at all, or a *.gta wrapping
            // something other than a texture (a future Mesh/Scene/...
            // asset) - InspectorPanel only calls Resolve() for a *.gta it
            // already confirmed is AssetType::Texture (see
            // Panels/InspectorPanel.cpp), so this is a defensive
            // double-check, not the expected common case.
            return std::nullopt;
        }

        const std::optional<Ktx2DecodeResult> decoded = DecodeKtx2ToRgba8(gta->payload);
        if (!decoded.has_value()) {
            return std::nullopt;
        }

        gtaOwnedPixels = std::move(decoded->rgba8Pixels);
        return DecodedPixels{ gtaOwnedPixels.data(), static_cast<int>(decoded->width), static_cast<int>(decoded->height) };
    }

    // Read the file ourselves (Unicode-path-safe - see ReadFileBytes()'s
    // own comment) and decode from the in-memory buffer via
    // stbi_load_from_memory(), rather than handing stb_image the path
    // directly - stb_image's own path-based stbi_load() cannot open a
    // non-ASCII (e.g. Japanese) path correctly on Windows.
    const std::optional<std::vector<unsigned char>> fileBytes = ReadFileBytes(absolutePath);
    if (!fileBytes.has_value()) {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    unsigned char* pixels = stbi_load_from_memory(fileBytes->data(), static_cast<int>(fileBytes->size()), &width,
        &height, &sourceChannels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return std::nullopt;
    }

    stbOwnedPixels = pixels;
    return DecodedPixels{ pixels, width, height };
}

} // namespace

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

    unsigned char* stbOwnedPixels = nullptr;
    std::vector<std::uint8_t> gtaOwnedPixels;
    const std::optional<DecodedPixels> decoded = DecodePixels(absolutePath, stbOwnedPixels, gtaOwnedPixels);
    if (!decoded.has_value()) {
        // m_cachedIsValid stays false (set by Reset() above) - remembered
        // as "not a decodable image" so re-selecting the same broken/
        // unsupported file doesn't re-attempt a decode every frame.
        if (stbOwnedPixels != nullptr) {
            stbi_image_free(stbOwnedPixels);
        }
        return std::nullopt;
    }

    try {
        m_texture = std::make_unique<Texture2D>(
            renderer.CreateTexture2D(decoded->data, decoded->width, decoded->height, "AssetPreview"));
    } catch (const std::exception&) {
        if (stbOwnedPixels != nullptr) {
            stbi_image_free(stbOwnedPixels);
        }
        m_texture.reset();
        return std::nullopt;
    }
    if (stbOwnedPixels != nullptr) {
        stbi_image_free(stbOwnedPixels);
    }

    m_descriptor =
        ImGui_ImplVulkan_AddTexture(m_texture->Sampler(), m_texture->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_width = decoded->width;
    m_height = decoded->height;
    m_cachedIsValid = true;

    return Preview{ m_descriptor, m_width, m_height };
}

} // namespace gte
