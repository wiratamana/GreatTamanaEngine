#pragma once

#include "MeshData.h"

namespace gte {

// Recomputes EVERY entry of `mesh.normals` from `mesh.positions` +
// `mesh.indices` alone, completely discarding whatever normals were
// previously stored - an area-weighted per-vertex accumulation (accumulate
// each triangle's own RAW, un-normalized face cross product into all 3 of
// its own vertex slots, then normalize once at the end). This is the one
// standard algorithm that is simultaneously correct for a NON-SHARED mesh
// (e.g. an imported STL - see src/Assets/StlLoader.h - where it reduces to
// exactly one flat per-face normal, since every vertex belongs to exactly
// one triangle) and a SHARED-vertex mesh (e.g. an imported PMX model - see
// src/Assets/PmxLoader.h - where it naturally produces smooth per-vertex
// normals, since a shared vertex accumulates every triangle that touches
// it) - the same technique real DCC tools use (Blender's "Recalculate
// Normals", Unity's mesh-import "Normals: Calculate"). See
// task_manager/stl-parser-1/PHASE3_GENERIC_MESH_NORMAL_RECOMPUTE_AND_INSPECTOR_ACTION.md
// for the full algorithm write-up and why this must never be a naive
// "flat-only" recompute.
//
// A vertex touched by zero triangles (or whose only triangle(s) are
// degenerate/zero-area) ends up with Vec3::Zero() - matches
// Normalize()'s own existing "never produce NaN/Inf" safety contract
// (src/Math/Vec3.h) - this is a defensible, honest "no well-defined normal"
// result, not a crash or garbage value.
//
// `mesh.normals` is resized to exactly `mesh.positions.size()` if it wasn't
// already that length (defensive; every real caller already keeps them in
// sync). `mesh.indices.size()` not being a multiple of 3 is tolerated by
// simply ignoring any trailing 1-2 leftover indices (never reads out of
// bounds). Never throws. Pure - touches nothing outside `mesh` itself, no
// file/GPU/ECS access whatsoever, fully Tier-1-testable (see
// tests/Assets/MeshNormalRecomputeTests.cpp).
void RecomputeMeshNormalsFromGeometry(MeshData& mesh);

} // namespace gte
