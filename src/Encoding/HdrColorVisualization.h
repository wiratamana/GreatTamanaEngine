#pragma once

#include <cstdint>
#include <volk.h>

namespace gte::Encoding {

// Atmosphere Scattering + Aerial Perspective campaign, Phase 4
// (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md)
// - the color-side counterpart of DepthVisualization.h's
// ConvertDepthToGrayscaleRgba8(), for a genuinely NEW situation neither that
// function nor ConvertBgraToRgbaInPlace() (PixelConversion.h) was ever built
// to handle: `GET /get_texture` capturing a HDR (16-bit-per-channel float)
// color texture. Every capturable texture before this phase (Game/Scene/
// Swapchain views, the Transmittance LUT) was always exactly 4 bytes/pixel
// (an 8-bit-per-channel format) - see Renderer::CaptureImagePixels()'s own
// "bytesPerPixel must be exactly 4 for every real caller today" doc comment,
// written before this phase existed. This phase's own Multi-Scattering LUT
// (`AtmosphereLutRenderer::AddMultiScatteringLutPass()`) is the FIRST
// capturable texture in the engine's history that genuinely needs an HDR
// format (VK_FORMAT_R16G16B16A16_SFLOAT, 8 bytes/pixel) - a multi-scattering
// "response" value can legitimately exceed 1.0, so an 8-bit UNORM format
// would clip it (see this campaign's own "Revision Notes").
//
// Supports EXACTLY VK_FORMAT_R16G16B16A16_SFLOAT (the one HDR color format
// this campaign introduces) - returns `false` (leaving `outRgba8` UNTOUCHED)
// for any other format, mirroring ConvertDepthToGrayscaleRgba8()'s own
// "accepted narrow risk, explicitly documented, never a crash or a
// silently-wrong result" convention for an unrecognized format.
//
// Uses a simple Reinhard tonemap (`value / (1 + value)`, mapping
// `[0, +inf)` monotonically into `[0, 1)`) rather than a plain clamp, since
// a plain clamp-to-[0,1]-then-scale would saturate every texel whose real
// HDR value already exceeds 1.0 to solid white, destroying exactly the
// low-frequency gradient structure this phase's own visual sanity check
// (see the strategy document's own Step 3/Step 5) needs to actually SEE.
// This is a debug-visualization-only tonemap - it is never used anywhere in
// this campaign's actual GPU shading math (Phase 7's real sky/aerial-
// perspective composite passes will do their own, separate exposure/
// tonemapping work, entirely unrelated to this capture-only helper).
//
// `rawRgba16f` is exactly width*height*8 bytes, tightly packed, row-major,
// R-G-B-A channel order (matches VK_FORMAT_R16G16B16A16_SFLOAT's own memory
// layout - no BGRA swap ever needed for this format). `outRgba8` must
// already be sized to width*height*4 bytes before calling - this function
// only ever WRITES into it, never resizes it (mirrors
// ConvertDepthToGrayscaleRgba8()'s own out-buffer convention). A 0-sized
// buffer (width <= 0 or height <= 0) is a safe no-op; a NULL
// `rawRgba16f`/`outRgba8` with a genuinely positive width/height is also a
// safe no-op returning `false`.
bool ConvertHdrRgba16fToRgba8(
    const std::uint8_t* rawRgba16f, VkFormat colorFormat, int width, int height, std::uint8_t* outRgba8);

// atmosphere-scattering-2 campaign, Phase 5
// (task_manager/atmosphere-scattering-2/PHASE5_AERIAL_LUT_NUMERIC_VALIDATION_TOOL.md)
// - a PURE visibility change only: this is the exact same IEEE-754 binary16
// -> binary32 conversion ConvertHdrRgba16fToRgba8() above already used
// internally (previously a private HalfToFloat() helper in this .cpp's own
// anonymous namespace), now exposed publicly so a second consumer
// (src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp, which reads back
// raw VK_FORMAT_R16G16B16A16_SFLOAT bytes and must decode each half-float
// channel itself before computing any min/max/mean statistic) never needs a
// second, independently-maintained copy of this bit-twiddling logic - see
// AGENTS.md's "one implementation, no duplication" discipline.
// ConvertHdrRgba16fToRgba8()'s own behavior/tests are completely unchanged by
// this move - it now simply calls this newly-public function instead of a
// private one.
//
// Handles zero, subnormals, normals, infinities, and NaN - no hardware F16C
// instruction dependency, since every real caller of this function is a
// rare, human/LLM-triggered debug-capture path, never a per-frame hot path.
float DecodeHalfFloat(std::uint16_t half) noexcept;

} // namespace gte::Encoding
