#pragma once

// ============================================================================
// GPU Vertex Skinning campaign - Phase 6: Validation & Parity Testing (v2).
// ============================================================================
// See task_manager/gpu_skinning/GPU_SKINNING_PHASE6_VALIDATION_PARITY_TESTING_STRATEGY_v2.md
// for the full design reasoning and, IMPORTANTLY, its own "V2 Revision
// Notes": this manual, Editor-tool-based validation is the PRIMARY and
// REQUIRED deliverable of this phase - NOT a stretch goal, and NOT expected
// to become an automated GoogleTest until this repository's own separate,
// already-tracked "Tier 2 GPU test fixture" TODO item lands first (this
// repo has zero precedent for a live-VkDevice-requiring GoogleTest today -
// see TESTING.md/TODO.md). This file is deliberately the exact same shape
// as src/Editor/ComputeBlurValidation.h/.cpp: Editor-only (GTE_ENABLE_EDITOR),
// Tier 2 (needs a live Renderer/VkDevice to do anything at all), and
// verified by a human running the Editor and reading its console/log
// output - not by ctest.
//
// Two independent checks, mirroring Phase 6 v2's own Step 3.1/Step 3.3:
//
//   1. ValidateGpuSkinningAgainstCpuOracle() - the numeric parity check: for
//      one model's bind pose/skin weights/bone matrices, runs BOTH the CPU
//      oracle (Animation/VertexSkinning.h's own SkinVertexRange() - see
//      AGENTS.md's "the CPU path stays the oracle, never modified to
//      'agree' with the GPU path" rule) and the real GPU compute kernel
//      (via a SELF-CONTAINED one-shot Renderer::ImmediateSubmit() call -
//      deliberately NOT routed through the full gte::rg::RenderGraph, so
//      this tool has no dependency on Phase 3/5's per-frame wiring being
//      live), reads the GPU result back to the CPU via a staging buffer,
//      and reports max/mean per-vertex position/normal deltas plus a count
//      of vertices exceeding a documented epsilon.
//
//   2. ValidateGpuSkinningGroupingParity() - a structural sanity check: for
//      an already-registered model (via GpuSkinningRigCache::Register(),
//      Phase 4), confirms the CPU path's own
//      GroupMeshAssetPartsBySharedVertexBuffer() grouping (used by
//      AnimationSystem::Update()'s own CPU skinning branch) produces the
//      EXACT SAME group COUNT the GPU cache ended up with - i.e. that the
//      two independently-invoked de-duplication passes over the SAME
//      `parts` list genuinely agree (see Phase 6 v2, Step 3.3).
//
// Neither function is wired into any Editor UI/menu by this phase - per
// this campaign's own phase fence (Phase 7, "Editor Toggle & Profiling
// UX", is still TODO and owns the actual user-facing wiring). A developer
// invokes these directly (e.g. a temporary call from Application::Run(),
// or a future debug menu item) against a real, already-loaded rigged
// model and reads the resulting ToDiagnosticString()/failureReason output.

#include "../Animation/AnimationPoseEvaluator.h"
#include "../Assets/MeshData.h"
#include "../Game/Instantiation/MeshAssetGpuCatalog.h"
#include "../Game/Animation/GpuSkinningRigCache.h"
#include "../Math/Mat4.h"
#include "../Math/Vec2.h"
#include "../Math/Vec3.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gte {

class Renderer;
class RenderSystem;

// The result of ValidateGpuSkinningAgainstCpuOracle() below - every delta
// is a plain double (Euclidean distance between the CPU oracle's and the
// GPU kernel's own output for the same vertex), never a fabricated 0.0 for
// a run that never actually happened (see `succeeded`/`failureReason`).
struct GpuSkinningValidationResult {
    bool succeeded = false;
    std::string failureReason;

    std::size_t vertexCount = 0;
    bool textured = false;
    double epsilon = 1e-4;

    double maxPositionDelta = 0.0;
    double meanPositionDelta = 0.0;
    double maxNormalDelta = 0.0;
    double meanNormalDelta = 0.0;
    std::size_t verticesExceedingEpsilon = 0;
};

// A human-readable, multi-line summary suitable for printing straight to
// the console/log from a manual Editor debug command - see this file's own
// header comment for why this is the PRIMARY deliverable this phase
// requires, not an automated assertion.
std::string ToDiagnosticString(const GpuSkinningValidationResult& result);

// Runs a SELF-CONTAINED (no GpuSkinningRigCache/RenderGraph dependency at
// all - see this campaign's Phase 6 v2 strategy document, Step 3.1) CPU-
// vs-GPU numeric parity check for one distinct model's vertex data: packs
// and uploads the bind-pose/skin-weight/bone-matrix (and, when `uvs` is
// non-empty, UV) GPU buffers itself via Phase 1's PackBindPoseVertices()/
// PackSkinWeights()/PackUvs(), dispatches whichever compute kernel variant
// matches (GpuSkinningPipelines::PositionNormalPipeline() vs.
// PositionNormalUvPipeline(), selected automatically by whether `uvs` is
// non-empty), reads the result back to the CPU via a staging buffer, and
// compares vertex-for-vertex against Animation::SkinVertices()'s own CPU
// output (the oracle).
//
// `uvs` may be empty for an untextured model (the PositionNormal variant is
// used in that case); otherwise it must have exactly as many entries as
// `bindPositions` or this call reports failure rather than reading out of
// bounds.
//
// `pipelines` is EnsureInitialized() internally if not already - safe to
// pass a fresh, never-yet-used GpuSkinningPipelines instance.
GpuSkinningValidationResult ValidateGpuSkinningAgainstCpuOracle(Renderer& renderer, GpuSkinningPipelines& pipelines,
    const std::vector<Vec3>& bindPositions, const std::vector<Vec3>& bindNormals,
    const std::vector<VertexSkinWeights>& skinWeights, const std::vector<Vec2>& uvs,
    const std::vector<Mat4>& skinningMatrices, double epsilon = 1e-4);

// The result of ValidateGpuSkinningGroupingParity() below - a purely
// structural (never numeric) check.
struct GpuSkinningGroupingParityResult {
    bool succeeded = false;
    std::string failureReason;
    std::size_t cpuGroupCount = 0;
    std::size_t gpuGroupCount = 0;
};

// Phase 6 v2, Step 3.3 ("Multi-part / shared-vertex-buffer parity") -
// cross-checks that an already-registered model's GpuSkinningRigCache::
// GpuModelEntry (Phase 4) ended up with exactly one OutputGroup per
// distinct shared vertex buffer the CPU path's own
// GroupMeshAssetPartsBySharedVertexBuffer() (src/Game/Instantiation/
// MeshAssetPartGrouping.h) would ALSO compute for the SAME `parts` list -
// i.e. that the two independently-invoked de-duplication passes genuinely
// agree on group COUNT, cross-checking the shared extraction Phase 4
// introduced specifically so the two never drift into two independently-
// maintained copies of the same grouping logic (see AGENTS.md's own "Job
// System" section and GPU_SKINNING_PHASE4_COMPLETION_REPORT.md). Does NOT
// compare buffer contents (see ValidateGpuSkinningAgainstCpuOracle() above
// for that) - purely a structural sanity check.
GpuSkinningGroupingParityResult ValidateGpuSkinningGroupingParity(RenderSystem& renderSystem,
    const GpuSkinningRigCache::GpuModelEntry& gpuEntry, const std::vector<MeshAssetPart>& parts);

} // namespace gte
