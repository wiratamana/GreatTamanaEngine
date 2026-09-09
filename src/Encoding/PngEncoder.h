#pragma once

#include <cstdint>
#include <vector>

namespace gte::Encoding {

// Encodes a tightly-packed, row-major RGBA8 pixel buffer (width*height*4
// bytes, no row padding, R/G/B/A channel order - see PixelConversion.h if
// your source data is BGRA instead) into an in-memory PNG file, returned as
// a byte vector ready to write to disk or hand back as an HTTP response
// body.
//
// Throws std::runtime_error if the underlying stb_image_write call reports
// failure (in practice this should never happen for well-formed, correctly-
// sized RGBA8 input - stb_image_write's own failure modes are essentially
// "invalid parameters", which this function's own signature already
// prevents by construction), or if width/height aren't both > 0.
std::vector<std::uint8_t> EncodeRgba8ToPng(const std::uint8_t* rgba, int width, int height);

} // namespace gte::Encoding
