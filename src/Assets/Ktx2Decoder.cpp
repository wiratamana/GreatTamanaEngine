#include "Ktx2Decoder.h"

#include <ktx.h>
#include <volk.h> // VK_FORMAT_R8G8B8A8_UNORM

namespace gte {

std::optional<Ktx2DecodeResult> DecodeKtx2ToRgba8(const std::vector<std::uint8_t>& ktx2Bytes)
{
    if (ktx2Bytes.empty()) {
        return std::nullopt;
    }

    ktxTexture2* texture = nullptr;
    ktx_error_code_e result = ktxTexture2_CreateFromMemory(
        ktx2Bytes.data(), ktx2Bytes.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    if (result != KTX_SUCCESS || texture == nullptr) {
        return std::nullopt;
    }

    // Only the exact container EncodeImageBytesToKtx2() (Ktx2Encoder.h)
    // produces - a single-mip, single-layer, single-face, uncompressed
    // VK_FORMAT_R8G8B8A8_UNORM 2D texture - is supported here; anything
    // else (a Basis Universal-transcodable texture, a block-compressed
    // format, mips/array layers/cube faces) is deliberately rejected
    // rather than guessed at - see this file's own header comment.
    if (texture->vkFormat != VK_FORMAT_R8G8B8A8_UNORM || texture->supercompressionScheme != KTX_SS_NONE
        || texture->numLevels != 1 || texture->numLayers != 1 || texture->numFaces != 1
        || ktxTexture2_NeedsTranscoding(texture)) {
        ktxTexture_Destroy(ktxTexture(texture));
        return std::nullopt;
    }

    ktx_size_t offset = 0;
    result = ktxTexture_GetImageOffset(ktxTexture(texture), 0, 0, 0, &offset);
    if (result != KTX_SUCCESS) {
        ktxTexture_Destroy(ktxTexture(texture));
        return std::nullopt;
    }

    const ktx_uint8_t* data = ktxTexture_GetData(ktxTexture(texture));
    const ktx_size_t imageSize = ktxTexture_GetImageSize(ktxTexture(texture), 0);
    if (data == nullptr || imageSize == 0) {
        ktxTexture_Destroy(ktxTexture(texture));
        return std::nullopt;
    }

    Ktx2DecodeResult decoded;
    decoded.width = texture->baseWidth;
    decoded.height = texture->baseHeight;
    decoded.rgba8Pixels.assign(data + offset, data + offset + imageSize);

    ktxTexture_Destroy(ktxTexture(texture));
    return decoded;
}

} // namespace gte
