# PHASE2_ASSET_IMPORTER_STL_GATING_AND_MESH_SOURCE_FORMAT.md

**Parent:** `PHASE0_MASTER_STRATEGY.md` — READ IT FIRST (Locked Design
Decisions 2 and 4, and the Risk Register entry tagged "PHASE2", apply
directly to this document).
**Depends on:** `PHASE1_STL_MESH_LOADER_AND_PARSER.md` must already be
implemented and merged (`LoadStlModel()`/`StlLoadResult` must exist and
compile) before this phase starts.

## Step 1: The Goal

Wire the new `LoadStlModel()` (from `PHASE1`) into the SAME gating function
every other import format already goes through —
`src/Assets/AssetImporter.h`/`.cpp`'s `IsImportableAsMeshAsset()` /
`ImportAssetFile()` — so that dropping a `.stl` file onto the Editor's
"Project" panel, or running `--reimport some.stl dest.gta` from the CLI,
produces a real Mesh `*.gta` exactly the way a `.pmx` drop already does
today, with zero other file needing to change for that to work end-to-end.

## Step 2: The Situation

- `src/Assets/AssetImporter.h`'s `IsImportableAsMeshAsset(const std::string&
  extensionLowercaseWithDot)` currently returns `true` only for `".pmx"`.
  Its OWN doc comment already anticipates exactly this extension (verbatim):
  *"A future OBJ/glTF importer would extend this same predicate (and
  ImportAssetFile()'s matching branch below) rather than inventing a
  separate gating function."*
- `src/Assets/AssetImporter.cpp`'s `ImportAssetFile()` mesh-import branch
  (the `if (IsImportableAsMeshAsset(extension)) { ... }` block) today always
  calls `LoadPmxModel()` unconditionally inside that branch. This must become
  an internal dispatch: `.pmx` → `LoadPmxModel()` (UNCHANGED behavior — see
  Step 3.3's regression requirement), `.stl` → the new `LoadStlModel()`
  (`PHASE1`).
- That same branch today does a fair amount of PMX-specific work inline
  (texture importing via `ImportPmxMaterialTextures()`, preserving an
  existing `*.gta`'s `jointPhysicsOverrides` across a re-import, encoding
  `RigFileData`, calling `database.ImportAsset()`, building the
  `AssetImportResult`). An STL import needs almost none of the PMX-specific
  parts (no textures, no skeleton/morphs/physics/materials) but DOES still
  want: the same jointPhysicsOverrides-preservation-on-reimport behavior (for
  free, future-proofing — costs nothing since an STL's own `RigFileData` is
  simply all-empty besides that field), the same `RigFileData` encode +
  `database.ImportAsset()` + `AssetImportResult` construction shape. Extract
  this SHARED tail into one private helper function used by BOTH the `.pmx`
  and `.stl` success paths (see Step 3.2) — this is the "share one
  implementation, not two independently-diverging copies" mitigation the
  master strategy's Risk Register calls for.
- `src/Editor/Panels/ProjectPanel.cpp`'s `HandleExternalFileDrop()` already
  reads `IsImportableAsMeshAsset(extension)` generically (see that file's own
  line ~482: `if (IsImportableAsKtx2Texture(extension) ||
  IsImportableAsMeshAsset(extension)) { desiredName.replace_extension(".gta");
  }`) — per Locked Design Decision 2, this file needs **no changes at all**;
  confirm this by inspection once `IsImportableAsMeshAsset` is updated (do
  not skip this confirmation, but do not "fix" anything here unless an actual
  problem is found).

## Step 3: The Plan

### 3.1 — `src/Assets/AssetImporter.h` changes

- Update `IsImportableAsMeshAsset()`'s own doc comment: it now recognizes
  BOTH `".pmx"` (via `PmxLoader.h`) AND `".stl"` (via the new `StlLoader.h`) —
  rewrite the comment to say so plainly, keeping its existing "Tier-1-
  testable pure predicate" framing intact.
- Add a small new enum, placed just above `AssetImportResult` (same file):

  ```cpp
  // Which mesh-format loader actually produced a given
  // AssetImportResult::convertedToMeshAsset == true import - see
  // AssetImportResult::meshSourceFormat below. Explicit, never renumbered
  // once shipped, matching this codebase's existing enum-stability
  // convention (see AssetTypes.h's AssetType) - though, unlike AssetType,
  // this is NEVER itself serialized into a *.gta file; it only travels
  // through this one in-memory result struct for the duration of a single
  // ImportAssetFile() call, so a future re-ordering would be harmless in
  // practice. Kept explicit anyway for consistency with this codebase's
  // house style.
  enum class MeshSourceFormat {
      Unknown = 0, // convertedToMeshAsset is false, or this AssetImportResult predates this field.
      Pmx = 1,
      Stl = 2,
  };
  ```

- Add one new field to `AssetImportResult`, immediately after
  `convertedToMeshAsset`:

  ```cpp
  // Only meaningful when convertedToMeshAsset is true - which mesh-format
  // loader actually produced this import (see MeshSourceFormat above).
  // MeshSourceFormat::Unknown otherwise.
  MeshSourceFormat meshSourceFormat = MeshSourceFormat::Unknown;
  ```

- `#include "StlLoader.h"` is NOT needed in this header (only the `.cpp`
  needs the actual loader) — `AssetImporter.h` itself stays free of any
  format-specific include, matching its existing "only AssetDatabase.h" set.

### 3.2 — `src/Assets/AssetImporter.cpp` changes

1. `#include "StlLoader.h"` alongside the existing `MeshFile.h`/`MotionFile.h`
   /`PmxLoader.h`/`RigFile.h`/`VmdLoader.h` includes.
2. Extract a new private helper (anonymous namespace, same file), used by
   BOTH the `.pmx` and the new `.stl` success paths:

   ```cpp
   // Shared tail of a successful mesh-format parse (PMX or STL): preserves
   // an already-imported destination *.gta's own jointPhysicsOverrides
   // across a re-import (see the existing inline comment this was
   // extracted from - task_manager/verlet-integration-11, PHASE1, 3.9),
   // imports any material textures `rig.materials` references (a no-op for
   // an STL import, whose `rig.materials.textures` is always empty),
   // encodes+writes the Mesh *.gta, and builds the resulting
   // AssetImportResult. `mesh`/`rig` are consumed (moved from) - the
   // caller must not use them again afterwards.
   AssetImportResult FinalizeMeshAssetImport(AssetDatabase& database, MeshData mesh, RigFileData rig,
       const std::filesystem::path& sourcePath, const std::filesystem::path& preferredDestinationPath,
       MeshSourceFormat sourceFormat, const std::string& sourceFormatLabel);
   ```

   Its body is exactly the existing inline logic from the current `.pmx`
   branch (gtaPath construction, the `ReadGtaFile`/`DecodeRigDataFromBytes`
   jointPhysicsOverrides-preservation read, `ImportPmxMaterialTextures()`
   call, `EncodeMeshDataToBytes`/`EncodeRigDataToBytes`,
   `database.ImportAsset()`, and the `AssetImportResult` field population +
   message-building) — moved verbatim into this function, parameterized by
   `sourceFormat`/`sourceFormatLabel` for the two small differences: setting
   `result.meshSourceFormat = sourceFormat`, and building `message` as
   `"Imported \"" + filename + "\" as a " + sourceFormatLabel + " mesh (...)
   -> \"" + gtaFilename + "\"."` (i.e. inject `sourceFormatLabel` — `"PMX"` or
   `"STL"` — into the exact same message shape that already exists today, so
   the counts/format text stays otherwise identical to what `AssetImporterTests.cpp`
   already asserts for PMX). `ImportPmxMaterialTextures()` itself needs no
   signature change — it already early-returns immediately when
   `materials.textures.empty()` (true for every STL import), so calling it
   unconditionally from the shared helper is safe and correct for both
   formats.

3. Rewrite the mesh-import branch's top level to dispatch by extension:

   ```cpp
   if (IsImportableAsMeshAsset(extension)) {
       if (extension == ".pmx") {
           PmxLoadResult loaded = LoadPmxModel(PathToUtf8(sourcePath));
           if (loaded.success) {
               RigFileData rig;
               rig.skinWeights = loaded.mesh.skinWeights;
               rig.skeleton = loaded.skeleton;
               rig.morphs = loaded.morphs;
               rig.physics = loaded.physics;
               rig.materials = loaded.materials;
               return FinalizeMeshAssetImport(database, std::move(loaded.mesh), std::move(rig), sourcePath,
                   preferredDestinationPath, MeshSourceFormat::Pmx, "PMX");
           }
           return ImportAsPlainCopy(sourcePath, preferredDestinationPath,
               "Could not parse as a mesh, imported as-is instead. ");
       }

       if (extension == ".stl") {
           StlLoadResult loaded = LoadStlModel(PathToUtf8(sourcePath));
           if (loaded.success) {
               // An STL carries no skeleton/morphs/physics/materials at all
               // - rig stays entirely default-constructed (all-empty),
               // which RigFile.h's own doc comment already documents as a
               // normal, well-formed, successful case (see
               // PHASE0_MASTER_STRATEGY.md's own "What's genuinely missing"
               // discussion).
               RigFileData rig;
               return FinalizeMeshAssetImport(database, std::move(loaded.mesh), std::move(rig), sourcePath,
                   preferredDestinationPath, MeshSourceFormat::Stl, "STL");
           }
           return ImportAsPlainCopy(sourcePath, preferredDestinationPath,
               "Could not parse as a mesh, imported as-is instead. ");
       }
   }
   ```

   (The exact variable names/control flow above should match this codebase's
   existing style in the surrounding function — adjust cosmetically as
   needed, but the DISPATCH SHAPE and the "PMX branch behaves identically to
   before" requirement are both load-bearing and must not drift.)

4. Update `IsImportableAsMeshAsset()`'s implementation itself:

   ```cpp
   bool IsImportableAsMeshAsset(const std::string& extensionLowercaseWithDot)
   {
       return extensionLowercaseWithDot == ".pmx" || extensionLowercaseWithDot == ".stl";
   }
   ```

5. Update `ImportAssetFile()`'s own top-of-file doc comment (in
   `AssetImporter.h`) to mention the new `.stl` branch alongside the existing
   numbered `.pmx`/`.vmd`/image list — keep the numbering/shape of that
   comment intact, just fold STL into item 1 (the mesh-import item) rather
   than adding a whole new numbered item, since it is the same gating
   predicate and the same overall "wrapped as a Mesh `*.gta`" outcome, only
   the specific parser differs.

### 3.3 — Regression safety (mandatory, not optional)

Before considering this phase done, re-run the FULL existing
`tests/Assets/AssetImporterTests.cpp` suite unchanged and confirm every
existing `.pmx`/`.vmd`/image-related test (e.g.
`ConvertsAValidPmxToMeshWrappedGta`,
`ReimportingAPmxPreservesAnAlreadySavedJointPhysicsOverridesList`,
`ConvertedMeshAssetsMetadataDecodesBackToItsRigData`, etc.) STILL passes with
identical assertions, unmodified. This is the direct verification for the
master strategy's Risk Register entry: extracting `FinalizeMeshAssetImport()`
must not change PMX behavior in any observable way.

### 3.4 — New/updated tests in `tests/Assets/AssetImporterTests.cpp`

Add, following the exact same hand-built-fixture style already used in this
file (see `BuildMinimalTrianglePmx()`/`BuildMinimal2x2Bmp()` for the pattern —
add an equivalent small `BuildMinimalOneTriangleBinaryStl()` local helper
building a 134-byte binary STL fixture, reusing `PmxU8`/`PmxU32`/`PmxF32`'s
already-existing byte-writing helpers in this same file rather than
duplicating them again):

- `TEST(IsImportableAsMeshAssetTest, RecognizesStl)` — `EXPECT_TRUE(IsImportableAsMeshAsset(".stl"))`.
- Extend the existing `TEST(IsImportableAsMeshAssetTest,
  RejectsNonMeshExtensions)` is left as-is (it already only lists formats
  that should stay rejected; `.stl` moving to "accepted" doesn't touch it).
- `TEST_F(AssetImporterTest, ConvertsAValidStlToMeshWrappedGta)` — build the
  minimal binary STL fixture, call `ImportAssetFile()`, assert `success`,
  `convertedToMeshAsset`, `meshSourceFormat == MeshSourceFormat::Stl`,
  `finalPath.extension() == ".gta"`, `guid.IsValid()`, `meshVertexCount ==
  3`, `meshTriangleCount == 1`, `skinnedVertexCount == 0`, `boneCount == 0`,
  `morphCount == 0`, `rigidBodyCount == 0`, `jointCount == 0`,
  `materialCount == 0`, `textureCount == 0` — the STL-equivalent of the
  existing `ConvertsAValidPmxToMeshWrappedGta` test, deliberately asserting
  every rig-derived count is zero (a normal, successful, riggless/
  materialless import, matching `RigFile.h`'s own documented convention).
- `TEST_F(AssetImporterTest, ConvertedStlMeshAssetsMetadataDecodesBackToAnEmptyRig)`
  — mirrors `ConvertedMeshAssetsMetadataDecodesBackToItsRigData`, asserting
  `rig->skinWeights.empty()`, `rig->skeleton.bones.empty()`,
  `rig->morphs.morphs.empty()`, `rig->physics.rigidBodies.empty()`,
  `rig->materials.materials.empty()`.
- `TEST_F(AssetImporterTest, ConvertedStlMeshAssetIsImmediatelyTrackedByTheDatabase)`
  — mirrors `ConvertedMeshAssetIsImmediatelyTrackedByTheDatabase`, asserting
  `record->type == AssetType::Mesh`.
- `TEST_F(AssetImporterTest, CorruptStlExtensionFallsBackToPlainCopy)` —
  mirrors `CorruptPmxExtensionFallsBackToPlainCopy` exactly (a file named
  `fake.stl` containing plain non-STL text) — assert `success`,
  `!convertedToMeshAsset`, plain-copy `finalPath`.
- `TEST_F(AssetImporterTest, PmxImportStillReportsMeshSourceFormatPmx)` — a
  small, explicit regression assertion added to (or alongside) the existing
  `ConvertsAValidPmxToMeshWrappedGta` test: `EXPECT_EQ(result.meshSourceFormat,
  MeshSourceFormat::Pmx)` — the direct proof the new field doesn't
  accidentally default/leak the wrong value for the pre-existing format.

### 3.5 — Build & verify

- `cmake --build build` succeeds.
- `ctest -C Debug --output-on-failure` (from `build`) passes — both the new
  STL-import tests AND every pre-existing PMX/VMD/image test, unchanged.
- Manual sanity check (optional but recommended, since a real fixture is
  available on this machine): run the headless CLI against the real
  reference asset and confirm it reports success —

  ```
  <build output dir>\GreatTamanaEngine.exe --reimport ^
      "C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\_reference\pl-sky\assets\terrain.stl" ^
      "C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build\ManualVerification\terrain.gta"
  ```

  (delete the throwaway `ManualVerification` output afterwards — it is a
  large, disposable artifact, not something to commit).
- Write `PHASE2_COMPLETION_REPORT.md` (this same folder) summarizing the
  change, the regression-safety confirmation from Step 3.3, and the manual
  CLI check's outcome if performed.
- `git add` + `git commit` the changed/added files together with the report.
