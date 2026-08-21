#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace gte {

// The result of successfully encoding a decoded image into an in-memory
// KTX2 container - see EncodeImageBytesToKtx2()/EncodeImageFileToKtx2()
// below.
struct Ktx2EncodeResult {
    std::vector<std::uint8_t> ktx2Bytes;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Decodes `fileBytes` (an already-in-memory encoded image file's raw bytes
// - PNG/JPEG/BMP/TGA/GIF/PSD/HDR/PIC/PNM, stb_image's own supported
// formats - NOT raw pixels) into RGBA8 pixels via stb_image, then
// re-encodes those pixels as a valid, single-mip-level, uncompressed KTX2
// container (VK_FORMAT_R8G8B8A8_UNORM) held entirely in memory via the
// statically-linked libktx (see cmake/FetchKTX.cmake). No GPU device/
// Renderer/ImGui involved anywhere in this path, so this is genuinely
// Tier-1-testable (see tests/Assets/Ktx2EncoderTests.cpp). Returns
// std::nullopt if `fileBytes` fails to decode as a supported image, or if
// libktx fails to build/serialize the KTX2 container for any reason -
// never throws.
//
// Deliberately produces an UNCOMPRESSED KTX2 today (no Basis Universal
// supercompression, even though the statically-linked KTX library this
// engine builds against has it baked in) - the immediate goal is format
// UNIFICATION (every imported image ships as a KTX2 payload inside a
// *.gta - see AssetImporter.h), not compression ratio. Real
// supercompression (ktxTexture2_CompressBasis()) is a natural follow-up
// once this pipeline is established - see TODO.md.
std::optional<Ktx2EncodeResult> EncodeImageBytesToKtx2(const std::vector<std::uint8_t>& fileBytes);

// Convenience wrapper: reads `sourceImagePath` fully into memory (a plain
// std::ifstream constructed from a std::filesystem::path, which correctly
// handles a non-ASCII path on Windows - see AssetPreviewTexture.cpp's own
// ReadFileBytes() comment for why this matters, and is not the same as
// calling stbi_load(path) directly), then calls EncodeImageBytesToKtx2()
// above. Returns std::nullopt if the file can't be opened/read, OR fails to
// decode/encode - never throws.
std::optional<Ktx2EncodeResult> EncodeImageFileToKtx2(const std::filesystem::path& sourceImagePath);

} // namespace gte
