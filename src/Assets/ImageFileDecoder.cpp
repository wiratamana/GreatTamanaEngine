#include "ImageFileDecoder.h"

#include "../Encoding/Utf8Path.h"

#include <stb_image.h> // Declarations only - see StbImageImpl.cpp for the ONE STB_IMAGE_IMPLEMENTATION translation unit.

#include <filesystem>
#include <fstream>

namespace gte {

std::optional<DecodedImageRgba8> DecodeImageFileToRgba8(const std::string& filePath)
{
    // Read via an ifstream on a UTF-8-converted path (non-ASCII names work), decode from memory.
    std::ifstream file(PathFromUtf8(filePath), std::ios::binary | std::ios::ate);
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
