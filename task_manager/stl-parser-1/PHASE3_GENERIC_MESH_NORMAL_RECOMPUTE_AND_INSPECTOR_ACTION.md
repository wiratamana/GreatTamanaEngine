# PHASE3_GENERIC_MESH_NORMAL_RECOMPUTE_AND_INSPECTOR_ACTION.md

**Parent:** `PHASE0_MASTER_STRATEGY.md` — READ IT FIRST (Locked Design
Decision 3, and the two Risk Register entries tagged "PHASE3", apply
directly to this document).
**Depends on:** `PHASE2_ASSET_IMPORTER_STL_GATING_AND_MESH_SOURCE_FORMAT.md`
should already be implemented and merged (so a real STL-derived Mesh `*.gta`
exists to manually verify this feature against), though the code in this
phase itself has no hard compile-time dependency on `PHASE2` — it operates
purely on the pre-existing, format-neutral `MeshData`/`*.gta` shape.

## Step 1: The Goal

Ship a generic (explicitly NOT STL-specific — it must work correctly on a
PMX-derived mesh too), on-demand "recompute every normal from this mesh's own
current vertex geometry" capability: a pure, Tier-1-testable math function,
a thin persistence wrapper that rewrites an existing Mesh `*.gta`'s payload
bytes in place, and an Editor Inspector button that lets a user trigger it
for whichever Mesh asset is currently selected — directly satisfying the
product decision recorded in `PHASE0_MASTER_STRATEGY.md`'s Locked Design
Decision 3 ("trust the file's normals by default, but let the user recompute
from actual vertex data on demand").

## Step 2: The Situation

- `src/Assets/MeshData.h`'s `MeshData` is the one shape this operates on —
  already fully in-memory-decoded by `MeshFile.h`'s
  `DecodeMeshDataFromBytes()`/re-encoded by `EncodeMeshDataToBytes()`. No new
  on-disk format is introduced by this phase; only the existing PAYLOAD bytes
  of an existing Mesh `*.gta` are ever rewritten (never its GUID, flags,
  version, or METADATA/`RigFileData` section).
- **The recompute algorithm must be correct for BOTH mesh shapes this engine
  can already produce**: `PHASE1`'s STL import (every vertex belongs to
  EXACTLY one triangle — non-shared, per Locked Design Decision 1) and an
  existing PMX import (many vertices are SHARED across multiple triangles,
  and rely on smooth per-vertex normal averaging for correct-looking
  skinned/organic shading). A naive "just recompute one flat face normal
  and blindly overwrite every vertex touched by that triangle" approach
  would silently corrupt a PMX model's shading the first time a user ever
  clicks this button on one (see the master strategy's Risk Register). The
  standard, correct algorithm that handles both cases identically with no
  branching is **area-weighted per-vertex accumulation**:
  1. Zero-initialize one `Vec3` accumulator per vertex (`positions.size()`
     of them).
  2. For every triangle `(i0, i1, i2)` in `indices` (walked 3 at a time):
     compute the RAW (NOT normalized) face vector
     `faceVector = Cross(positions[i1] - positions[i0], positions[i2] -
     positions[i0])` — deliberately left un-normalized, so a larger triangle
     naturally contributes proportionally more "weight" to its corners' 3
     accumulators (a standard, well-known area-weighting trick: the
     magnitude of an un-normalized cross product is exactly twice the
     triangle's own area). Add this SAME `faceVector` into all 3 of
     `accumulators[i0]`, `accumulators[i1]`, `accumulators[i2]`.
  3. After every triangle has been processed, `mesh.normals[v] =
     Normalize(accumulators[v])` for every vertex `v` (falls back to
     `Vec3::Zero()` for a vertex touched by zero triangles, or whose only
     triangle(s) are degenerate/zero-area — matches `Normalize()`'s own
     existing "never NaN" safety contract, `src/Math/Vec3.h`).
  - For a non-shared STL-style mesh, this reduces EXACTLY to one flat normal
    per triangle (each vertex has exactly one contributor) — for a
    shared-vertex PMX-style mesh, this naturally produces smooth per-vertex
    normals (each shared vertex blends every triangle touching it) — this is
    the exact same algorithm real DCC tools use (Blender's own "Recalculate
    Normals (Outside)", Unity's mesh-import "Normals: Calculate" option), so
    there is nothing STL-specific baked into it at all.
- `src/Game/Physics/DynamicChainPhysicsPersistence.h`/`.cpp` is this phase's
  direct structural precedent for the persistence wrapper (read it in full
  before starting) — same shape: read the existing `*.gta`, decode, mutate
  ONLY the one piece this operation owns, re-encode, write back preserving
  everything else, `bool` return + optional `std::string* outErrorMessage`,
  never fabricates a fresh/empty structure on a decode failure (refuses and
  reports an error instead).
- `src/Editor/Panels/InspectorPanel.cpp`'s existing "Save Joint Physics to
  Asset" button (inside the `DynamicChainRig`-gated `if` block, roughly lines
  679-727) is this phase's direct UI precedent — same shape: a button, two
  narrowly-scoped `static` locals tracking the last attempt's
  success/error-message for a persistent status line, a `TextDisabled()`
  one-line explanation of what the button does.
- `src/Game/Instantiation/MeshAssetGpuCatalog.h`/`.cpp`'s
  `RefreshCachedJointPhysicsOverridesFromDisk()` (+ `MeshInstantiationSystem.h`'s
  one-line forwarding wrapper of the same name) is this phase's precedent
  for cache invalidation — but note the important DIFFERENCE explained in
  Step 3.3 below: that existing method patches a cached value IN PLACE
  (because `jointPhysicsOverrides` is read fresh by `PhysicsSystem` at
  attach-time, not baked into a GPU buffer); this phase's own new method
  must instead ERASE the cache entry outright, because normals are baked
  directly into the GPU vertex buffer (`MeshVertex`) at upload time.

## Step 3: The Plan

### 3.1 — New file: `src/Assets/MeshNormalRecompute.h`

```cpp
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
```

### 3.2 — New file: `src/Assets/MeshNormalRecompute.cpp`

Implements the algorithm from Step 2 above exactly:

```cpp
#include "MeshNormalRecompute.h"

#include "../Math/Vec3.h"

namespace gte {

void RecomputeMeshNormalsFromGeometry(MeshData& mesh)
{
    if (mesh.normals.size() != mesh.positions.size()) {
        mesh.normals.resize(mesh.positions.size());
    }

    std::vector<Vec3> accumulators(mesh.positions.size(), Vec3::Zero());

    const std::size_t triangleAlignedCount = (mesh.indices.size() / 3) * 3;
    for (std::size_t i = 0; i < triangleAlignedCount; i += 3) {
        const std::uint32_t i0 = mesh.indices[i];
        const std::uint32_t i1 = mesh.indices[i + 1];
        const std::uint32_t i2 = mesh.indices[i + 2];
        if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() || i2 >= mesh.positions.size()) {
            continue; // Defensive: never index out of bounds on a corrupt/mismatched index buffer.
        }

        const Vec3 faceVector = Cross(mesh.positions[i1] - mesh.positions[i0], mesh.positions[i2] - mesh.positions[i0]);
        accumulators[i0] += faceVector;
        accumulators[i1] += faceVector;
        accumulators[i2] += faceVector;
    }

    for (std::size_t v = 0; v < mesh.normals.size(); ++v) {
        mesh.normals[v] = Normalize(accumulators[v]);
    }
}

} // namespace gte
```

(`#include <vector>`/`<cstdint>` as needed per this codebase's existing
include-what-you-use convention — check the exact includes already used by
a sibling file like `MeshFile.cpp` for the house style.)

### 3.3 — New file: `src/Assets/MeshNormalRecomputePersistence.h`

```cpp
#pragma once

#include <string>

namespace gte {

// Reads the EXISTING Mesh *.gta at `absoluteGtaPath`, decodes its current
// MeshData payload, overwrites its normals via
// RecomputeMeshNormalsFromGeometry() (MeshNormalRecompute.h), re-encodes,
// and writes the file back out - GUID, flags, version, and the METADATA
// section (RigFileData - skeleton/morphs/physics/materials/skin weights/
// jointPhysicsOverrides) are all preserved byte-for-byte/value-for-value
// unchanged; ONLY the payload's normals actually change. Mirrors
// src/Game/Physics/DynamicChainPhysicsPersistence.h's own
// SaveJointPhysicsOverridesToGtaFile() shape and conservative-on-failure
// contract exactly (see that header's own doc comment) - returns false (and
// writes NOTHING to disk) if the file can't be read, isn't an
// AssetType::Mesh *.gta, or its existing payload can't be decoded as a
// MeshData.
//
// Deliberately does NOT touch any GPU/MeshAssetGpuCatalog cache itself -
// see src/Game/Instantiation/MeshAssetGpuCatalog.h's
// InvalidateCachedMeshAsset() (a separate, explicit follow-up call an
// Editor caller makes after this succeeds - see Panels/InspectorPanel.cpp),
// exactly the same "persistence and cache invalidation are two separate,
// explicit steps" split SaveJointPhysicsOverridesToGtaFile()/
// RefreshCachedJointPhysicsOverridesFromDisk() already established.
//
// `outErrorMessage`, if non-null, is set to a short human-readable reason on
// failure - left untouched on success.
bool RecomputeAndSaveMeshNormalsToGtaFile(const std::string& absoluteGtaPath, std::string* outErrorMessage = nullptr);

} // namespace gte
```

### 3.4 — New file: `src/Assets/MeshNormalRecomputePersistence.cpp`

Mirrors `DynamicChainPhysicsPersistence.cpp`'s own structure exactly (read it
first): a local anonymous-namespace `Utf8PathFromGamePath()` helper (same
`std::u8string` round-trip - duplicated locally per this codebase's own
established "no cross-layer utility header for a 2-call-site helper"
convention, see that file's own comment on this exact point) + a local
`SetError()` helper, then:

```cpp
bool RecomputeAndSaveMeshNormalsToGtaFile(const std::string& absoluteGtaPath, std::string* outErrorMessage)
{
    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteGtaPath));
    if (!gta.has_value()) {
        SetError(outErrorMessage, "Could not read the *.gta file (missing, unreadable, or bad magic).");
        return false;
    }
    if (gta->header.Type() != AssetType::Mesh) {
        SetError(outErrorMessage, "This *.gta file is not an AssetType::Mesh asset.");
        return false;
    }

    std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value()) {
        SetError(outErrorMessage, "This *.gta file's existing mesh payload could not be decoded - refusing to overwrite it.");
        return false;
    }

    RecomputeMeshNormalsFromGeometry(*mesh);

    const std::vector<std::uint8_t> newPayload = EncodeMeshDataToBytes(*mesh);
    const bool wrote = WriteGtaFile(Utf8PathFromGamePath(absoluteGtaPath), gta->header.Type(), gta->header.Id(),
        gta->header.Flags(), gta->metadata, newPayload, gta->header.version);
    if (!wrote) {
        SetError(outErrorMessage, "Failed to write the *.gta file back to disk.");
        return false;
    }
    return true;
}
```

(Includes needed: `"AssetTypes.h"`, `"GtaFile.h"`, `"MeshFile.h"`,
`"MeshNormalRecompute.h"`, `<filesystem>` — match
`DynamicChainPhysicsPersistence.cpp`'s own include list shape.)

### 3.5 — Register the four new files

Add to the root `CMakeLists.txt`'s main source list, next to the other
`src/Assets/*.h`/`*.cpp` entries:

```
src/Assets/MeshNormalRecompute.h
src/Assets/MeshNormalRecompute.cpp
src/Assets/MeshNormalRecomputePersistence.h
src/Assets/MeshNormalRecomputePersistence.cpp
```

### 3.6 — `MeshAssetGpuCatalog` cache-invalidation hook

In `src/Game/Instantiation/MeshAssetGpuCatalog.h`, add (public section, next
to `RefreshCachedJointPhysicsOverridesFromDisk`):

```cpp
// Drops any cached MeshAssetPart/SkinnedMeshData entry for `absoluteGtaPath`
// (a no-op if this path was never cached), so the NEXT Resolve()/
// EnsureMeshAsset() call for that same path re-reads and re-uploads it fresh
// from disk - used after an operation that changed the *.gta's own vertex
// DATA in place (e.g. RecomputeAndSaveMeshNormalsToGtaFile() -
// MeshNormalRecomputePersistence.h), unlike
// RefreshCachedJointPhysicsOverridesFromDisk() above (which patches a
// cached value IN PLACE because that data is read fresh at attach-time, not
// baked into a GPU buffer) - normals ARE baked directly into the uploaded
// MeshVertex GPU buffer at upload time, so the only safe fix here is a
// fresh re-upload, not an in-place patch. Deliberately does NOT retroactively
// touch any ALREADY-SPAWNED Scene entity's current GPU mesh (those keep
// showing whatever normals they had at spawn time until deleted and
// re-spawned) - see this method's own call site in Panels/InspectorPanel.cpp
// for the exact user-facing wording of this limitation.
void InvalidateCachedMeshAsset(const std::string& absoluteGtaPath);
```

In `MeshAssetGpuCatalog.cpp`:

```cpp
void MeshAssetGpuCatalog::InvalidateCachedMeshAsset(const std::string& absoluteGtaPath)
{
    m_meshAssetCache.erase(absoluteGtaPath);
    m_skinnedMeshCache.erase(absoluteGtaPath);
}
```

In `src/Game/Instantiation/MeshInstantiationSystem.h`, add the exact same
one-line forwarding shape `RefreshCachedJointPhysicsOverridesFromDisk`
already uses (same file, right next to it):

```cpp
void InvalidateCachedMeshAsset(const std::string& absoluteGtaPath)
{
    m_meshAssetCatalog.InvalidateCachedMeshAsset(absoluteGtaPath);
}
```

### 3.7 — Inspector button

`src/Editor/Panels/InspectorPanel.cpp`'s `BuildAssetInspector()` actually has
**TWO** separate call sites that each render mesh metadata for the same
`isGtaMesh && gtaHeader.has_value()` condition, not one — confirmed by
inspection: one inside the `if (preview.has_value() || meshPreview.has_value())`
split-layout branch (`BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes,
meshPreview); ImGui::Separator();`, around line 1318-1321, taken whenever the
live mesh preview render succeeds), and a SECOND, separate one in the
fallback branch reached only when BOTH `preview` and `meshPreview` are
`std::nullopt` (`BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes,
std::nullopt); ImGui::Separator();`, around line 1366-1368 — reached whenever
`AssetPreviewMesh::Render()` fails, e.g. `EnsureMeshUploaded()` rejects a
zero-vertex/corrupt mesh, see `AssetPreviewMesh.cpp`). The button MUST be
reachable from BOTH of these, not just the first — otherwise a Mesh `*.gta`
whose live preview happens to fail to render (arguably exactly the kind of
asset a user is most likely to want to fix via this very button) would have
no way to trigger it at all. Do this by factoring the button + status line +
disabled-text block into ONE small local helper function (anonymous
namespace, same file, placed near `BuildGtaMeshMetadata()`) and calling that
helper from both of the two `else if (isGtaMesh && gtaHeader.has_value())`
sites, immediately after each site's own `BuildGtaMeshMetadata(...)` call,
still inside that same `if`/`else if` block:

```cpp
void BuildRecomputeNormalsFromGeometryButton(const std::string& absolutePath, MeshInstantiationSystem& meshInstantiationSystem)
{
    static bool s_lastNormalRecomputeSucceeded = false;
    static std::string s_lastNormalRecomputeError;

    if (ImGui::Button("Recompute Normals from Geometry")) {
        std::string errorMessage;
        s_lastNormalRecomputeSucceeded = RecomputeAndSaveMeshNormalsToGtaFile(absolutePath, &errorMessage);
        s_lastNormalRecomputeError = errorMessage;
        if (s_lastNormalRecomputeSucceeded) {
            meshInstantiationSystem.InvalidateCachedMeshAsset(absolutePath);
        }
    }
    if (!s_lastNormalRecomputeError.empty() || s_lastNormalRecomputeSucceeded) {
        if (s_lastNormalRecomputeSucceeded) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Recomputed normals and saved to asset.");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Recompute failed: %s", s_lastNormalRecomputeError.c_str());
        }
    }
    ImGui::TextDisabled(
        "Overwrites every stored normal with a fresh, area-weighted average computed directly from this "
        "mesh's own vertex positions - useful when a source file's original normals are missing/incorrect "
        "(common for some exported STL files). Only affects instances spawned AFTER this; any copies of this "
        "model already placed in the current Scene keep their current normals until deleted and re-spawned.");
}
```

Then, at BOTH call sites (`... BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes, meshPreview); ImGui::Separator();`
around line 1318-1321, AND `... BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes, std::nullopt); ImGui::Separator();`
around line 1366-1368), add immediately after each one's own `ImGui::Separator();`:

```cpp
BuildRecomputeNormalsFromGeometryButton(absolutePath, meshInstantiationSystem);
```

(`absolutePath` is already in scope at both sites - it is `BuildAssetInspector()`'s
own local `const std::string&` bound to `ctx.selection.SelectedAssetAbsolutePath()`
near the top of the function - and a single static pair of "last attempt"
locals shared by both call sites is intentional and correct: only one of the
two branches ever actually executes for a given selection/frame, so there is
never any ambiguity about which one the button's own status line refers to.)

This requires `BuildAssetInspector()` to actually have a `MeshInstantiationSystem&`
in scope — confirmed by inspection: its current parameter list is exactly
`(EditorContext&, Renderer&, AssetPreviewTexture&, AssetPreviewMesh&)`, so it
does NOT have one yet. However, its one and only call site
(`BuildAssetInspector(ctx, renderer, assetPreview, assetPreviewMesh);`,
around line 1392) sits INSIDE `BuildInspectorPanel()` - the very same
function that ALREADY receives its own `MeshInstantiationSystem&
meshInstantiationSystem` parameter (used a few hundred lines below for the
sibling "Dynamic Chain Physics" section's "Save Joint Physics to Asset"
button - see that section's own `meshInstantiationSystem.
RefreshCachedJointPhysicsOverridesFromDisk(...)` call for the precedent this
mirrors). So the only change needed is ONE level deep, entirely local to
this file: add a `MeshInstantiationSystem& meshInstantiationSystem` parameter
to `BuildAssetInspector()`'s own signature, and pass `meshInstantiationSystem`
through at its one call site - there is no need to touch `ImGuiEditorLayer`
or any other file at all for this plumbing.

Also add the necessary `#include "../../Assets/MeshNormalRecomputePersistence.h"`
to `InspectorPanel.cpp`'s existing include list.

### 3.8 — Tests: new file `tests/Assets/MeshNormalRecomputeTests.cpp`

Pure, Tier-1, no file I/O at all — plain `MeshData` construction + assertion,
following `tests/Math/Vec3Tests.cpp`'s own hand-computed-value style:

- `RecomputesAFlatNormalForASingleNonSharedTriangle` — one triangle (3
  unique vertices, `indices = {0, 1, 2}`), a deliberately WRONG starting
  normal on all 3. After calling `RecomputeMeshNormalsFromGeometry()`, assert
  all 3 resulting normals equal `Normalize(Cross(v1 - v0, v2 - v0))` computed
  independently by the test.
- `ProducesFlatDistinctNormalsForTwoNonSharedCoplanarTriangles` — the direct
  regression test for STL-shaped (non-shared) input: 2 triangles sharing an
  edge by POSITION but using 6 independent vertex slots (indices `{0,1,2,
  3,4,5}`, matching `PHASE1`'s own non-welding contract) lying in the same
  plane. Assert both triangles' 3-vertex normal groups end up equal to each
  other (same plane ⇒ same flat normal) and to the hand-computed face
  normal.
- `AveragesNormalsAcrossATrueSharedVertex` — the direct regression test for
  PMX-shaped (shared) input: a small fan of e.g. 4 triangles sharing ONE
  central vertex index, each triangle in a different plane/orientation.
  Assert the shared vertex's resulting normal equals the manually-computed
  `Normalize(sum of each triangle's own raw un-normalized face cross
  product)` - i.e. confirm real area-weighted averaging actually happened,
  not just "whichever triangle came last wins."
- `ZeroAreaTriangleContributesNothingAndNeverProducesNaN` — a degenerate
  triangle (two identical vertex positions). Assert the resulting normal(s)
  are finite (`std::isfinite` on x/y/z) and, for the case where ALL of a
  vertex's triangles are degenerate, equal exactly `Vec3::Zero()`.
- `ResizesNormalsArrayIfItDoesNotAlreadyMatchPositions` — start with
  `mesh.normals` empty (or a mismatched size) and confirm it ends up sized
  exactly `mesh.positions.size()` afterward with no crash.
- `IgnoresATrailingPartialTriangleWithoutCrashing` — `indices.size() % 3 !=
  0` (e.g. one dangling extra index at the end). Assert no crash/out-of-
  bounds and that the well-formed leading triangles still compute correctly.

### 3.9 — Tests: new file `tests/Assets/MeshNormalRecomputePersistenceTests.cpp`

Real-temp-file round-trip tests, following
`tests/Assets/AssetImporterTests.cpp`'s own fixture-directory
`SetUp()`/`TearDown()` convention (a fresh `std::filesystem::temp_directory_path()`
subfolder per test):

- `RecomputesAndOverwritesNormalsInAnExistingMeshGtaFile` — hand-build a
  small `MeshData` (a single triangle) with deliberately wrong normals,
  encode it via `EncodeMeshDataToBytes()`, wrap it as a Mesh `*.gta` via
  `WriteGtaFile()` directly (a fixed dummy GUID/version/empty metadata is
  fine here), call `RecomputeAndSaveMeshNormalsToGtaFile()`, assert it
  returns `true`, then `ReadGtaFile()` the SAME path again and assert: the
  GUID/flags/version/metadata bytes are byte-for-byte unchanged, and the
  freshly-decoded `MeshData`'s normals now match the geometry-derived
  expectation (not the original wrong ones).
- `FailsGracefullyWhenTheFileDoesNotExist` — assert `false` + non-empty
  `outErrorMessage`, no file created.
- `FailsGracefullyWhenTheGtaIsNotAMeshAsset` — write a Texture-type `*.gta`
  at the target path (any valid KTX2 payload, or even an empty payload with
  `AssetType::Texture` is fine for this purely-type-check assertion), assert
  graceful failure with a message mentioning it isn't a Mesh asset.
- `FailsGracefullyWhenThePayloadIsUndecodable` — write a Mesh-type `*.gta`
  whose payload bytes are deliberately NOT a valid `EncodeMeshDataToBytes()`
  blob (e.g. a few random bytes), assert graceful failure, and that the
  ORIGINAL file on disk is left completely untouched (re-read it and confirm
  its bytes are unchanged from what was written before the call).

### 3.10 — Register the two new test files

Add `Assets/MeshNormalRecomputeTests.cpp` and
`Assets/MeshNormalRecomputePersistenceTests.cpp` to `tests/CMakeLists.txt`'s
`GTE_TEST_SOURCES` list (next to the other `Assets/*Tests.cpp` entries) plus
a one-line description each in that file's own leading Tier-1 taxonomy
comment block, matching every existing entry's style.

### 3.11 — Build & verify

- `cmake --build build` succeeds (this phase touches `InspectorPanel.cpp`,
  which only compiles under `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` —
  confirm the default build configuration used for verification has these
  ON, matching how every other Inspector-panel feature in this codebase is
  normally verified).
- `ctest -C Debug --output-on-failure` passes, including the new
  `MeshNormalRecomputeTests.cpp`/`MeshNormalRecomputePersistenceTests.cpp`
  suites, with zero regressions to any existing test (in particular, every
  existing `DynamicChainPhysicsPersistence`-related test, since this phase's
  `InspectorPanel.cpp` edit sits right next to that code).
- Manual visual sanity check (recommended, GPU/window required — use
  `run_app_background` + `gte_send_request`'s `/get_swapchain` or
  `/get_game_view` for non-blocking screenshot verification, per this
  engine's own established Editor-verification convention): import the real
  `terrain.stl` reference asset via the Editor's "Project" panel drag-and-
  drop (or the `--reimport` CLI from `PHASE2`), select the resulting Mesh
  `*.gta` in the Inspector, click "Recompute Normals from Geometry", and
  visually confirm the live mesh preview keeps looking correct (no black/
  inverted/flickering shading) before and after clicking it.
- Write `PHASE3_COMPLETION_REPORT.md` (this same folder) summarizing what was
  added, the regression-safety confirmation, and the manual visual check's
  outcome if performed.
- `git add` + `git commit` the changed/added files together with the report.
