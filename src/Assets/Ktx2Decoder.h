#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace gte {

// The result of successfully decoding an in-memory KTX2 container back into
// plain pixels - see DecodeKtx2ToRgba8() below.
struct Ktx2DecodeResult {
    // width * height * 4 bytes, tightly packed, top-row-first - the exact
    // same layout/convention stb_image's own stbi_load*() functions
    // produce (see AssetPreviewTexture.cpp), so a caller can feed this
    // straight into Renderer::CreateTexture2D() either way.
    std::vector<std::uint8_t> rgba8Pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Decodes `ktx2Bytes` (a complete, in-memory KTX2 container's bytes - e.g.
// a *.gta AssetType::Texture asset's own payload, see GtaFile.h) back into
// plain RGBA8 pixels - the exact opposite direction of Ktx2Encoder.h's
// EncodeImageBytesToKtx2(), and the primitive behind the Editor's
// "Inspector" panel showing a live preview for a *.gta-wrapped texture the
// same way it already does for a plain, not-yet-imported PNG/JPEG (see
// AssetPreviewTexture.h).
//
// Deliberately only understands the single, uncompressed, single-mip-
// level/layer/face VK_FORMAT_R8G8B8A8_UNORM container this engine's own
// encoder actually produces today - does NOT attempt Basis Universal
// transcoding or any other compressed/supercompressed/multi-level
// container, returning std::nullopt rather than guessing at an unsupported
// one (see TODO.md for real supercompression as a future follow-up on the
// ENCODE side, which would need a matching decode path added here too). No
// GPU device involved anywhere in this path, so this is genuinely
// Tier-1-testable (see tests/Assets/Ktx2DecoderTests.cpp). Never throws.
std::optional<Ktx2DecodeResult> DecodeKtx2ToRgba8(const std::vector<std::uint8_t>& ktx2Bytes);

} // namespace gte
