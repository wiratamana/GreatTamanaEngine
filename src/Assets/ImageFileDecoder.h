#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// The result of successfully decoding an on-disk image file straight into
// plain RGBA8 pixels - same shape as Ktx2Decoder.h's Ktx2DecodeResult (width
// * height * 4 tightly-packed bytes, top-row-first, exactly what
// Renderer::CreateTexture2D()/CreateMaterialTexture2D() expect), just a
// separate struct/function since the INPUT here is a plain PNG/JPEG/etc.
// file on disk, never a KTX2 container.
struct DecodedImageRgba8 {
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Decodes the image file at `filePath` (a plain filesystem path, UTF-8
// encoded - matches every other path-taking function in this engine, e.g.
// PmxLoader.h's LoadPmxModel()) straight into RGBA8 pixels via stb_image,
// with NO KTX2/`*.gta` wrapping involved at all - the primitive behind
// Game::EnsureMeshAsset() (src/Game/Game.cpp) loading a PMX material's
// diffuse texture directly from wherever the source .pmx's own tex/ folder
// resolved to (see MaterialData::textures), as opposed to
// Ktx2Encoder.h/Ktx2Decoder.h's own "import into the Project as a *.gta
// AssetType::Texture asset first" pipeline. Returns std::nullopt (never
// throws) for a missing/corrupt/undecodable file - the same "best-effort,
// degrade gracefully" contract as PmxLoader.h's own texture-path resolution
// (see MaterialData::textures' doc comment).
std::optional<DecodedImageRgba8> DecodeImageFileToRgba8(const std::string& filePath);

} // namespace gte
