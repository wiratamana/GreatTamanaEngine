#include "PngEncoder.h"

#include <stdexcept>

// The ONE translation unit in the entire engine that compiles
// stb_image_write's actual implementation (STB_IMAGE_WRITE_IMPLEMENTATION) -
// mirroring src/Assets/StbImageImpl.cpp's own STB_IMAGE_IMPLEMENTATION
// precedent for the decode-side stb_image.h. Every other translation unit
// needing stb_image_write's declarations must #include <stb_image_write.h>
// WITHOUT defining STB_IMAGE_WRITE_IMPLEMENTATION itself - defining it more
// than once would be an ODR violation (multiple definitions of every
// stbi_write_* function across translation units).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace gte::Encoding {

namespace {

// stbi_write_png_to_func's own callback signature -
// void callback(void* context, void* data, int size). `context` here is a
// std::vector<std::uint8_t>* that this callback simply appends to. Written
// to APPEND whatever it's given regardless of whether it's called once or
// multiple times for the whole encoded PNG, so this stays correct even if a
// future stb_image_write version ever chunks its output differently than it
// does today (a single call for the whole buffer).
void AppendEncodedBytes(void* context, void* data, int size) {
    if (context == nullptr || data == nullptr || size <= 0) {
        return;
    }
    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

std::vector<std::uint8_t> EncodeRgba8ToPng(const std::uint8_t* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        throw std::runtime_error("EncodeRgba8ToPng: width/height must both be > 0 and rgba must not be null.");
    }

    std::vector<std::uint8_t> output;
    const int channels = 4; // RGBA
    const int strideInBytes = width * channels;

    const int ok = stbi_write_png_to_func(
        &AppendEncodedBytes,
        &output,
        width,
        height,
        channels,
        rgba,
        strideInBytes);

    if (ok == 0 || output.empty()) {
        throw std::runtime_error("EncodeRgba8ToPng: stbi_write_png_to_func failed to encode the PNG.");
    }

    return output;
}

} // namespace gte::Encoding
