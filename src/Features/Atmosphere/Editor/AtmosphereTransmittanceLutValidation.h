#pragma once

// Reads back the real, currently-computed "AtmosphereTransmittanceLut" texture
// (via RenderGraph::DebugTextureSnapshotFor() + Renderer::CaptureImagePixels() -
// mirrors ValidateAerialPerspectiveSkyPurity()'s own exact shape, since this
// LUT is now owned by the render graph's persistent texture cache rather than
// a Texture2D this feature holds directly - see AtmosphereLutRendererImpl.cpp),
// decodes each texel's (u, v) grid position back into (heightKm, upDot) via
// AtmosphereMath.h's TransmittanceLutUvToHeightZenith(), reproduces the
// shader's own ground-occlusion check (a local helper in this .cpp, not part
// of the permanent AtmosphereMath.h oracle), and calls
// AtmosphereMath::ComputeTransmittanceToTopOfAtmosphere() for that same input -
// the permanent CPU oracle this whole feature is built around (see AGENTS.md's
// "Atmosphere Scattering" section). Reports max/mean per-channel absolute
// difference across every texel plus a pass/fail against a documented
// tolerance.
//
// TOLERANCE REASONING: "AtmosphereTransmittanceLut" is an 8-bit-per-channel
// VK_FORMAT_R8G8B8A8_UNORM texture (a transmittance value is always in [0, 1]
// per channel by definition), so UNORM quantization alone contributes up to
// 1/255 ~= 0.0039 of expected per-channel delta before any numerical-
// integration difference is even considered. Both the GPU shader and this CPU
// oracle use the identical 40-sample, forward-marching, advance-then-sample
// numerical method, so the two should agree almost exactly in exact
// arithmetic - the remaining gap is float32 accumulation-order/transcendental-
// function (exp/sqrt) differences between the GPU driver's and the CPU's own
// implementations across 40 sequentially-dependent steps. A default epsilon
// of 0.01 (~2.5x the UNORM8 quantization floor) gives comfortable, documented
// margin for both sources of difference without being so loose it would
// silently accept a genuine bug.

#include "../AtmosphereTypes.h"
#include "../../../Renderer/RenderGraph/RenderGraphTypes.h"

#include <cstddef>
#include <string>

namespace gte {

class Renderer;

namespace rg {
class RenderGraph;
} // namespace rg

// The result of ValidateAtmosphereTransmittanceLut() below - every delta is
// a plain double (never a fabricated 0.0 for a run that never actually
// happened - see `succeeded`/`failureReason`).
struct AtmosphereTransmittanceLutValidationResult {
    bool succeeded = false;
    std::string failureReason;

    int width = 0;
    int height = 0;
    std::size_t texelCount = 0;
    double epsilon = 0.01;

    double maxChannelDelta = 0.0;
    double meanChannelDelta = 0.0;
    std::size_t texelsExceedingEpsilon = 0;
};

// A human-readable, multi-line summary suitable for printing straight into
// the Editor's "Atmosphere" panel (Panels/AtmospherePanel.cpp).
std::string ToDiagnosticString(const AtmosphereTransmittanceLutValidationResult& result);

// Reads back "AtmosphereTransmittanceLut"'s real, currently-computed pixels -
// a graceful failure, not a crash, if AddTransmittanceLutPass() has never run
// yet this session - and numerically compares every texel against `params`
// fed through the exact same decode + AtmosphereMath::ComputeTransmittanceToTopOfAtmosphere()
// the real AtmosphereTransmittanceLut.comp shader itself used to generate it
// this session. `params` should normally be
// AtmosphereParameters::MakeDefaultEarthAtmosphereParameters() - this LUT's
// own physical constants are never affected by AtmosphereSettings::groundAlbedo.
AtmosphereTransmittanceLutValidationResult ValidateAtmosphereTransmittanceLut(
    Renderer& renderer, const rg::RenderGraph& renderGraph, const AtmosphereParametersGpu& params, double epsilon = 0.01);

} // namespace gte
