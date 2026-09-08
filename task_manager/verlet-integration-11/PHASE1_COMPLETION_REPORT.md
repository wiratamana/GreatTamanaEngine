# PHASE1 Completion Report — `*.gta` Metadata Data Model for Persisted Joint Physics Overrides

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
phase: `PHASE1_GTA_METADATA_JOINT_OVERRIDE_DATA_MODEL.md`
status: **DONE** — compiles cleanly (`gte_core` + `GreatTamanaEngineTests`), ready for PHASE2.

## What was implemented

Followed the phase strategy document (`PHASE1_GTA_METADATA_JOINT_OVERRIDE_DATA_MODEL.md`)
step by step:

1. **`src/Assets/PhysicsData.h`** — added the new `JointPhysicsOverride` struct
   (`boneIndex`/`damping`/`stiffness`/`mass`), right after the existing
   `PhysicsData` struct, with the exact doc comment the strategy document
   specifies (explaining why it's keyed by skeleton bone index rather than
   chain/joint-in-chain position, and why it's separate from the raw,
   PMX-imported `RigidBody`/`Joint` data).
2. **`src/Assets/RigFile.h`** — added `std::vector<JointPhysicsOverride>
   jointPhysicsOverrides` to `RigFileData`, and appended the new trailing-
   section bullet to the on-disk-layout doc comment above `kRigFileMagic`
   (magic stays `"GTERIG03"` — unchanged, per the compatibility analysis).
3. **`src/Assets/RigFile.cpp`** — added `WriteJointPhysicsOverrides()`/
   `ReadJointPhysicsOverrides()` (mirroring `WritePhysics()`/`ReadPhysics()`'s
   own shape exactly), wired `WriteJointPhysicsOverrides()` unconditionally
   into `EncodeRigDataToBytes()` (always written, even when empty), and wired
   `ReadJointPhysicsOverrides()` into `DecodeRigDataFromBytes()` behind the
   `r.Cursor() < bytes.size()` guard — this is the load-bearing backward-
   compatibility mechanism: a pre-PHASE1 blob with no trailing bytes decodes
   successfully with an empty `jointPhysicsOverrides`, while a blob whose
   trailing section IS present but truncated/corrupt still correctly fails.
4. **`src/Game/Animation/SkeletalRigCache.h`** — added
   `jointPhysicsOverrides` to `SkinnedMeshData`, with the doc comment
   explaining PHASE2 will treat an empty list as a pure no-op.
5. **`src/Game/Instantiation/MeshAssetGpuCatalog.cpp`** — `EnsureMeshAsset()`
   now copies `rig->jointPhysicsOverrides` into `skinData.jointPhysicsOverrides`
   inside the existing `if (skinned) { ... }` block, right alongside the
   existing `skinData.physics = rig->physics;` line.
6. **`src/Assets/AssetImporter.cpp`** (step 3.9, the v2 addition) — the `.pmx`
   import branch of `ImportAssetFile()` now reads whatever
   `jointPhysicsOverrides` already exists at the destination `*.gta` path (if
   it exists and is a Mesh asset) **before** building the fresh `RigFileData`
   from the newly-reparsed `.pmx`, and carries that list forward via
   `rig.jointPhysicsOverrides = std::move(preservedJointPhysicsOverrides)`.
   This mirrors the exact same "preserve something the reparsed source can
   never regenerate" pattern `AssetDatabase::ImportAsset()` already uses for
   `Guid` across a re-import. Deliberately best-effort/never a hard failure —
   a brand-new import, a non-Mesh destination, or an undecodable existing
   file all just leave the preserved list empty and proceed exactly as
   before.

## Tests added/extended

- **`tests/Assets/RigFileTests.cpp`**:
  - `BuildSampleRigData()` extended with two `JointPhysicsOverride` entries.
  - `EncodeThenDecodeRoundTripsSkinWeightsBonesMorphsAndPhysics` extended to
    assert the round-tripped `jointPhysicsOverrides` match field-for-field.
  - `EncodesAllEmptyRigDataAsAHeaderOnlyBlobThatDecodesBackToEmpty` extended
    to assert `jointPhysicsOverrides.empty()`.
  - New: `DecodeSucceedsOnABlobWithNoTrailingJointOverrideSection` — proves
    backward compatibility with a pre-PHASE1-shaped blob (no trailing bytes
    at all).
  - New: `DecodeFailsWhenJointOverrideSectionIsPresentButTruncated` — proves
    a genuinely truncated trailing section is still a real decode failure,
    distinct from "no section at all".
- **`tests/Assets/AssetImporterTests.cpp`**:
  - New: `ReimportingAPmxPreservesAnAlreadySavedJointPhysicsOverridesList` —
    imports a `.pmx`, simulates a PHASE4-style save by directly mutating the
    written `*.gta`'s `RigFileData::jointPhysicsOverrides`, re-imports the
    same source file onto the same destination path, and asserts the saved
    override survives unchanged.
  - New: `FirstTimeImportOfAPmxHasNoJointPhysicsOverridesToPreserve` — a
    brand-new import has nothing to preserve, confirming this is not a
    regression for the ordinary case.

## Compile verification

Ran a fast, targeted build (not a full project build/regression run, per this
task's instructions):

```
cmake --build build --target gte_core                 -> SUCCESS (0 errors)
cmake --build build --target GreatTamanaEngineTests    -> SUCCESS (0 errors)
```

Both the engine core static library and the full test binary link cleanly.
Test *execution* (`ctest`) was intentionally not run this phase, matching
PHASE1's own "data model first, wiring later" scope and the workflow
instructions for this task (no full build/regression yet).

## Note on a self-caused/self-corrected tool hiccup

While editing `src/Assets/RigFile.h`, an `edit_line` call meant to append one
new bullet to the on-disk-layout doc comment used a `length` value computed
against a stale line count (the file had already grown from an earlier edit
in the same session), which clamped and silently dropped the file's own tail
(the `kRigFileMagic` constant, both function declarations, and the closing
`} // namespace gte`). This was caught immediately by the very next compile
check (a cascade of `gte::gte::`-qualified errors and "expected '}' at end of
input" pointing back at `RigFile.h`'s `namespace gte {`), and fixed by
appending the missing tail back. Re-verified the fix with a second clean
`gte_core` build. This was a self-made mistake corrected within the same
session, not a malfunctioning tool — `edit_line`'s own documented "safe to
over-estimate `length`" contract only holds when the file's line count is
re-checked immediately beforehand, which I failed to do that one time.

## What this phase deliberately does NOT do (unchanged from the strategy doc)

- Does not call `ApplyJointPhysicsOverrides()` anywhere — that function
  doesn't exist until PHASE2. `SkinnedMeshData::jointPhysicsOverrides` is
  populated but currently unused at runtime.
- Does not add any Editor UI (PHASE4).
- Does not modify `CMakeLists.txt` — every file touched this phase was
  already a registered build source.
- Does not touch the same-session cache-freshness gap (PHASE3) or the save
  path (PHASE4).

## Next step

**PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md** — add
`ApplyJointPhysicsOverrides()` (`src/Physics/JointPhysicsOverrideApplication.h/.cpp`)
and wire it into `PhysicsSystem::RegisterDynamicChains()` right after
`DetectDynamicChains()` returns.
