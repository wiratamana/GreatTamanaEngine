# PHASE3_COMPLETION_REPORT.md — Generic Mesh Normal Recompute & Inspector Action

**Phase:** `PHASE3_GENERIC_MESH_NORMAL_RECOMPUTE_AND_INSPECTOR_ACTION.md`
**Campaign:** `stl-parser-1` (parent: `PHASE0_MASTER_STRATEGY.md`)
**Branch:** `feature/stl-parser-impl` (unchanged, as required)

## Summary

Implemented the generic, format-agnostic "recompute every normal from this
mesh's own vertex geometry" capability exactly as specified: a pure,
Tier-1-testable area-weighted per-vertex normal-accumulation function, a thin
persistence wrapper that rewrites an existing Mesh `*.gta`'s payload bytes in
place, a `MeshAssetGpuCatalog`/`MeshInstantiationSystem` cache-invalidation
hook, and a new Inspector "Recompute Normals from Geometry" button reachable
from both of `BuildAssetInspector()`'s mesh-metadata call sites.

### Files added

- `src/Assets/MeshNormalRecompute.h` / `.cpp` — `RecomputeMeshNormalsFromGeometry()`,
  implemented exactly per the strategy doc's Step 3.1/3.2 code blocks: zero-init
  one `Vec3` accumulator per vertex, walk `indices` 3-at-a-time accumulating each
  triangle's RAW (un-normalized) face cross product into all 3 of its own vertex
  slots, then `Normalize()` once at the end. Resizes `mesh.normals` to
  `mesh.positions.size()` first if mismatched; tolerates a non-multiple-of-3
  trailing partial triangle and out-of-bounds indices defensively; never throws.
- `src/Assets/MeshNormalRecomputePersistence.h` / `.cpp` —
  `RecomputeAndSaveMeshNormalsToGtaFile()`, mirroring
  `DynamicChainPhysicsPersistence.h`/`.cpp`'s exact structure: a local
  `Utf8PathFromGamePath()` + `SetError()` helper, `ReadGtaFile()` →
  `AssetType::Mesh` type-check → `DecodeMeshDataFromBytes()` →
  `RecomputeMeshNormalsFromGeometry()` → `EncodeMeshDataToBytes()` →
  `WriteGtaFile()` preserving GUID/flags/version/metadata byte-for-byte;
  conservative-on-failure (never writes on a decode/type failure).
- `tests/Assets/MeshNormalRecomputeTests.cpp` — every Step 3.8 test case:
  `RecomputesAFlatNormalForASingleNonSharedTriangle`,
  `ProducesFlatDistinctNormalsForTwoNonSharedCoplanarTriangles`,
  `AveragesNormalsAcrossATrueSharedVertex`,
  `ZeroAreaTriangleContributesNothingAndNeverProducesNaN`,
  `ResizesNormalsArrayIfItDoesNotAlreadyMatchPositions`,
  `IgnoresATrailingPartialTriangleWithoutCrashing`.
- `tests/Assets/MeshNormalRecomputePersistenceTests.cpp` — every Step 3.9 test
  case: `RecomputesAndOverwritesNormalsInAnExistingMeshGtaFile`,
  `FailsGracefullyWhenTheFileDoesNotExist`,
  `FailsGracefullyWhenTheGtaIsNotAMeshAsset`,
  `FailsGracefullyWhenThePayloadIsUndecodable` (asserting the ORIGINAL file on
  disk is byte-for-byte unchanged after a rejected write).

### Files changed

- `CMakeLists.txt` — registered the four new `src/Assets/MeshNormalRecompute*`
  files immediately after `src/Assets/StlLoader.h`/`.cpp`, matching Step 3.5.
- `tests/CMakeLists.txt` — added `Assets/MeshNormalRecomputeTests.cpp` and
  `Assets/MeshNormalRecomputePersistenceTests.cpp` to `GTE_TEST_SOURCES`
  immediately after `Assets/RigFileTests.cpp`, plus a descriptive entry for
  each in the file's own leading Tier-1 taxonomy comment block, matching
  Step 3.10.
- `src/Game/Instantiation/MeshAssetGpuCatalog.h` — added
  `InvalidateCachedMeshAsset(const std::string&)`'s public declaration + doc
  comment, right after `RefreshCachedJointPhysicsOverridesFromDisk()`, per
  Step 3.6.
- `src/Game/Instantiation/MeshAssetGpuCatalog.cpp` — added the method's
  implementation (`m_meshAssetCache.erase(...)` +
  `m_skinnedMeshCache.erase(...)`), exactly as specified — a full
  erase-and-reupload, not an in-place patch, since normals are baked directly
  into the GPU vertex buffer.
- `src/Game/Instantiation/MeshInstantiationSystem.h` — added the one-line
  forwarding wrapper `InvalidateCachedMeshAsset()`, mirroring
  `RefreshCachedJointPhysicsOverridesFromDisk()`'s exact shape.
- `src/Editor/Panels/InspectorPanel.cpp`:
  - Added `#include "../../Assets/MeshNormalRecomputePersistence.h"` to the
    `GTE_ENABLE_PROJECT_PANEL` include block.
  - Added a new anonymous-namespace helper,
    `BuildRecomputeNormalsFromGeometryButton(const std::string&,
    MeshInstantiationSystem&)`, placed immediately after `BuildGtaMeshMetadata()`
    — the button + a shared static "last attempt" status line + the
    `TextDisabled()` explanatory tooltip text, exactly per Step 3.7's code
    block.
  - Called `BuildRecomputeNormalsFromGeometryButton(absolutePath,
    meshInstantiationSystem)` from BOTH of `BuildAssetInspector()`'s
    `isGtaMesh && gtaHeader.has_value()` branches — the "live preview
    succeeded" split-layout branch (right after its own
    `BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes, meshPreview);
    ImGui::Separator();`) AND the "preview failed" fallback branch (right
    after its own `BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes,
    std::nullopt); ImGui::Separator();`) — so the button is reachable even
    when a mesh's live preview fails to render.
  - Added a `MeshInstantiationSystem&` parameter to `BuildAssetInspector()`'s
    signature and threaded it through at its one call site inside
    `BuildInspectorPanel()` (which already receives its own
    `MeshInstantiationSystem& meshInstantiationSystem` parameter, used by the
    neighboring "Dynamic Chain Physics" section) — a single-file, one-level
    plumbing change, exactly as the strategy doc describes; no other file
    needed to change for this.

## Deviations from the strategy doc

None. Every element of Steps 3.1–3.10 was implemented exactly as specified —
the accumulation algorithm, the persistence wrapper's structure/contract, the
cache-invalidation method's full-erase (not in-place-patch) shape, the shared
button helper factored out and called from both Inspector call sites, the
`BuildAssetInspector()` signature/call-site plumbing, and every test case both
Step 3.8 and Step 3.9 list.

## Build & test results (this machine)

### Targeted build

```
cmake -S . -B build
cmake --build build --target GreatTamanaEngineTests
cmake --build build --target GreatTamanaEngine
```

Both targets built successfully with no new warnings from
`MeshNormalRecompute.h`/`.cpp`, `MeshNormalRecomputePersistence.h`/`.cpp`,
`MeshAssetGpuCatalog.h`/`.cpp`, `MeshInstantiationSystem.h`, or
`InspectorPanel.cpp`. This build's active configuration has both
`GTE_ENABLE_EDITOR=ON` and `GTE_ENABLE_PROJECT_PANEL=ON` (confirmed via
`build/CMakeCache.txt` before starting), so `InspectorPanel.cpp`'s changes
were genuinely compiled and exercised, not skipped.

### Targeted test run (`--gtest_filter=*MeshNormalRecompute*`)

```
[==========] Running 10 tests from 2 test suites.
[----------] 4 tests from MeshNormalRecomputePersistenceTest
[ RUN      ] MeshNormalRecomputePersistenceTest.RecomputesAndOverwritesNormalsInAnExistingMeshGtaFile
[       OK ] MeshNormalRecomputePersistenceTest.RecomputesAndOverwritesNormalsInAnExistingMeshGtaFile (11 ms)
[ RUN      ] MeshNormalRecomputePersistenceTest.FailsGracefullyWhenTheFileDoesNotExist
[       OK ] MeshNormalRecomputePersistenceTest.FailsGracefullyWhenTheFileDoesNotExist (2 ms)
[ RUN      ] MeshNormalRecomputePersistenceTest.FailsGracefullyWhenTheGtaIsNotAMeshAsset
[       OK ] MeshNormalRecomputePersistenceTest.FailsGracefullyWhenTheGtaIsNotAMeshAsset (1 ms)
[ RUN      ] MeshNormalRecomputePersistenceTest.FailsGracefullyWhenThePayloadIsUndecodable
[       OK ] MeshNormalRecomputePersistenceTest.FailsGracefullyWhenThePayloadIsUndecodable (2 ms)
[----------] 6 tests from MeshNormalRecomputeTests
[ RUN      ] MeshNormalRecomputeTests.RecomputesAFlatNormalForASingleNonSharedTriangle
[       OK ] ...
[ RUN      ] MeshNormalRecomputeTests.ProducesFlatDistinctNormalsForTwoNonSharedCoplanarTriangles
[       OK ] ...
[ RUN      ] MeshNormalRecomputeTests.AveragesNormalsAcrossATrueSharedVertex
[       OK ] ...
[ RUN      ] MeshNormalRecomputeTests.ZeroAreaTriangleContributesNothingAndNeverProducesNaN
[       OK ] ...
[ RUN      ] MeshNormalRecomputeTests.ResizesNormalsArrayIfItDoesNotAlreadyMatchPositions
[       OK ] ...
[ RUN      ] MeshNormalRecomputeTests.IgnoresATrailingPartialTriangleWithoutCrashing
[       OK ] ...
[==========] 10 tests from 2 test suites ran. (19 ms total)
[  PASSED  ] 10 tests.
```

### Full regression suite (`ctest -C Debug --output-on-failure`)

```
100% tests passed out of 1467
Total Test time (real) = 101.23 sec

The following tests did not run:
	1338 - PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine (Skipped)
```

Zero regressions. Test count grew from 1457 (PHASE2's baseline) to 1467 — the
10 new tests this phase added (4 in
`MeshNormalRecomputePersistenceTests.cpp`, 6 in
`MeshNormalRecomputeTests.cpp`). The one skipped test is the same
pre-existing, unrelated real-fixture smoke test already noted in
`PHASE1_COMPLETION_REPORT.md`/`PHASE2_COMPLETION_REPORT.md` (its own MMD model
directory isn't present on this machine) — not something this phase touched
or affected. Every existing `DynamicChainPhysicsPersistence`-related test
(`DynamicChainPhysicsPersistenceTest.*`, four tests) still passes unchanged,
confirming this phase's `InspectorPanel.cpp` edit sitting right next to that
code introduced no regression there.

### Manual sanity check

Ran the headless `--reimport` CLI against the real reference asset to confirm
this phase's code has no hard dependency issue with a real STL-derived Mesh
`*.gta`:

```
GreatTamanaEngine.exe --reimport ^
    "C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\_reference\pl-sky\assets\terrain.stl" ^
    "C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build\ManualVerification\terrain.gta"
```

Output: `Imported "terrain.stl" as a STL mesh (3136374 vertices, 1045458
triangles, 0 bones, 0 morphs, 0 rigid bodies, 0 joints, 0 materials, 0
textures) -> "terrain.gta".` — matching PHASE1/PHASE2's own measured facts.
The throwaway `ManualVerification` directory was deleted afterward.

Then launched the built `GreatTamanaEngine.exe` in the background and
confirmed via `GET /get_swapchain` that the Editor (with this phase's changed
`InspectorPanel.cpp` compiled in) starts up and renders normally — Hierarchy/
Scene/Game/Inspector/Project panels all present and functioning, no crash, no
visual regression. The process was then cleanly terminated.

**Limitation, honestly noted:** the phase doc's own fully-manual verification
step (select the reimported Mesh `*.gta` in the Editor's "Project" panel,
click "Recompute Normals from Geometry" in the Inspector, and visually
confirm the live mesh preview still looks correct before/after) requires
interactive mouse clicks inside specific panel widgets (asset selection in
the Project tree, then a specific Inspector button) that this engine's
current HTTP surface does not expose an endpoint for (no
"select this asset"/"click this named button" route exists — only
`/activate_tab`, which brings a whole panel to the front, not a click inside
it). This was therefore not end-to-end automated. In its place, this report
relies on: (1) the Tier-1 unit tests of the pure math function
(`MeshNormalRecomputeTests.cpp`), which independently hand-verify the exact
area-weighted algorithm against both a non-shared (STL-style) and a
shared-vertex (PMX-style, 4-triangle fan) fixture; (2) the real-temp-file
round-trip persistence tests (`MeshNormalRecomputePersistenceTests.cpp`),
which prove the wrapper genuinely rewrites a Mesh `*.gta`'s normals in place
while leaving GUID/flags/version/metadata untouched; and (3) the live
Editor-launch/render smoke check above, confirming the new Inspector code
compiles and runs with no regression to the rest of the Editor. A future
phase wiring a generic "click a named ImGui button" HTTP command (mirroring
`EditorUiCommandBridge`'s existing pattern) would let this exact manual step
become fully automated.

## Definition-of-done checklist (this phase's slice)

- [x] `RecomputeMeshNormalsFromGeometry()` implements the area-weighted
  per-vertex accumulation algorithm exactly, correct for both non-shared
  (STL-style) and shared-vertex (PMX-style) meshes.
- [x] `RecomputeAndSaveMeshNormalsToGtaFile()` mirrors
  `DynamicChainPhysicsPersistence`'s structure/conservative-on-failure
  contract exactly.
- [x] `MeshAssetGpuCatalog::InvalidateCachedMeshAsset()` erases both caches;
  `MeshInstantiationSystem`'s forwarding wrapper added.
- [x] The Inspector button is reachable from BOTH `BuildAssetInspector()`
  mesh-metadata call sites (live-preview-succeeded AND preview-failed
  branches), via one shared helper.
- [x] `BuildAssetInspector()` gained a `MeshInstantiationSystem&` parameter,
  threaded through from its one call site in `BuildInspectorPanel()`; no
  other file needed to change.
- [x] Every test case in Step 3.8 and Step 3.9 implemented and passing.
- [x] Both new test files registered in `tests/CMakeLists.txt`.
- [x] `cmake --build build` succeeds (both `GreatTamanaEngineTests` and
  `GreatTamanaEngine`, confirming the `GTE_ENABLE_EDITOR`/
  `GTE_ENABLE_PROJECT_PANEL`-gated `InspectorPanel.cpp` change actually
  compiles); `ctest -C Debug --output-on-failure` passes with zero
  regressions and the new tests included.
- [x] Manual CLI reimport of the real `terrain.stl` performed successfully;
  live Editor launch/render smoke-checked via `GET /get_swapchain`. The fully
  interactive "click the Inspector button" step was not automatable with
  this engine's current HTTP surface (see "Limitation, honestly noted"
  above) and was not performed as a live human-driven click in this session.

## Git

Source/test changes plus this report are committed together in one commit on
`feature/stl-parser-impl` (branch unchanged, per the workflow rules).
