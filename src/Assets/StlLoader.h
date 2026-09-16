#pragma once

#include "MeshData.h"

#include <string>

namespace gte {

// Result of one LoadStlModel() call below - mirrors PmxLoadResult's own
// "always fully populated, success or failure" convention (see
// src/Assets/PmxLoader.h).
struct StlLoadResult {
    bool success = false;

    // Positions/normals/(zero-filled) UVs/triangle indices - see
    // LoadStlModel()'s own doc comment below for exactly how these are
    // populated. mesh.skinWeights is always left empty (STL carries no
    // skinning concept whatsoever) - matches MeshData::skinWeights' own
    // documented "empty means no skinning info" contract.
    MeshData mesh;

    // True if the source file was detected and parsed as the binary STL
    // variant, false for the ASCII variant - purely informational (e.g. for
    // a caller that wants to report it), never meaningful when success is
    // false.
    bool wasBinaryFormat = false;

    std::string message; // Human-readable status - always set, success or failure.
};

// Parses an STL (STereoLithography) 3D model file at `filePath` (a plain
// filesystem path, UTF-8 encoded - matches LoadPmxModel()'s own parameter
// contract) into a MeshData - this engine's own, format-neutral mesh shape
// (src/Assets/MeshData.h), never a format-specific intermediate type.
//
// Auto-detects binary vs. ASCII: a file whose actual on-disk size exactly
// matches the binary layout's own size formula (84-byte header + declared
// triangle count * 50 bytes each - see this .cpp's own doc comment for the
// exact layout) is parsed as binary; otherwise, if its content is valid text
// that starts with "solid" (after trimming leading whitespace, case-
// insensitive), it is parsed as ASCII. Deliberately checks the binary-size
// formula FIRST, even when the file's own 80-byte header text happens to
// start with the word "solid" - a well-known STL-format ambiguity: many
// real-world binary STL files (this is explicitly allowed by the format)
// still write a human-readable comment starting with "solid" into their
// header for tooling/documentation purposes, despite being genuinely binary
// underneath. Trusting the more mechanically-verifiable size-formula match
// over the human-readable text heuristic is what keeps such a file from
// being mis-parsed as (garbage) ASCII text.
//
// Every triangle's 3 vertices become 3 BRAND-NEW, never-shared MeshData
// entries (never welded/deduplicated by position) - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision 1 for why. `indices`
// is therefore always the trivial identity sequence (0, 1, 2, 3, ...),
// 3 per triangle, never reused.
//
// Per-vertex normals: this engine TRUSTS the file's own stored per-facet
// normal by default (matches Unity's own default mesh-import behavior - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision 3) - all 3 of a
// triangle's vertices get that SAME stored normal. The one exception: if the
// file's stored normal is degenerate (near-zero length, i.e. Normalize()
// would otherwise yield Vec3::Zero() - see src/Math/Vec3.h), it is silently
// recomputed from the triangle's own 3 vertex positions instead
// (Normalize(Cross(v1 - v0, v2 - v0))), so an imported mesh is never left
// with a literal (0,0,0) normal purely because the source tool wrote one.
// This one-time, degenerate-only recompute is entirely independent of - and
// far narrower in scope than - the separate, ALWAYS-available, user-
// triggered "Recompute Normals from Geometry" Inspector action added in
// PHASE3, which recomputes every normal unconditionally on demand.
//
// Non-finite hardening: a stored NORMAL whose x/y/z is NaN or +/-Infinity
// (e.g. from a corrupt/hostile binary file's arbitrary bit pattern, or a
// textual "nan"/"inf"/"-inf" token in an ASCII file) is treated exactly like
// a degenerate (near-zero) normal above - i.e. silently recomputed from the
// triangle's own vertex positions instead - since Vec3::LengthSquared() of a
// NaN/Infinity vector is itself NaN, and `NaN <= kEpsilon * kEpsilon` is
// always false, a plain "is it near-zero" check alone would let a NaN normal
// straight through uncaught; this engine's own "never propagate NaN/Inf"
// convention (see Vec3::Normalize()'s own safe-normalize contract) is
// deliberately extended to cover this too. A stored POSITION that is itself
// NaN/Infinity has no such fallback (there is no sensible geometry to
// recompute a position FROM) - this fails the whole LoadStlModel() call
// gracefully (`success = false`, descriptive message), exactly like any
// other malformed/corrupt input, rather than silently importing a mesh with
// a NaN vertex that would then corrupt this mesh's own bounding-sphere
// computation (see AssetPreviewMesh.cpp) or its GPU vertex buffer.
//
// UVs: STL carries no texture-coordinate concept at all - mesh.uvs is
// always populated with Vec2::Zero() for every vertex (never left a
// different length than mesh.positions/mesh.normals - MeshFile.h's
// EncodeMeshDataToBytes() assumes all three arrays share one vertexCount).
//
// No axis/winding/scale remapping of any kind is performed - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision 5 (mirrors
// PmxLoader.h's own precedent exactly).
//
// Never throws. Returns success == false with a descriptive message and an
// otherwise-default-constructed (empty) result for: a missing/unreadable
// file; a file too short to even contain a valid 80-byte header; a binary
// file whose declared triangle count does NOT actually match its own real
// file size (see this .cpp's own doc comment - this is the hardening this
// engine's own reference terrain.stl fixture's real 1,045,458-triangle,
// 52,272,984-byte size specifically exercises, and is what protects this
// engine from ever attempting to pre-allocate memory for a triangle count a
// corrupt/hostile file merely CLAIMS to have); or a file that is neither a
// recognizable binary STL by size formula NOR ASCII text starting with
// "solid".
StlLoadResult LoadStlModel(const std::string& filePath);

} // namespace gte
