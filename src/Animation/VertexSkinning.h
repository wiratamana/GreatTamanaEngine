#pragma once

#include "../Assets/MeshData.h"
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"

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
// tests/Animation/VertexSkinningTests.cpp).
void SkinVertices(const std::vector<Vec3>& bindPositions, const std::vector<Vec3>& bindNormals,
    const std::vector<VertexSkinWeights>& skinWeights, const std::vector<Mat4>& skinningMatrices,
    std::vector<Vec3>& outPositions, std::vector<Vec3>& outNormals);

} // namespace gte
