#pragma once

#include "../Assets/MeshData.h"
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"

#include <cstdint>
#include <vector>

namespace gte {

// Applies CPU vertex skinning: for every vertex i, blends
// `skinningMatrices` (see SkeletonPose.h's ComputeSkinningMatrices(),
// index-aligned with SkeletonData::bones) according to `skinWeights[i]`
// (see MeshData.h's own VertexWeightType doc comment for exactly which of
// its 4 index/weight slots are meaningful per weight type - this function
// itself is generic across all of them: an unused slot is always
// boneIndex == -1 / weight == 0.0, contributing nothing regardless of
// type), producing bone-deformed positions/normals into
// `outPositions`/`outNormals` (resized to match `bindPositions`).
//
// Positions are blended as full points (translation matters); normals are
// blended via TransformVector() (translation ignored) rather than a proper
// inverse-transpose normal transform - correct as long as every skinning
// matrix is a rigid transform (rotation + translation, no scale/shear),
// which holds here since PMX bones carry no scale of their own (see
// SkeletonPose.h). The blended normal is re-normalized at the end (a
// weighted sum of unit vectors is not itself unit length in general).
//
// SDEF's own extra correction terms (VertexSkinWeights::sdefC/sdefR0/
// sdefR1) and QDEF's dual-quaternion blending are deliberately NOT applied
// here - both weight types are treated exactly like BDEF2/BDEF4
// respectively (same index/weight layout - see VertexWeightType's own doc
// comment for why that's an explicitly sanctioned simplification) - a
// visually reasonable approximation, not a bug.
//
// Pure CPU-side math, no GPU/Renderer dependency - Tier-1-testable (see
// tests/Animation/VertexSkinningTests.cpp). Implemented purely in terms of
// SkinVertexRange() below (0, bindPositions.size()) - there is exactly one
// copy of the actual per-vertex blending logic.
void SkinVertices(const std::vector<Vec3>& bindPositions, const std::vector<Vec3>& bindNormals,
    const std::vector<VertexSkinWeights>& skinWeights, const std::vector<Mat4>& skinningMatrices,
    std::vector<Vec3>& outPositions, std::vector<Vec3>& outNormals);

// Job System Phase 6 (First Production Consumer - Animation / Vertex
// Skinning - see AGENTS.md, "Job System", and
// task_manager/job_system/JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md): the same
// per-vertex blending SkinVertices() performs above, restricted to a single
// half-open [beginIndex, endIndex) subrange of vertices - this is what lets
// AnimationSystem::Update() (src/Game/Animation/AnimationSystem.cpp) split
// one model's own vertex array into several DISJOINT batches and skin them
// in parallel via gte::Jobs::Dispatch(), each batch writing into its own
// non-overlapping slice of the SAME `outPositions`/`outNormals` vectors.
//
// Unlike SkinVertices() above, this function NEVER resizes `outPositions`/
// `outNormals` - the caller must size them to `bindPositions.size()` (or
// larger) BEFORE dispatching any batches, since two batches writing into
// two different [begin, end) slices of the same vectors must never race a
// third, hidden reallocation triggered by one of them calling resize().
// `endIndex` is clamped internally to `bindPositions.size()` as a defensive
// safety net - never reads/writes out of bounds even if a caller passes a
// bad range.
//
// Pure CPU-side math, no GPU/Renderer/Jobs dependency of its own (nothing
// under src/Jobs/ is included here - only AnimationSystem.cpp, the actual
// job-body trampoline's owner, knows about the Job System) - Tier-1-
// testable exactly like SkinVertices() itself (see
// tests/Animation/VertexSkinningParityTests.cpp, which proves this
// function - called serially across the WHOLE range, or in several
// concurrent batches via a real gte::Jobs::Dispatch() call - always
// produces results identical to SkinVertices()).
void SkinVertexRange(std::uint32_t beginIndex, std::uint32_t endIndex, const std::vector<Vec3>& bindPositions,
    const std::vector<Vec3>& bindNormals, const std::vector<VertexSkinWeights>& skinWeights,
    const std::vector<Mat4>& skinningMatrices, std::vector<Vec3>& outPositions, std::vector<Vec3>& outNormals);

} // namespace gte
