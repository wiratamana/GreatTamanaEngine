#include "AssetPreviewTexture.h"

#include "../Encoding/Utf8Path.h"
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()
#include "../Assets/Ktx2Decoder.h" // DecodeKtx2ToRgba8()
#include "../Renderer/Renderer.h"
#include "../Renderer/Texture2D.h"

// Only stb_image's declarations are needed here; its implementation is compiled once in src/Assets/StbImageImpl.cpp.
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

namespace gte {

namespace {

// Reads a UTF-8 path fully into memory, or nullopt on any failure. Goes through std::filesystem::path so
// non-ASCII names work on Windows.
std::optional<std::vector<unsigned char>> ReadFileBytes(const std::string& absolutePathUtf8)
{
    std::ifstream file(PathFromUtf8(absolutePathUtf8), std::ios::binary | std::ios::ate);
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

// True if `absolutePathUtf8` has the *.gta extension (case-insensitive).
bool HasGtaExtension(const std::string& absolutePathUtf8)
{
    std::string extension = PathToUtf8(PathFromUtf8(absolutePathUtf8).extension());
    std::transform(
        extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".gta";
}

// Decoded RGBA8 pixels, owned either by stb_image or by a caller-side vector.
struct DecodedPixels {
    const void* data = nullptr;
    int width = 0;
    int height = 0;
};

// Decodes a texture-type *.gta (KTX2 payload) or any plain image file. `stbOwnedPixels` receives stb_image's
// allocation (the caller frees it with stbi_image_free()); the *.gta path stores its pixels in `gtaOwnedPixels`.
std::optional<DecodedPixels> DecodePixels(
    const std::string& absolutePath, unsigned char*& stbOwnedPixels, std::vector<std::uint8_t>& gtaOwnedPixels)
{
    if (HasGtaExtension(absolutePath)) {
        const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(absolutePath));
        if (!gta.has_value() || gta->header.Type() != AssetType::Texture) {
            // Not a valid *.gta, or one that wraps something other than a texture.
            return std::nullopt;
        }

        const std::optional<Ktx2DecodeResult> decoded = DecodeKtx2ToRgba8(gta->payload);
        if (!decoded.has_value()) {
            return std::nullopt;
        }

        gtaOwnedPixels = std::move(decoded->rgba8Pixels);
        return DecodedPixels{ gtaOwnedPixels.data(), static_cast<int>(decoded->width), static_cast<int>(decoded->height) };
    }

    // Decode from memory: stb_image's own path-based loader cannot open non-ASCII paths on Windows.
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
    // The GPU may still use the texture and its descriptor; retiring waits for it first.
    if (m_renderer != nullptr && (m_texture || m_descriptor != UiTextureHandle::Invalid)) {
        m_renderer->RetireUiTexture(m_descriptor, std::move(m_texture));
    }
    m_texture.reset();
    m_descriptor = UiTextureHandle::Invalid;
    m_cachedPath.clear();
    m_cachedWriteTime = std::filesystem::file_time_type{};
    m_cachedIsValid = false;
    m_width = 0;
    m_height = 0;
}

std::optional<AssetPreviewTexture::Preview> AssetPreviewTexture::Resolve(
    Renderer& renderer, const std::string& absolutePath)
{
    m_renderer = &renderer;

    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime =
        std::filesystem::last_write_time(std::filesystem::path(absolutePath), timeEc);

    if (!absolutePath.empty() && absolutePath == m_cachedPath && !timeEc && writeTime == m_cachedWriteTime) {
        // Same unchanged file: reuse the last result (a valid preview or a remembered failure).
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
        // m_cachedIsValid stays false, so a broken file is not decoded again every frame.
        if (stbOwnedPixels != nullptr) {
            stbi_image_free(stbOwnedPixels);
        }
        return std::nullopt;
    }

    try {
        m_texture = std::make_unique<Texture2D>(renderer.CreateTexture2D(
            { .pixelsRgba8 = decoded->data, .width = decoded->width, .height = decoded->height, .debugName = "AssetPreview" }));
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

    m_descriptor = renderer.GetUiTextureHandle(*m_texture);
    m_width = decoded->width;
    m_height = decoded->height;
    m_cachedIsValid = true;

    return Preview{ m_descriptor, m_width, m_height };
}

} // namespace gte
