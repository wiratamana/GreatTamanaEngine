# PHASE2_COMPLETION_REPORT.md — Asset Importer STL Gating & Mesh Source Format

**Phase:** `PHASE2_ASSET_IMPORTER_STL_GATING_AND_MESH_SOURCE_FORMAT.md`
**Campaign:** `stl-parser-1` (parent: `PHASE0_MASTER_STRATEGY.md`)
**Branch:** `feature/stl-parser-impl` (unchanged, as required)

## Summary

Wired the PHASE1 `LoadStlModel()`/`StlLoadResult` (`src/Assets/StlLoader.h`)
into the existing mesh-import gating machinery in `src/Assets/AssetImporter.h`/
`.cpp`, exactly as the strategy doc specifies: `IsImportableAsMeshAsset()` now
accepts `.stl` alongside `.pmx`, the `.pmx`/`.stl` success paths share one
extracted `FinalizeMeshAssetImport()` helper (moved verbatim from the old
inline `.pmx` branch logic, parameterized by `MeshSourceFormat`/a format
label string), and `AssetImportResult` gained a `meshSourceFormat` field.

### Files changed

- `src/Assets/AssetImporter.h`
  - Added `enum class MeshSourceFormat { Unknown = 0, Pmx = 1, Stl = 2 }`,
    placed just above `AssetImportResult`, doc comment copied verbatim from
    the strategy doc's Step 3.1 code block.
  - Added `AssetImportResult::meshSourceFormat` field immediately after
    `convertedToMeshAsset`.
  - Updated `IsImportableAsMeshAsset()`'s doc comment to mention both `.pmx`
    (`PmxLoader.h`) and `.stl` (`StlLoader.h`, both binary and ASCII).
  - Updated `ImportAssetFile()`'s own top-of-file doc comment's item 1 to
    fold STL into the existing mesh-import bullet (not a new numbered item),
    mentioning `LoadStlModel()`/`AssetImportResult::meshSourceFormat`.
- `src/Assets/AssetImporter.cpp`
  - `#include "StlLoader.h"` added alongside the existing `PmxLoader.h`/
    `RigFile.h`/`VmdLoader.h`/`MeshFile.h`/`MotionFile.h` includes.
  - New private helper `FinalizeMeshAssetImport(AssetDatabase&, MeshData,
    RigFileData, sourcePath, preferredDestinationPath, MeshSourceFormat,
    sourceFormatLabel)` in the anonymous namespace, placed right after
    `ImportPmxMaterialTextures()`. Its body is the existing inline `.pmx`
    branch's tail logic (gtaPath construction, the jointPhysicsOverrides-
    preservation read via `ReadGtaFile`/`DecodeRigDataFromBytes`,
    `ImportPmxMaterialTextures()` call, `EncodeMeshDataToBytes`/
    `EncodeRigDataToBytes`, `database.ImportAsset()`, and the
    `AssetImportResult` field population + message-building), moved
    verbatim and parameterized by `sourceFormat`/`sourceFormatLabel` for the
    two documented differences (`result.meshSourceFormat = sourceFormat`,
    and injecting `sourceFormatLabel` — `"PMX"` or `"STL"` — into the
    `"Imported \"...\" as a <label> mesh (...) -> \"...\"."` message).
  - `ImportAssetFile()`'s mesh-import branch top level rewritten to dispatch
    by extension inside the `IsImportableAsMeshAsset(extension)` gate:
    `.pmx` → `LoadPmxModel()` (builds `RigFileData` from
    `skinWeights`/`skeleton`/`morphs`/`physics`/`materials` exactly as
    before, then calls `FinalizeMeshAssetImport(..., MeshSourceFormat::Pmx,
    "PMX")`), `.stl` → the new `LoadStlModel()` (an entirely
    default-constructed, all-empty `RigFileData`, then
    `FinalizeMeshAssetImport(..., MeshSourceFormat::Stl, "STL")`). Both
    parse-failure paths fall back to `ImportAsPlainCopy()` with the same
    "Could not parse as a mesh, imported as-is instead. " message, matching
    the existing PMX fallback shape exactly.
  - `IsImportableAsMeshAsset()`'s implementation updated to
    `extensionLowercaseWithDot == ".pmx" || extensionLowercaseWithDot == ".stl"`.

### Files added / extended (tests)

- `tests/Assets/AssetImporterTests.cpp`
  - `BuildMinimalOneTriangleBinaryStl()` — a new local 134-byte binary STL
    fixture builder (80-byte header + `uint32` triangle count 1 + one
    50-byte triangle record), reusing the file's already-existing
    `PmxU8()`/`PmxU32()`/`PmxF32()` byte-writing helpers rather than
    duplicating them, matching the strategy doc's Step 3.4 instruction.
  - `TEST(IsImportableAsMeshAssetTest, RecognizesStl)`.
  - `TEST_F(AssetImporterTest, PmxImportStillReportsMeshSourceFormatPmx)` —
    the explicit `MeshSourceFormat::Pmx` regression assertion the strategy
    doc calls for.
  - `TEST_F(AssetImporterTest, ConvertsAValidStlToMeshWrappedGta)` — the
    STL-equivalent of `ConvertsAValidPmxToMeshWrappedGta`, asserting
    `success`, `convertedToMeshAsset`, `meshSourceFormat ==
    MeshSourceFormat::Stl`, `.gta` extension, a valid guid, 3
    vertices/1 triangle, and every rig-derived count (skinned vertices,
    bones, morphs, rigid bodies, joints, materials, textures) at zero.
  - `TEST_F(AssetImporterTest, ConvertedStlMeshAssetsMetadataDecodesBackToAnEmptyRig)`
    — mirrors the PMX metadata round-trip test, asserting every rig section
    (`skinWeights`/`skeleton.bones`/`morphs.morphs`/`physics.rigidBodies`/
    `materials.materials`) decodes back empty.
  - `TEST_F(AssetImporterTest, ConvertedStlMeshAssetIsImmediatelyTrackedByTheDatabase)`
    — mirrors the PMX database-tracking test.
  - `TEST_F(AssetImporterTest, CorruptStlExtensionFallsBackToPlainCopy)` —
    a `fake.stl` containing plain non-STL text, asserting the plain-copy
    fallback (`success`, `!convertedToMeshAsset`, `finalPath` unchanged).

No changes were needed to `tests/CMakeLists.txt` (the file was already
registered by PHASE1).

### `src/Editor/Panels/ProjectPanel.cpp` — confirmed unchanged (by inspection)

Per Locked Design Decision 2, inspected `HandleExternalFileDrop()`'s own
`.gta`-renaming check (line ~482):

```cpp
if (IsImportableAsKtx2Texture(extension) || IsImportableAsMeshAsset(extension)) {
    desiredName.replace_extension(".gta");
}
```

This calls `IsImportableAsMeshAsset()` generically — no `.pmx`-specific
literal anywhere in this file — so a dropped `.stl` is automatically renamed
to `.gta` and imported correctly the moment this phase lands, with zero
changes required here. Confirmed by inspection only; no edits made to this
file, matching the strategy doc's explicit instruction.

## Deviations from the strategy doc

None. Every element of Step 3.1–3.4 was implemented exactly as specified:
the `MeshSourceFormat` enum values/ordering/doc comments, the
`FinalizeMeshAssetImport()` signature and body (parameterized exactly as
described), the dispatch shape inside `ImportAssetFile()`'s mesh-import
branch, `IsImportableAsMeshAsset()`'s updated implementation, and every test
case Step 3.4 lists (STL import success, metadata round-trip, database
tracking, corrupt-STL plain-copy fallback, and the `MeshSourceFormat::Pmx`
regression assertion).

## Regression safety confirmation (Step 3.3)

Re-ran the FULL existing `tests/Assets/AssetImporterTests.cpp` suite
unchanged. Every pre-existing `.pmx`/`.vmd`/image-related test still passes
with IDENTICAL assertions (no test file changed for these cases, only new
tests were appended): `ConvertsAValidImageToKtx2WrappedGta`,
`ConvertedAssetIsImmediatelyTrackedByTheDatabase`,
`NonImageExtensionIsCopiedAsIsWithNoGtaWrapping`,
`CorruptImageExtensionFallsBackToPlainCopy`,
`FailsGracefullyWhenSourceDoesNotExist`,
`CreatesMissingDestinationDirectoriesForAPlainCopy`,
`ConvertsAValidPmxToMeshWrappedGta`,
`ConvertedMeshAssetsMetadataDecodesBackToItsRigData`,
`ReimportingAPmxPreservesAnAlreadySavedJointPhysicsOverridesList`,
`FirstTimeImportOfAPmxHasNoJointPhysicsOverridesToPreserve`,
`ConvertedMeshAssetIsImmediatelyTrackedByTheDatabase`,
`CorruptPmxExtensionFallsBackToPlainCopy`,
`ConvertsAValidVmdToMotionWrappedGta`,
`ConvertedMotionAssetsPayloadDecodesBackToItsMotionData`,
`ConvertedMotionAssetIsImmediatelyTrackedByTheDatabase`,
`CorruptVmdExtensionFallsBackToPlainCopy` — all 21 tests in
`AssetImporterTest` pass, confirming `FinalizeMeshAssetImport()`'s
extraction did not change PMX/VMD/image import behavior in any observable
way (see "Build & test results" below for the actual run output).

## Build & test results (this machine)

### Targeted build

```
cmake --build build --target GreatTamanaEngineTests
cmake --build build --target GreatTamanaEngine
```

Both succeeded with no new warnings from `AssetImporter.h`/`.cpp` or
`AssetImporterTests.cpp`.

### Targeted test run (`--gtest_filter=*AssetImporter*`)

```
[==========] Running 21 tests from 1 test suite.
...
[==========] 21 tests from 1 test suite ran. (268 ms total)
[  PASSED  ] 21 tests.
```

Including the new tests:

```
[ RUN      ] AssetImporterTest.PmxImportStillReportsMeshSourceFormatPmx
[       OK ] AssetImporterTest.PmxImportStillReportsMeshSourceFormatPmx (12 ms)
[ RUN      ] AssetImporterTest.ConvertsAValidStlToMeshWrappedGta
[       OK ] AssetImporterTest.ConvertsAValidStlToMeshWrappedGta (13 ms)
[ RUN      ] AssetImporterTest.ConvertedStlMeshAssetsMetadataDecodesBackToAnEmptyRig
[       OK ] AssetImporterTest.ConvertedStlMeshAssetsMetadataDecodesBackToAnEmptyRig (8 ms)
[ RUN      ] AssetImporterTest.ConvertedStlMeshAssetIsImmediatelyTrackedByTheDatabase
[       OK ] AssetImporterTest.ConvertedStlMeshAssetIsImmediatelyTrackedByTheDatabase (12 ms)
[ RUN      ] AssetImporterTest.CorruptStlExtensionFallsBackToPlainCopy
[       OK ] AssetImporterTest.CorruptStlExtensionFallsBackToPlainCopy (17 ms)
```

Also confirmed `--gtest_filter=*IsImportable*` (7 tests, including the new
`IsImportableAsMeshAssetTest.RecognizesStl`) all pass.

### Manual CLI sanity check (Step 3.5, optional but performed)

Ran the headless `--reimport` CLI against the real reference asset:

```
GreatTamanaEngine.exe --reimport ^
    "C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\_reference\pl-sky\assets\terrain.stl" ^
    "C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build\ManualVerification\terrain.gta"
```

Output:

```
Imported "terrain.stl" as a STL mesh (3136374 vertices, 1045458 triangles, 0 bones, 0 morphs, 0 rigid bodies, 0 joints, 0 materials, 0 textures) -> "terrain.gta".
```

Exactly matching the master strategy doc's measured facts (1,045,458
triangles → 3,136,374 non-shared vertices), and correctly reporting the new
`"STL"` label in the message. The throwaway `ManualVerification` output
directory was deleted afterward, per the phase doc's instruction.

### Full regression suite (`ctest -C Debug --output-on-failure`)

```
100% tests passed out of 1457

Total Test time (real) = 109.04 sec

The following tests did not run:
	1328 - PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine (Skipped)
```

Zero regressions. The one skipped test is the same pre-existing, unrelated
real-fixture smoke test already noted in `PHASE1_COMPLETION_REPORT.md` (its
own MMD model directory isn't present on this machine) — not something this
phase touched or affected. Test count grew from 1451 (PHASE1's baseline) to
1457 — the 6 new tests this phase added
(`IsImportableAsMeshAssetTest.RecognizesStl`,
`AssetImporterTest.PmxImportStillReportsMeshSourceFormatPmx`,
`AssetImporterTest.ConvertsAValidStlToMeshWrappedGta`,
`AssetImporterTest.ConvertedStlMeshAssetsMetadataDecodesBackToAnEmptyRig`,
`AssetImporterTest.ConvertedStlMeshAssetIsImmediatelyTrackedByTheDatabase`,
`AssetImporterTest.CorruptStlExtensionFallsBackToPlainCopy`).

## Definition-of-done checklist (this phase's slice)

- [x] `IsImportableAsMeshAsset(".stl")` returns `true`.
- [x] `AssetImporter.cpp`'s mesh-import branch dispatches internally by
  extension (`.pmx` → `LoadPmxModel()`, `.stl` → `LoadStlModel()`), sharing
  one `FinalizeMeshAssetImport()` helper for the "write the Mesh `*.gta`"
  logic.
- [x] `AssetImportResult::meshSourceFormat` correctly reports `Stl` vs.
  `Pmx` (and `Unknown` for a non-mesh import — unchanged default).
- [x] `src/Editor/Panels/ProjectPanel.cpp`'s `HandleExternalFileDrop()`
  needs zero changes — confirmed by inspection.
- [x] Every existing `.pmx`/`.vmd`/image `AssetImporterTests.cpp` assertion
  still passes unchanged.
- [x] Every new test case Step 3.4 lists is implemented and passes.
- [x] `cmake --build build` succeeds; `ctest -C Debug --output-on-failure`
  passes with zero regressions and the new tests included.
- [x] Manual CLI sanity check against the real `terrain.stl` performed and
  succeeded, reporting the expected counts.

## Git

Source/test changes plus this report are committed together in one commit
on `feature/stl-parser-impl` (branch unchanged, per the workflow rules).
