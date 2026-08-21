#include "Ktx2Encoder.h"

#include <stb_image.h> // Declarations only - see StbImageImpl.cpp for the ONE STB_IMAGE_IMPLEMENTATION translation unit.

#include <ktx.h>
#include <volk.h> // VK_FORMAT_R8G8B8A8_UNORM

#include <cstdlib>
#include <fstream>

namespace gte {

std::optional<Ktx2EncodeResult> EncodeImageBytesToKtx2(const std::vector<std::uint8_t>& fileBytes)
{
    if (fileBytes.empty()) {
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

    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
    createInfo.baseWidth = static_cast<ktx_uint32_t>(width);
    createInfo.baseHeight = static_cast<ktx_uint32_t>(height);
    createInfo.baseDepth = 1;
    createInfo.numDimensions = 2;
    createInfo.numLevels = 1;
    createInfo.numLayers = 1;
    createInfo.numFaces = 1;
    createInfo.isArray = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktxTexture2* texture = nullptr;
    ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
    if (result != KTX_SUCCESS || texture == nullptr) {
        stbi_image_free(pixels);
        return std::nullopt;
    }

    const ktx_size_t imageSize = static_cast<ktx_size_t>(width) * static_cast<ktx_size_t>(height) * 4;
    result = ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, pixels, imageSize);
    stbi_image_free(pixels); // stb_image's decoded buffer is no longer needed either way past this point.
    if (result != KTX_SUCCESS) {
        ktxTexture_Destroy(ktxTexture(texture));
        return std::nullopt;
    }

    ktx_uint8_t* ktxBytes = nullptr;
    ktx_size_t ktxSize = 0;
    result = ktxTexture_WriteToMemory(ktxTexture(texture), &ktxBytes, &ktxSize);
    ktxTexture_Destroy(ktxTexture(texture));
    if (result != KTX_SUCCESS || ktxBytes == nullptr) {
        return std::nullopt;
    }

    Ktx2EncodeResult encoded;
    encoded.ktx2Bytes.assign(ktxBytes, ktxBytes + ktxSize);
    encoded.width = static_cast<std::uint32_t>(width);
    encoded.height = static_cast<std::uint32_t>(height);
    std::free(ktxBytes); // libktx allocates this buffer via malloc/realloc internally (see lib/memstream.c) - freed with plain free(), not ktxTexture_Destroy().

    return encoded;
}

std::optional<Ktx2EncodeResult> EncodeImageFileToKtx2(const std::filesystem::path& sourceImagePath)
{
    std::ifstream file(sourceImagePath, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }

    const std::streamoff size = file.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        return std::nullopt;
    }

    return EncodeImageBytesToKtx2(bytes);
}

} // namespace gte
