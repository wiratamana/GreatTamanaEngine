# PHASE1_STL_MESH_LOADER_AND_PARSER.md

**Parent:** `PHASE0_MASTER_STRATEGY.md` — READ IT FIRST (Locked Design
Decisions 1, 3, 5, and the Risk Register entries tagged "PHASE1" all apply
directly to this document and are not repeated in full here).

## Step 1: The Goal

Add a brand-new, self-contained, engine-native STL reader —
`src/Assets/StlLoader.h`/`.cpp` — that parses either a binary or an ASCII
`.stl` file straight into this engine's existing, format-neutral `MeshData`
(`src/Assets/MeshData.h`), following the exact same shape/contract
`PmxLoader.h`'s `LoadPmxModel()` already established (`XxxLoadResult` struct:
`success` + payload + `message`, never throws, degrades to a clear failure
message rather than crashing on anything malformed). This module has **zero**
knowledge of `AssetImporter`/`AssetDatabase`/`*.gta` — it is a pure "read this
file format into this plain struct" leaf, exactly like `PmxLoader.h`/
`VmdLoader.h` are today. Wiring it into the actual import pipeline is
`PHASE2`'s job, not this one's.

## Step 2: The Situation

- `src/Assets/MeshData.h`'s `MeshData` struct already has everything this
  loader needs to produce: `positions`/`normals`/`uvs` (`std::vector<Vec3>`/
  `std::vector<Vec3>`/`std::vector<Vec2>`, all three always kept the SAME
  length — see `MeshFile.cpp`'s `EncodeMeshDataToBytes()`, which encodes one
  shared `vertexCount` for all three arrays) plus `indices`
  (`std::vector<std::uint32_t>`, independently sized) plus optional
  `skinWeights` (leave completely empty for STL — matches `MeshData::
  skinWeights`'s own documented "empty means no skinning info" contract).
- `src/Math/Vec3.h` already provides everything needed for normal handling:
  `Cross(a, b)`, `Normalize(v)` (returns `Vec3::Zero()` for a near-zero input
  rather than NaN), `LengthSquared(v)`, and the module-wide `kEpsilon`
  constant (`src/Math/MathTypes.h`) for degeneracy checks.
- `PmxLoader.h`/`.cpp` and `VmdLoader.h`/`.cpp` are this module's own direct
  precedent for file shape, doc-comment style, and failure-handling
  convention — read `src/Assets/PmxLoader.h` in full before starting; every
  convention it establishes (UTF-8 `std::string` path parameter, never
  throws, `success`+`message` always both populated, doc comment explaining
  exactly what is and isn't remapped) applies here too.
- The real fixture this phase is measured against:
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\_reference\pl-sky\assets\terrain.stl`
  — confirmed (via a direct header read during strategy research) to be a
  **binary** STL, 52,272,984 bytes, declaring exactly **1,045,458 triangles**
  at byte offset 80 (a little-endian `uint32_t`), and `84 + 1045458*50 ==
  52272984` exactly — i.e. its declared count and its real file size agree
  perfectly (a well-formed file). This folder (`_reference/`) is gitignored
and never committed (see `.gitignore`) — any test against this real
  file MUST skip cleanly (never fail the build) on a machine where it's
  absent, exactly like `tests/Assets/PmxLoaderTests.cpp`'s own
  `PmxLoaderRealModelSmokeTest` already does for its own real MMD fixture.

## Step 3: The Plan

### 3.1 — New file: `src/Assets/StlLoader.h`

```cpp
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
```

### 3.2 — New file: `src/Assets/StlLoader.cpp`

Implementation notes (write real code following these exactly — this is not
pseudocode, it is the literal algorithm to implement):

1. **Read the whole file into memory once**, as raw bytes
   (`std::ifstream(path, std::ios::binary)`, seek to end for size, seek back
   to 0, single `read()` call into a `std::vector<std::uint8_t>`) — do this
   the same way regardless of eventual binary/ASCII detection, since both
   detection AND parsing need the raw bytes/text anyway, and a single
   up-front read avoids two separate I/O passes over a 52MB file. Fail
   immediately (`success = false`, message mentioning the path) if the file
   can't be opened at all, exactly like `LoadPmxModel()`'s own missing-file
   failure path.

2. **Binary-format detection & parsing** (try this FIRST, per this file's
   own doc comment above):
   - Require at least 84 bytes total (80-byte header + 4-byte count) to even
     attempt this path; otherwise fall through to ASCII detection below.
   - Read the `uint32_t` triangle count via a raw `std::memcpy` out of bytes
     `[80, 84)` (little-endian — this engine's only target platform is
     little-endian x64 Windows, matching `MeshFile.cpp`'s own existing
     `AppendU32`/raw-`memcpy` convention; no explicit byte-swapping needed).
   - **Validate BEFORE allocating anything**: compute
     `expectedSize = 84u64 + static_cast<std::uint64_t>(triangleCount) * 50u64`
     using 64-bit arithmetic throughout (a hostile/corrupt 32-bit count times
     50 can itself overflow a 32-bit computation — this must never
     wrap/truncate silently). If `expectedSize != actualFileSizeInBytes`,
     this is NOT a valid binary STL by this engine's detection rule — fall
     through to the ASCII detection path below instead of failing outright
     (a file that fails the binary check might still be valid ASCII text
     that simply happens to be at least 84 bytes long).
   - Once validated, `mesh.positions`/`normals`/`uvs`.`reserve(triangleCount *
     3)` and `mesh.indices.reserve(triangleCount * 3)` up front (this is what
     keeps the real 1,045,458-triangle fixture importing efficiently — see
     `PHASE0_MASTER_STRATEGY.md`'s Risk Register).
   - Loop `triangleCount` times, reading each 50-byte record starting at
     offset `84 + i * 50`: 3 floats (normal), 3×3 floats (v0/v1/v2), 1
     `uint16_t` (attribute byte count — read and discard; never
     interpreted). Use `std::memcpy` per-float the same way
     `MeshFile.cpp`/`PmxLoaderTests.cpp`'s own byte-writer helpers already do
     elsewhere in this codebase, for exactly the same "correct on this
     engine's only target platform, no UB from misaligned reads" reasoning.
   - Before doing anything else with this triangle's 3 just-read vertex
     positions, check all 9 floats with `std::isfinite()`
     (`<cmath>`) - if ANY is NaN/Infinity, fail the whole `LoadStlModel()`
     call gracefully right here (`success = false`, a message mentioning a
     non-finite/corrupt vertex position) rather than importing a mesh that
     would silently carry a NaN/Infinity vertex forever after (see this
     file's own header doc comment's "Non-finite hardening" paragraph).
   - Per this file's own doc comment: if the stored normal is degenerate
     (`LengthSquared(storedNormal) <= kEpsilon * kEpsilon`, or equivalently
     just check `Normalize(storedNormal) == Vec3::Zero()` and treat that as
     "degenerate" — either formulation is acceptable, pick one and be
     consistent) OR non-finite (any of its 3 floats fails `std::isfinite()`
     — see this file's own header doc comment), recompute it as
     `Normalize(Cross(v1 - v0, v2 - v0))` instead. Push this SAME (possibly
     recomputed) normal 3 times (once per vertex of this triangle).
   - Push `v0`, `v1`, `v2` into `mesh.positions`; push `Vec2::Zero()` 3 times
     into `mesh.uvs`; push the next 3 sequential indices
     (`mesh.positions.size() - 3`, `- 2`, `- 1`, cast to `uint32_t`) into
     `mesh.indices`.
   - On successful completion of the loop: `result.success = true`,
     `result.wasBinaryFormat = true`, a `message` reporting the path plus
     vertex/triangle counts (mirror `LoadPmxModel()`'s own message shape:
     `"Loaded STL file: " + filePath + " (" + ... + " vertices, " + ... +
     " triangles, binary)"`).
   - A `triangleCount` of exactly `0` is a valid, well-formed binary STL
     (`expectedSize == 84` exactly, matching a real 84-byte file with no
     triangle records at all) and successfully parses to an empty
     `MeshData` — matching the same "empty is a well-formed state, not an
     error" convention called out for the ASCII path below; this is not
     treated as a failure either.

3. **ASCII-format detection & parsing** (only reached if the binary check
   above didn't match):
   - Interpret the already-read bytes as a `std::string` (or operate on the
     byte buffer directly with `char`-based comparisons — either is fine, be
     consistent). Trim leading ASCII whitespace, then case-insensitively
     check for a `"solid"` prefix. If it doesn't start with `"solid"`, this
     is not a recognizable STL at all in either variant — fail with a clear
     "unrecognized/corrupt STL file" message (this is exactly the case
     `AssetImporter.cpp`'s own `.stl` branch, PHASE2, falls back to a plain
     file copy for — the same "extension claimed a supported format but it
     didn't actually parse" degrade-gracefully contract every other importer
     branch already uses). An empty or whitespace-only file trims down to an
     empty string, which correctly does NOT start with `"solid"` and falls
     into this same failure path — no separate empty/whitespace-only-file
     special case is needed on the ASCII side (and the binary side's own
     "at least 84 bytes" check already rejects it first anyway).
   - A cheap up-front reserve-size estimate: count the number of
     non-overlapping occurrences of the literal substring `"facet"` in the
     raw text (e.g. a small loop repeatedly calling `std::string::find()`
     starting just past each previous match — NOT `std::count()`, which only
     counts single `char`s and cannot search for a multi-character substring
     directly) to get `estimatedTriangleCount`, then `reserve()` every output
     vector to `estimatedTriangleCount * 3` before the real tokenizing pass
     (see `PHASE0_MASTER_STRATEGY.md`'s Risk Register). This does not need to
     be exact, just a reasonable upper bound to avoid vector-growth churn.
   - Tokenize whitespace-delimited words (a simple manual scan/
     `std::istringstream >> token` loop is sufficient — no external library
     needed). Keyword comparisons (`"solid"`/`"facet"`/`"normal"`/
     `"outer"`/`"loop"`/`"vertex"`/`"endloop"`/`"endfacet"`/`"endsolid"`) must
     ALL be case-insensitive, the same as the initial `"solid"` prefix check
     above, not just that first one — some real-world exporters emit
     upper-case or mixed-case keywords, and there is no reason for this
     engine's tolerance to stop after only the very first token. Walk tokens
     looking for:
     - `"facet"` followed by `"normal"` followed by 3 floats → this
       triangle's stored normal (subject to the exact same degenerate-OR-
       non-finite-normal recompute rule as the binary path above, applied
       once this triangle's 3 vertices are known and confirmed finite).
     - `"vertex"` followed by 3 floats, expected EXACTLY 3 times per facet
       (between `"outer"`/`"loop"` and `"endloop"` — these bracketing
       keywords can simply be skipped/ignored; they carry no data of their
       own). A 4th (or later) `"vertex"` encountered before `"endfacet"` is
       just as malformed as too few — fail gracefully in that case too (see
       the new `FailsGracefullyOnAnAsciiFacetWithTooManyVertexLines` test
       below), never silently keep only the first/last 3.
     - `"endfacet"` → this triangle is complete: push its 3 positions, its
       (possibly-recomputed) normal ×3, 3 zero UVs, and 3 sequential
       indices, exactly like the binary path.
     - `"endsolid"` (or simple end-of-input) → stop parsing entirely, even if
       more bytes remain afterward. A well-formed multi-solid ASCII STL file
       (multiple concatenated `solid ... endsolid` blocks — valid per the
       format, though rare in practice) therefore only has its FIRST solid's
       triangles imported; everything from the first `"endsolid"` onward is
       simply ignored. This is an intentional scope decision (this engine's
       `MeshData` shape has no concept of "multiple named sub-objects" in
       one import — see `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision
       1), not an oversight, and must NOT be treated as a failure.
   - Every numeric token (a stored normal's or a vertex's x/y/z) MUST be
     parsed with a routine that (a) accepts both ordinary decimal notation
     (`1.5`, `-0.25`) AND scientific notation (`1.23e-05`, `-2.3E+00`) —
     both are completely valid in a real-world ASCII STL and neither needs
     any special-case code, since every standard C/C++ floating-point
     parser (`std::strtof`/`std::strtod`, `std::stof`, `std::istringstream`'s
     own `operator>>`) already accepts both forms identically — and (b) can
     NEVER throw or otherwise abort parsing, to preserve this whole
     function's documented "never throws" contract. Concretely: either use
     `std::istringstream >> token` (which never throws; it just sets the
     stream's failbit on bad input) or wrap `std::stof`/`std::strtof` so a
     parse failure (a thrown exception, `errno == ERANGE`, or an `endptr`
     that didn't advance) is treated as "this token is not a valid float"
     rather than propagating. Either way, a token that was expected to be a
     float but isn't a parseable number at all (e.g. a `"vertex"` line
     containing non-numeric text) must fail this whole `LoadStlModel()` call
     gracefully (`success = false`, descriptive message) exactly like any
     other malformed ASCII content — never let an uncaught exception or an
     unread/garbage value escape. A token that DOES parse successfully as a
     float but yields a non-finite value (`std::isfinite()` false - e.g. the
     literal text "nan"/"inf"/"-inf", which `std::strtof`/`std::stof`/
     `istringstream::operator>>` all happily parse into a real NaN/Infinity
     `float` rather than failing) is likewise treated as "not a valid token"
     for this engine's purposes and must fail `LoadStlModel()` gracefully the
     same way — see this file's own header doc comment's "Non-finite
     hardening" paragraph for why silently accepting one would be wrong.
   - Malformed/truncated ASCII content (e.g. a `"facet"` with fewer OR more
     than 3 `"vertex"` entries before `"endfacet"`/end-of-file, a non-numeric
     token where a float was expected, or a `"vertex"`/`"facet normal"` line
     with fewer than 3 numbers before the next keyword) must fail gracefully
     (`success = false`, descriptive message), never read past the end of
     the token stream or produce a partially-formed triangle.
   - A well-formed `"solid <name>\nendsolid <name>\n"` with zero `"facet"`
     blocks in between is a VALID, successful parse producing an empty
     `MeshData` (`result.success = true`, all vectors empty) — matching
     `MeshFile.cpp`'s own "an empty `MeshData` is a well-formed state, not
     an error" convention (see Step 2 above); this is not a failure case.
   - On success: `result.success = true`, `result.wasBinaryFormat = false`,
     an analogous message noting "(... vertices, ... triangles, ASCII)".

4. **Top-level `LoadStlModel()` orchestration**: open file → read all bytes
   → attempt binary detection/parse → if that path was not taken (didn't
   satisfy the size formula), attempt ASCII detection/parse → if NEITHER
   matched, return a clear failure. Every intermediate failure path returns
   promptly with `success = false` and a non-empty `message`; `mesh` is left
   at its default-constructed (empty) state on any failure, matching
   `PmxLoadResult`'s own established failure contract exactly.

### 3.3 — Register the new files

Add to the root `CMakeLists.txt`'s main source list, immediately next to the
other `src/Assets/*Loader.*` entries (alongside `src/Assets/PmxLoader.h`/
`.cpp`, `src/Assets/VmdLoader.h`/`.cpp` — see that file's existing ordering
around line 335-338):

```
src/Assets/StlLoader.h
src/Assets/StlLoader.cpp
```

### 3.4 — Tests: new file `tests/Assets/StlLoaderTests.cpp`

Follow `tests/Assets/PmxLoaderTests.cpp`'s exact structure/conventions (a
`::testing::Test`-derived fixture with a per-test temp directory created in
`SetUp()`/removed in `TearDown()`, a `WriteBinaryFile()` helper, hand-built
byte-precise fixtures built via small local `PmxByteWriter`-style helper
functions — write an equivalent small `StlByteWriter` helper class local to
this test file, with `U8`/`U16`/`U32`/`F32`/`Vec3F` methods mirroring
`PmxByteWriter`'s own shape from `PmxLoaderTests.cpp`).

Required test cases (each a `TEST_F(StlLoaderTest, ...)` unless noted):

- `ParsesAMinimalOneTriangleBinaryStl` — hand-build an 84 + 50 = 134-byte
  binary STL (1 triangle, a non-degenerate stored normal, 3 distinct
  vertices). Assert `success`, `wasBinaryFormat == true`,
  `mesh.positions.size() == 3`, `mesh.normals.size() == 3` (all 3 equal to
  the file's own stored normal), `mesh.uvs.size() == 3` (all `Vec2::Zero()`),
  `mesh.indices == {0, 1, 2}`.
- `RecomputesADegenerateStoredNormalFromVertexWinding` — same fixture shape
  as above but with the stored normal written as `(0, 0, 0)`. Assert success
  and that the resulting normal equals `Normalize(Cross(v1 - v0, v2 - v0))`
  computed by the test itself from the same 3 vertices (use
  `EXPECT_EQ`/`ApproximatelyEqual` as appropriate for float comparison,
  matching `Vec3Tests.cpp`'s own established float-comparison convention).
- `ParsesMultipleTrianglesWithNonSharedVertices` — a 2-triangle binary
  fixture sharing an edge (2 vertices' positions repeated across both
  triangles). Assert `mesh.positions.size() == 6` (NOT welded down to 4) and
  `mesh.indices == {0, 1, 2, 3, 4, 5}` — this is the direct regression test
  for Locked Design Decision 1.
- `ParsesAWellFormedEmptyBinaryStlWithZeroTriangles` — a binary fixture
  whose header declares a triangle count of exactly `0` (and whose real
  file size is therefore exactly the 84-byte header, satisfying `84 + 0*50
  == 84`). Assert `success == true` and `mesh.positions`/`mesh.normals`/
  `mesh.uvs`/`mesh.indices` all empty — an empty parse is a valid,
  well-formed result, not a failure (matches `MeshFile.cpp`'s own "an empty
  MeshData is a well-formed state" convention).
- `FailsGracefullyWhenBinaryTriangleCountDoesNotMatchFileSize` — write a
  binary-shaped file whose 80-84 byte count field claims (say) 1000
  triangles but whose actual file is only long enough for 1 real triangle
  (134 bytes total). Assert `success == false`, non-empty `message`, `mesh`
  left empty — the direct regression test for this phase's own hardening
  requirement (see `PHASE0_MASTER_STRATEGY.md`'s Risk Register).
- `FailsGracefullyWhenBinaryTriangleCountWouldOverflow` — write a count field
  of `0xFFFFFFFF` (paired with a small real file). Assert graceful failure
  (no crash, no attempted huge allocation) — the direct regression test for
  the 64-bit-arithmetic requirement in Step 3.2.
- `RecomputesANonFiniteStoredBinaryNormalFromVertexWinding` — same fixture
  shape as `ParsesAMinimalOneTriangleBinaryStl` but with the stored normal
  written as `(NaN, 0, 0)` (or `+Infinity`/`-Infinity` in any component).
  Assert success and that the resulting normal equals
  `Normalize(Cross(v1 - v0, v2 - v0))`, exactly like
  `RecomputesADegenerateStoredNormalFromVertexWinding` — the direct
  regression test for this file's own header doc comment's "Non-finite
  hardening" paragraph on the BINARY path.
- `FailsGracefullyOnABinaryFileWithANonFiniteVertexPosition` — a
  1-triangle binary fixture where one vertex's x/y/z contains a NaN or
  Infinity float (a bit pattern e.g. `0x7FC00000` for NaN). Assert
  `success == false`, non-empty `message`, `mesh` left empty — never a
  silently-imported NaN vertex.
- `ParsesAMinimalOneTriangleAsciiStl` — hand-build a plain-text
  `"solid test\n facet normal 0 0 1\n outer loop\n vertex 0 0 0\n vertex 1 0
  0\n vertex 0 1 0\n endloop\n endfacet\n endsolid test\n"`-shaped string.
  Assert `success`, `wasBinaryFormat == false`, same position/normal/uv/index
  assertions as the binary minimal test.
- `AsciiParsingToleratesExtraWhitespaceAndMixedLineEndings` — same content as
  above but with extra blank lines/leading spaces/`\r\n` line endings mixed
  in. Assert identical successful parse.
- `FailsGracefullyOnATruncatedAsciiFacet` — an ASCII fixture where a
  `"facet"` block is cut off after only 2 `"vertex"` lines (missing the
  3rd + `endfacet`). Assert graceful failure.
- `FailsGracefullyOnAnAsciiFacetWithTooManyVertexLines` — an ASCII fixture
  where one `"facet"` block contains 4 `"vertex"` lines before its
  `"endfacet"` (one too many). Assert graceful failure (never silently keep
  only the first/last 3 vertices).
- `FailsGracefullyOnAsciiFileWithANonNumericFloatToken` — an ASCII fixture
  where one `"vertex"` line's numbers are replaced with non-numeric text
  (e.g. `"vertex abc def ghi"`). Assert graceful failure — no thrown
  exception ever escapes `LoadStlModel()`, matching its documented "never
  throws" contract.
- `FailsGracefullyOnAsciiFileWithANonFiniteFloatToken` — an ASCII fixture
  where one `"vertex"` line's numbers are replaced with the literal text
  `"nan"` (or `"inf"`/`"-inf"`) instead of an ordinary number — a token that
  a plain `std::isfinite()`-unaware float parser would happily accept.
  Assert graceful failure — the direct regression test for this file's own
  header doc comment's "Non-finite hardening" paragraph on the ASCII path.
- `ParsesAsciiFloatsWrittenInScientificNotation` — an ASCII fixture whose
  `"facet normal"`/`"vertex"` lines use scientific-notation floats (e.g.
  `1.5e-01`, `-2.3E+00`, `1.23e-05`) instead of plain decimal notation.
  Assert success and that the parsed positions/normal exactly match the
  equivalent plain-decimal values.
- `IgnoresContentAfterTheFirstEndsolidInAMultiSolidAsciiFile` — an ASCII
  fixture with two back-to-back, well-formed `"solid ... endsolid"` blocks
  (a valid, if rare, multi-solid ASCII STL). Assert success and that only
  the FIRST solid's triangle(s) were imported (the second solid's own
  distinct vertex is NOT present anywhere in `mesh.positions`) — the direct
  regression test for this phase's "only the first solid is imported"
  scope decision.
- `ParsesAWellFormedEmptyAsciiStlWithZeroFacets` — a `"solid test\nendsolid
  test\n"` fixture with no `"facet"` blocks at all. Assert `success == true`
  and `mesh.positions`/`mesh.normals`/`mesh.uvs`/`mesh.indices` all empty —
  an empty parse is a valid, well-formed result, not a failure.
- `FailsGracefullyWhenFileDoesNotExist` — mirrors `PmxLoaderTest.
  FailsGracefullyWhenFileDoesNotExist` exactly.
- `FailsGracefullyOnEmptyFile` — a zero-byte file. Assert graceful failure
  (not a crash from indexing into an empty byte buffer).
- `FailsGracefullyOnUnrecognizedContent` — a small file containing plain
  arbitrary text that starts with neither the binary size formula nor
  `"solid"` (e.g. `"this is not an stl file at all"`). Assert graceful
  failure.

Additionally, ONE optional real-world smoke test, following
`PmxLoaderTests.cpp`'s own `PmxLoaderRealModelSmokeTest` pattern EXACTLY
(a plain `TEST(...)`, not `TEST_F`, checking the fixed path directly and
`GTEST_SKIP()`-ing cleanly if it's absent — never a hard failure on a machine
without it):

```cpp
TEST(StlLoaderRealModelSmokeTest, LoadsTheRealTerrainStlIfPresentOnThisMachine)
{
    const std::filesystem::path path =
        "C:\\Users\\F5954\\Documents\\TAMANA\\GreatTamanaEngine\\_reference\\pl-sky\\assets\\terrain.stl";

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        GTEST_SKIP() << "Real terrain.stl reference fixture not present on this machine - skipping.";
    }

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.wasBinaryFormat);
    EXPECT_EQ(result.mesh.positions.size(), 1045458u * 3u);
    EXPECT_EQ(result.mesh.normals.size(), result.mesh.positions.size());
    EXPECT_EQ(result.mesh.uvs.size(), result.mesh.positions.size());
    EXPECT_EQ(result.mesh.indices.size(), result.mesh.positions.size());
    EXPECT_TRUE(result.mesh.skinWeights.empty());

    std::cout << "[StlLoaderRealModelSmokeTest] " << path.string() << ": "
              << result.mesh.positions.size() << " vertices, "
              << (result.mesh.indices.size() / 3) << " triangles\n";
}
```

### 3.5 — Register the new test file

Add `Assets/StlLoaderTests.cpp` to `tests/CMakeLists.txt`'s
`GTE_TEST_SOURCES` list, immediately next to `Assets/PmxLoaderTests.cpp` /
`Assets/VmdLoaderTests.cpp` (see that file's existing ordering around line
1774-1776). Also add a short one-line description of the new test file to
that same `CMakeLists.txt`'s own leading comment block (the "Tier 1" test
taxonomy list near the top of the file), matching the style every other
entry there already uses.

### 3.6 — Build & verify

- `cmake --build build` (working directory: the repo root) must succeed with
  no new warnings introduced by this phase's own new files.
- Build and run `GreatTamanaEngineTests` (e.g. `ctest -C Debug
  --output-on-failure` from the `build` directory) — every new
  `StlLoaderTests.cpp` case must pass, and the real-file smoke test must
  either pass (if `_reference/pl-sky/assets/terrain.stl` is present on this
  machine) or report as skipped (never as a failure).
- Write a short completion report (`PHASE1_COMPLETION_REPORT.md`, this same
  folder) summarizing what was added, confirming the build/test results
  above, and noting the real-file smoke test's actual outcome on this
  machine (ran-and-passed vs. skipped-because-absent).
- `git add` + `git commit` the new/changed files together with the report.
