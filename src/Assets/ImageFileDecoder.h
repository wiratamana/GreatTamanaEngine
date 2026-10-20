#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// Decoded plain RGBA8 pixels: width * height * 4 tightly packed bytes, top row first.
struct DecodedImageRgba8 {
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Decodes the plain image file at the UTF-8 `filePath` into RGBA8 via stb_image (no KTX2/`*.gta` wrapping).
// Returns std::nullopt (never throws) for a missing, corrupt or undecodable file.
std::optional<DecodedImageRgba8> DecodeImageFileToRgba8(const std::string& filePath);

} // namespace gte
