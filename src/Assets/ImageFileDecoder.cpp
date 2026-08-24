#include "ImageFileDecoder.h"

#include <stb_image.h> // Declarations only - see StbImageImpl.cpp for the ONE STB_IMAGE_IMPLEMENTATION translation unit.

#include <filesystem>
#include <fstream>

namespace gte {

namespace {

// std::filesystem::path(const std::string&) goes through the OS's native
// narrow encoding (the current ANSI codepage on Windows), NOT UTF-8 - the
// same pitfall this engine avoids everywhere else a UTF-8 path crosses into
// std::filesystem/std::ifstream (see Game.cpp's Utf8PathFromGamePath(),
// AssetImporter.cpp's PathToUtf8(), PmxLoader.cpp's Utf8ToPath()). Textures
// referenced by a real-world .pmx routinely have non-ASCII (e.g. Japanese/
// Chinese) filenames, so this round-trip is not optional here - reading the
// file's bytes via a std::filesystem::path-constructed std::ifstream (which
// uses the native wide API on Windows) and decoding via
// stbi_load_from_memory (which never itself touches the filesystem) sidesteps
// stb_image's own plain stbi_load(const char*)'s narrow/ANSI fopen()
// entirely - the exact same reasoning/pattern as Ktx2Encoder.cpp's
// EncodeImageFileToKtx2().
std::filesystem::path Utf8ToPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

} // namespace

std::optional<DecodedImageRgba8> DecodeImageFileToRgba8(const std::string& filePath)
{
    std::ifstream file(Utf8ToPath(filePath), std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }

    const std::streamoff size = file.tellg();
    if (size <= 0) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> fileBytes(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(fileBytes.data()), static_cast<std::streamsize>(size))) {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        fileBytes.data(), static_cast<int>(fileBytes.size()), &width, &height, &sourceChannels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return std::nullopt;
    }

    DecodedImageRgba8 decoded;
    decoded.width = static_cast<std::uint32_t>(width);
    decoded.height = static_cast<std::uint32_t>(height);
    const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    decoded.pixels.assign(pixels, pixels + byteCount);
    stbi_image_free(pixels);

    return decoded;
}

} // namespace gte
