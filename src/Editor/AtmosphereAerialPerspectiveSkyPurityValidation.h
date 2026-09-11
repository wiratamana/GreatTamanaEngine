#pragma once

// ============================================================================
// atmosphere-scattering-4 campaign, Phase 3 - Aerial Perspective Sky Purity
// Validation Tool.
// ============================================================================
// See task_manager/atmosphere-scattering-4/PHASE3_REGRESSION_DIAGNOSTIC_TOOLING.md
// for this file's own design reasoning - directly modeled on
// src/Editor/AtmosphereTransmittanceLutValidation.h/.cpp's shape: Editor-only
// (GTE_ENABLE_EDITOR), self-contained (Renderer::CaptureImagePixels()-based),
// NO gte::rg::RenderGraph PASS added (this tool only ever READS the graph's
// own DebugTextureSnapshot registry, via RenderGraph::DebugTextureSnapshotFor()
// - the exact same primitive GET /get_texture's own handler uses).
//
// WHAT THIS PROVES: for every pixel where the PRE-composite render target's
// own depth says "no opaque geometry was drawn here this frame" (per
// AtmosphereAerialPerspectiveCompositeMath.h's own
// ShouldBypassAerialPerspectiveComposite() - the SAME predicate the composite
// shader itself now uses, see PHASE1/PHASE2), the POST-composite output
// texture's color must be identical (within an 8-bit quantization tolerance)
// to the PRE-composite color at that exact pixel. This is the permanent,
// automated regression guard for
// AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md: a non-zero mismatch
// count means the composite pass is, once again, altering a pixel it must
// never touch.

#include <cstddef>
#include <string>

namespace gte {

class Renderer;

namespace rg {
class RenderGraph;
}

struct AtmosphereAerialPerspectiveSkyPurityResult {
    bool succeeded = false;
    std::string failureReason;

    int width = 0;
    int height = 0;

    // How many pixels this frame's PRE-composite depth buffer reported as
    // "no opaque geometry" (ShouldBypassAerialPerspectiveComposite() ==
    // true) - i.e. how many pixels this check actually covers. A scene that
    // is 100% covered by opaque geometry (no sky visible at all) will
    // legitimately report 0 here - see ToDiagnosticString()'s own handling
    // of that case.
    std::size_t skyPixelCount = 0;

    // Of skyPixelCount above, how many differ between pre- and
    // post-composite by more than `toleranceUnorm8` on at least one RGB
    // channel. MUST be 0 for a passing result.
    std::size_t mismatchingSkyPixelCount = 0;

    double maxSkyPixelChannelDelta = 0.0;  // in [0,1] units, largest single-channel |pre - post| seen across every sky pixel.
    double meanSkyPixelChannelDelta = 0.0; // averaged across every sky pixel (0.0 if skyPixelCount == 0).

    // One 8-bit UNORM quantization step by default (1/255) - both textures
    // are ordinary 8-bit-per-channel color targets, so a byte-identical
    // pass-through can still legitimately differ by up to this much due to
    // two independent UNORM encode/decode round-trips (this render target's
    // own write, then the compute shader's own texture()/imageStore()
    // round-trip) - mirrors AtmosphereTransmittanceLutValidationResult's own
    // "epsilon" reasoning (see that file's own header comment) applied to a
    // pass-through check instead of a physical-formula parity check.
    double toleranceUnorm8 = 1.0 / 255.0;

    // succeeded && mismatchingSkyPixelCount == 0 - the actual pass/fail a
    // caller should branch on (mirrors
    // AtmosphereTransmittanceLutValidationResult's own "succeeded && ...
    // == 0" pattern already used by AtmospherePanel.cpp's "Validate
    // Transmittance LUT" button).
    bool Passed() const noexcept { return succeeded && mismatchingSkyPixelCount == 0; }
};

std::string ToDiagnosticString(const AtmosphereAerialPerspectiveSkyPurityResult& result);

// Compares `preCompositeColorTextureName`'s (e.g. "GameView") own real,
// currently-registered color+depth against
// `compositedColorTextureName`'s (e.g. "GameViewComposited") own real,
// currently-registered color, both read back via
// RenderGraph::DebugTextureSnapshotFor() + Renderer::CaptureImagePixels() -
// see this file's own header comment. Graceful `succeeded=false` (never a
// crash/assert) if either name is not currently registered, the pre-
// composite name has no depth half, or the two textures' extents disagree.
AtmosphereAerialPerspectiveSkyPurityResult ValidateAerialPerspectiveSkyPurity(Renderer& renderer,
    const rg::RenderGraph& renderGraph, const char* preCompositeColorTextureName,
    const char* compositedColorTextureName, double toleranceUnorm8 = 1.0 / 255.0);

} // namespace gte
