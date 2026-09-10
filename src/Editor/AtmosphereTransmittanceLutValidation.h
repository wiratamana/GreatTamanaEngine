#pragma once

// ============================================================================
// Atmosphere Scattering + Aerial Perspective campaign - Phase 9: Validation,
// Debug Tooling, and Docs (v1).
// ============================================================================
// See task_manager/atmosphere-scattering-1/
// ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md (Step 3.1) for
// this file's own design reasoning - directly modeled on
// src/Editor/GpuSkinningValidation.h/.cpp's shape: Editor-only
// (GTE_ENABLE_EDITOR), self-contained (built on Renderer::CaptureImagePixels(),
// which itself is built on Renderer::ImmediateSubmit() - see Renderer.cpp),
// and with NO gte::rg::RenderGraph dependency of any kind - this tool never
// adds a pass to a graph; it only ever reads back whatever the ALREADY-
// RUNNING, permanent, every-frame atmosphere pass sequence
// (src/Application/AtmospherePassSequence.h/.cpp) most recently wrote into
// "AtmosphereTransmittanceLut".
//
// ONE numeric parity check, mirroring GpuSkinningValidation's own GPU-vs-
// CPU-oracle shape:
//
//   ValidateAtmosphereTransmittanceLut() - reads back the REAL, currently-
//   computed "AtmosphereTransmittanceLut" texture (via
//   Renderer::CaptureImagePixels() - the exact same generic image-readback
//   primitive GET /get_texture itself uses, see Renderer.h), decodes each
//   texel's own (u, v) grid position back into (heightKm, upDot) via
//   AtmosphereMath.h's TransmittanceLutUvToHeightZenith() (added THIS
//   phase - see that file's own doc comment), reproduces
//   AtmosphereTransmittanceLut.comp's own ground-occlusion check (a LOCAL
//   helper in this .cpp, NOT part of the permanent AtmosphereMath.h oracle
//   - ground occlusion is explicitly out of that file's own scope, see its
//   doc comment on ComputeOpticalDepthToTopOfAtmosphere()), and calls
//   AtmosphereMath::ComputeTransmittanceToTopOfAtmosphere() for that exact
//   same input - the permanent CPU oracle this whole campaign is built
//   around (see AGENTS.md's "Atmosphere Scattering" section). Reports
//   max/mean per-channel absolute difference across every texel plus a
//   pass/fail against a documented tolerance.
//
// TOLERANCE REASONING: "AtmosphereTransmittanceLut" is stored as an 8-bit-
// per-channel VK_FORMAT_R8G8B8A8_UNORM texture (Phase 3's own deliberate
// choice - a transmittance value is always in [0, 1] per channel by
// definition), so UNORM quantization alone contributes up to 1/255 ~=
// 0.0039 of expected per-channel delta before any numerical-integration
// difference is even considered. Both the GPU shader and this CPU oracle
// use the IDENTICAL 40-sample, forward-marching, advance-then-sample
// numerical method (see AtmosphereMath.h's own doc comment on
// ComputeOpticalDepthToTopOfAtmosphere()), so the two should agree almost
// exactly in exact arithmetic - the remaining gap is float32 accumulation-
// order/transcendental-function (exp/sqrt) differences between the GPU
// driver's and the CPU's own implementations across 40 sequentially-
// dependent steps. A default epsilon of 0.01 (~2.5x the UNORM8
// quantization floor) gives comfortable, documented margin for both
// sources of difference without being so loose it would silently accept a
// genuine bug (e.g. a sign error, a completely wrong height/angle decode,
// or the wrong sample count) - any of those would produce deltas far
// larger than 0.01 in practice.

#include "../Renderer/Atmosphere/AtmosphereTypes.h"

#include <cstddef>
#include <string>

namespace gte {

class Renderer;
class AtmosphereLutRenderer;

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
// the Editor's "Atmosphere" panel (Panels/AtmospherePanel.cpp) - mirrors
// GpuSkinningValidation.h's own ToDiagnosticString() shape exactly (a
// second, distinct overload - never a collision, since the two live in
// separate headers and take different result types).
std::string ToDiagnosticString(const AtmosphereTransmittanceLutValidationResult& result);

// Reads back "AtmosphereTransmittanceLut"'s REAL, currently-computed pixels
// (via `atmosphereLutRenderer.TransmittanceLutOutput()` - a graceful
// failure, not a crash, if AddTransmittanceLutPass() has never run yet this
// session; in practice it always has by the time the Editor's own
// "Atmosphere" panel button that calls this is even clickable, since the
// atmosphere pass sequence runs unconditionally every frame) and
// numerically compares every texel against `params` fed through the exact
// same decode + AtmosphereMath::ComputeTransmittanceToTopOfAtmosphere() the
// real AtmosphereTransmittanceLut.comp shader itself used to generate it
// this session. `params` should normally be
// AtmosphereParameters::MakeDefaultEarthAtmosphereParameters() - this LUT's
// own physical constants are never affected by
// AtmosphereSettings::groundAlbedoTint (see AtmosphereMath.h's own
// ComputeExtinctionCoefficientAtHeight(), which never reads groundAlbedo at
// all).
AtmosphereTransmittanceLutValidationResult ValidateAtmosphereTransmittanceLut(Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, const AtmosphereParametersGpu& params, double epsilon = 0.01);

} // namespace gte
