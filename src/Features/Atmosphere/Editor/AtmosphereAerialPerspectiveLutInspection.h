#pragma once

// ============================================================================
// atmosphere-scattering-2 campaign, Phase 5 - Aerial Perspective LUT Numeric
// Inspection Tool.
// ============================================================================
// See task_manager/atmosphere-scattering-2/
// PHASE5_AERIAL_LUT_NUMERIC_VALIDATION_TOOL.md for this file's own design
// reasoning - directly modeled on
// src/Editor/AtmosphereTransmittanceLutValidation.h/.cpp's shape: Editor-only
// (compiled only under GTE_ENABLE_EDITOR, per this whole folder's own
// convention - see AGENTS.md's "Editor Module Structure"), self-contained
// (built on AtmosphereLutRenderer::CaptureAerialPerspectiveVolumeSliceImmediate(),
// itself built on Renderer::ImmediateSubmit()/Renderer::CaptureImagePixels()),
// and with NO gte::rg::RenderGraph dependency of any kind.
//
// UNLIKE AtmosphereTransmittanceLutValidation.h, this is NOT a GPU-vs-CPU-
// oracle PARITY check - the aerial-perspective volume's own ray-march has no
// independent CPU oracle in this campaign's scope (and Locked Design
// Decision 5 of PHASE0_MASTER_STRATEGY.md forbids touching AtmosphereMath.h
// to build one). This is instead a plain DESCRIPTIVE-STATISTICS readback:
// what are the real min/max/mean transmittance and in-scattering values this
// volume currently holds, and does that add up to "big enough to plausibly
// be visible"?
//
// IMPORTANT, VERIFIED-AGAINST-REAL-CODE CORRECTION this phase's own strategy
// document carries (and this file follows literally): a captured
// VK_FORMAT_R16G16B16A16_SFLOAT slice's raw bytes
// (Renderer::CapturedRawPixels::pixels, or this file's own
// AtmosphereLutRenderer::CaptureAerialPerspectiveVolumeSliceImmediate()
// wrapper around it) are NOT 4 full 32-bit floats per texel - they are 4
// IEEE-754 HALF floats (2 bytes/channel, 8 bytes/texel total), the exact same
// raw layout src/Encoding/HdrColorVisualization.cpp's own
// ConvertHdrRgba16fToRgba8() already decodes. AccumulateAerialPerspectiveSliceStats()
// below therefore takes `const std::uint8_t*` (never `const float*`) and
// decodes every channel via the newly-public Encoding::DecodeHalfFloat() -
// treating this buffer as `const float*` directly would silently
// misinterpret every texel's bytes, quietly producing garbage statistics
// with no crash to flag it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gte {

class Renderer;
class AtmosphereLutRenderer;

// Pure aggregation over one already-captured rgba16f slice's raw pixel data
// (`rawRgba16fBytes`, exactly `texelCount * 8` bytes, tightly packed,
// row-major, R-G-B-A channel order - matches
// Renderer::CapturedRawPixels::pixels for this format exactly). Never
// touches a VkDevice - a real Tier-1-testable function (the half->float
// decode itself is pure integer/float bit manipulation, no GPU dependency),
// mirroring this codebase's own "pure math helper first, GPU wrapper second"
// convention (e.g. VolumeTexturePreviewMath.h/.cpp's own split).
//
// `accumulateInto` lets a caller chain per-slice accumulation across every
// one of the volume's depth slices (see InspectAerialPerspectiveVolume()
// below) without re-deriving a whole-volume aggregate from scratch each
// time - pass the previous slice's own returned result as this call's
// `accumulateInto` to keep accumulating; omit it (the default, a
// fresh-zeroed struct) to start a brand-new aggregate.
struct AerialPerspectiveSliceStats {
    float minTransmittance = 1.0f;
    float maxTransmittance = 0.0f;
    double sumTransmittance = 0.0;

    float minInScatteringMagnitude = 0.0f; // length(rgb)
    float maxInScatteringMagnitude = 0.0f;
    double sumInScatteringMagnitude = 0.0;

    std::size_t texelCount = 0;
};

AerialPerspectiveSliceStats AccumulateAerialPerspectiveSliceStats(
    const std::uint8_t* rawRgba16fBytes, std::size_t texelCount, AerialPerspectiveSliceStats accumulateInto = {});

// atmosphere-scattering-3 campaign, Phase 1 - a lightweight per-BAND
// summary (Near/Mid/Far thirds of the volume's own Z/depth range), added
// alongside the existing whole-volume AtmosphereAerialPerspectiveLutInspectionResult
// specifically to answer a question the whole-volume min/max/mean cannot:
// "is there a real, systematic DIFFERENCE between the near end and the far
// end of this volume, or is it just noisy/uniform?" - the literal
// definition of "does this LUT have a near/far gradient worth visualizing
// at all", which is this whole campaign's own root-cause question (see
// PHASE0_MASTER_STRATEGY.md, Step 2.2, point 3).
struct AerialPerspectiveBandSummary {
    int sliceBeginInclusive = 0;
    int sliceEndExclusive = 0;
    float meanTransmittance = 1.0f;
    float meanInScatteringMagnitude = 0.0f;
};

// Reduces ONE band's own accumulated AerialPerspectiveSliceStats (e.g. one
// entry of InspectAerialPerspectiveVolume()'s own bandAccumulators[3] - see
// that function's own updated body) into a single AerialPerspectiveBandSummary.
// Deliberately a NAMED, header-declared function - never a .cpp-local/
// anonymous-namespace helper - specifically so this per-band reduction is
// directly Tier-1-testable in isolation, mirroring
// FinalizeAerialPerspectiveLutInspection()'s own existing testable shape
// exactly (mean = sum / texelCount, reusing AerialPerspectiveSliceStats's own
// existing sumTransmittance/sumInScatteringMagnitude/texelCount fields
// directly - no floating-point re-derivation from raw pixels needed here,
// same contract as FinalizeAerialPerspectiveLutInspection() itself). Always
// succeeds - an empty/zero-texelCount band reports meanTransmittance=1.0f/
// meanInScatteringMagnitude=0.0f (the struct's own defaults), never a
// divide-by-zero.
AerialPerspectiveBandSummary FinalizeAerialPerspectiveBandSummary(
    const AerialPerspectiveSliceStats& bandStats, int sliceBeginInclusive, int sliceEndExclusive);

// The result of InspectAerialPerspectiveVolume() below - a whole-volume
// summary (every depth slice combined), suitable for printing straight into
// the Editor's "Atmosphere" panel (Panels/AtmospherePanel.cpp), mirroring
// AtmosphereTransmittanceLutValidationResult's own shape.
struct AtmosphereAerialPerspectiveLutInspectionResult {
    bool succeeded = false;
    std::string failureReason;

    int width = 0;
    int height = 0;
    int depth = 0;
    std::size_t texelCount = 0;

    float minTransmittance = 1.0f;
    float maxTransmittance = 1.0f;
    float meanTransmittance = 1.0f;

    float minInScatteringMagnitude = 0.0f;
    float maxInScatteringMagnitude = 0.0f;
    float meanInScatteringMagnitude = 0.0f;

    // A documented, tunable HEURISTIC (NOT a physical-correctness check like
    // AtmosphereTransmittanceLutValidationResult's own epsilon comparison) -
    // true when this volume's own numbers look "big enough to plausibly be
    // visible" once composited into an 8-bit sceneColor. `minTransmittance`/
    // `maxInScatteringMagnitude` already stand in for "the farthest slice's
    // own numbers" without needing separate per-slice tracking, since
    // transmittance only ever decreases (and in-scattering magnitude only
    // ever increases) with distance through the volume. Threshold chosen
    // loosely: an 8-bit channel's own smallest visible step is 1/255 ~=
    // 0.0039 - this heuristic requires at least 5x that much combined
    // haze/in-scattering "budget" (`(1 - minTransmittance) +
    // maxInScatteringMagnitude`) before calling it plausible, to allow
    // comfortable margin over pure quantization noise.
    bool likelyVisibleAtDefaultExposure = false;

    // atmosphere-scattering-3 campaign, Phase 1 - Near/Mid/Far (in that fixed
    // order) thirds of the volume's own Z/depth range - see
    // AerialPerspectiveBandSummary's own doc comment above for why this
    // exists alongside the whole-volume fields above.
    std::array<AerialPerspectiveBandSummary, 3> bandSummaries;
};

std::string ToDiagnosticString(const AtmosphereAerialPerspectiveLutInspectionResult& result);

// Combines per-slice stats (accumulated across every one of the volume's
// depth slices via AccumulateAerialPerspectiveSliceStats() above) into one
// final, whole-volume summary result. A pure function of its own input
// struct's accumulated sums/counts - no floating-point re-derivation from
// raw pixels needed here. Always returns `succeeded == true` - the caller
// (InspectAerialPerspectiveVolume() below) only ever calls this once the
// whole-volume capture loop has already succeeded; a capture FAILURE is
// reported directly by that caller, without ever calling this function.
AtmosphereAerialPerspectiveLutInspectionResult FinalizeAerialPerspectiveLutInspection(
    const AerialPerspectiveSliceStats& totalStats, int width, int height, int depth);

// Loops over every Z slice of the named aerial-perspective volume (via
// AtmosphereLutRenderer::CaptureAerialPerspectiveVolumeSliceImmediate() - see
// that method's own doc comment, AtmosphereLutRenderer.h, for the full "which
// approach, and why" reasoning: an ad-hoc immediate-dispatch method mirroring
// VolumeTexturePreviewRenderer::RenderPreview()'s own already-shipped
// pattern, NEVER a throwaway rg::RenderGraph + RenderGraph::Execute() call),
// accumulating AerialPerspectiveSliceStats across all of them, then finalizes
// and returns one whole-volume result. Tier-2 (GPU-touching, no automated
// test) - mirrors ValidateAtmosphereTransmittanceLut()'s own exact shape/
// caveats (graceful failure via `succeeded=false` if the named volume has
// never been generated this session, never a crash/assert).
//
// NOTE: this function issues ONE extra ad-hoc compute dispatch + one CPU
// readback PER SLICE (32 round-trips at today's fixed volume depth) -
// deliberately acceptable ONLY because this is a rare, human/LLM-triggered
// Editor button click, never a per-frame cost, mirroring
// VolumeTexturePreviewRenderer::RenderPreview()'s own identical "at most once
// per request" cost-tier reasoning.
AtmosphereAerialPerspectiveLutInspectionResult InspectAerialPerspectiveVolume(
    Renderer& renderer, AtmosphereLutRenderer& atmosphereLutRenderer, const char* aerialPerspectiveVolumeName);

} // namespace gte
