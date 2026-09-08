# PHASE1 (v2) — `*.gta` Metadata Data Model for Persisted Joint Physics Overrides

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
depends on: nothing (first phase)
required by: `PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md`,
`PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md`,
`PHASE4_EDITOR_SAVE_TO_ASSET_PERSISTENCE.md`

> **v2 change from v1:** everything in Steps 1-3.8 below is unchanged from v1
> (re-verified against the current source tree; nothing needed correcting).
> **New: Step 3.9**, which closes a real data-loss gap v1 did not cover —
> re-importing an already-tuned model's source `.pmx` would otherwise
> silently discard every previously-saved joint override (see
> `PHASE0_MASTER_STRATEGY.md`, Step 2.6, for the full analysis). Step 3.8's
> test list also gained one new case for this.

---

## Step 1: The Goal

Give the engine a new, permanent on-disk record type,
`JointPhysicsOverride { boneIndex, damping, stiffness, mass }`, that a Mesh
`*.gta` file's existing METADATA section (owned by `RigFileData`, see
`PHASE0_MASTER_STRATEGY.md`, Step 2.2) can carry a list of — and make sure that
list survives an `EncodeRigDataToBytes()` -> `DecodeRigDataFromBytes()` round
trip, is threaded all the way through to `SkinnedMeshData`
(`src/Game/Animation/SkeletalRigCache.h`), and — critically — does this
**without invalidating a single already-imported model's existing `*.gta`
file**, and **without a re-import of that same model ever silently discarding
a previously-saved override list** (v2, Step 3.9). Nothing in this phase
changes what the engine actually simulates; it only makes the data plumbing
exist and be tested in isolation, exactly the "pure data model first, wiring
later" discipline this codebase already follows elsewhere (e.g. `DrawStats.h`
before `FrameRecorder`, `GpuTiming.h` before `GpuTimingService` — see
`AGENTS.md`).

## Step 2: The Situation

- `src/Assets/PhysicsData.h` defines `RigidBody`/`Joint`/`PhysicsData` — the
  RAW, imported-from-PMX rigid-body/joint data. It has no concept of a
  "user-tuned override" today.
- `src/Assets/RigFile.h`/`.cpp` defines `RigFileData` and its
  `EncodeRigDataToBytes()`/`DecodeRigDataFromBytes()` pair, magic
  `"GTERIG03"` (`kRigFileMagic`). The encoder is a flat, ordered sequence of
  writes; the decoder (`BinaryReader`) is a "sticky failure" reader: once it
  runs past the end of the byte buffer it silently zero-fills every further
  read and `Ok()` latches `false` — but **it never checks whether every byte
  was consumed**, meaning trailing, un-read bytes at the end of a buffer are
  silently ignored today and always have been.
- `src/Game/Animation/SkeletalRigCache.h` defines `SkinnedMeshData` (currently:
  `bindPositions`/`bindNormals`/`uvs`/`skinWeights`/`skeleton`/
  `physics: std::optional<PhysicsData>`), populated by
  `src/Game/Instantiation/MeshAssetGpuCatalog.cpp`'s `EnsureMeshAsset()`.
- `tests/Assets/RigFileTests.cpp` already exercises the full encode/decode
  round trip, including deliberate corruption/truncation tests — the pattern
  to extend, not replace.
- **(v2)** `src/Assets/AssetImporter.cpp`'s `ImportAssetFile()` (the `.pmx`
  import branch, lines 173-228) is the ONE place a `RigFileData` is
  constructed FRESH, from nothing but the just-reparsed `.pmx` file, and
  written unconditionally over whatever `*.gta` may already exist at the
  destination path:
  ```cpp
  RigFileData rig;
  rig.skinWeights = loaded.mesh.skinWeights;
  rig.skeleton = loaded.skeleton;
  rig.morphs = loaded.morphs;
  rig.physics = loaded.physics;
  rig.materials = loaded.materials;
  const std::vector<std::uint8_t> metadata = EncodeRigDataToBytes(rig);
  const std::optional<Guid> guid = database.ImportAsset(gtaPath, AssetType::Mesh, metadata, payload);
  ```
  `AssetDatabase::ImportAsset()` (`AssetDatabase.cpp`, line 116) already
  establishes the precedent for "preserve something the freshly-reparsed
  source file can never itself supply, across a re-import" — it reuses an
  EXISTING destination file's own `Guid` rather than generating a new one:
  ```cpp
  Guid guid = Guid::Generate();
  // Reuse an existing asset's Guid when overwriting it in place, so any
  // existing scene cross-reference to it survives a re-import.
  if (const std::optional<GtaHeader> existing = ReadGtaHeader(destinationGtaPath); existing.has_value()) {
      guid = existing->Id();
  }
  ```
  `jointPhysicsOverrides` (this phase's new field) is EXACTLY this same kind
  of data: engine-owned, user-authored, with no PMX equivalent whatsoever
  (see `JointPhysicsOverride`'s own doc comment, 3.1 below) — a freshly
  reparsed `.pmx` can never regenerate it, so it must be explicitly carried
  forward the same way `Guid` already is.
- `tests/Assets/AssetImporterTests.cpp` already has a `AssetImporterTest`
  fixture and several `ConvertsAValidPmxToMeshWrappedGta`-style tests to
  extend (see 3.9 below).

## Step 3: The Plan

### 3.1 — New record type: `src/Assets/PhysicsData.h`

Add, right after the existing `Joint` struct (so it reads as "the physics
setup, then the persisted user tuning on top of it"):

```cpp
// task_manager/verlet-integration-11, PHASE1 - a single joint's SAVED, USER-
// EDITED tuning (damping/stiffness/mass), keyed by SKELETON BONE INDEX rather
// than by chain/joint-in-chain position - the latter is recomputed fresh by
// DetectDynamicChains() on every load and is NOT a stable identity across
// engine versions/algorithm changes, while a bone index is stable for the
// lifetime of the imported skeleton (see PHASE0_MASTER_STRATEGY.md, Step 2.4).
// This is DELIBERATELY separate from RigidBody/Joint above: those are RAW,
// read-only, imported-from-PMX data; this is engine-owned, user-authored
// tuning data with no PMX equivalent at all - never populated by PmxLoader.h,
// only ever by a human editing DynamicJointSettings sliders in the Editor and
// clicking "Save Joint Physics to Asset" (see
// Game/Physics/DynamicChainPhysicsPersistence.h, PHASE4), EXCEPT for one
// deliberate carry-forward: AssetImporter.cpp's own re-import path (PHASE1,
// 3.9) copies an ALREADY-SAVED list forward across a re-import of the SAME
// destination path, exactly mirroring how AssetDatabase::ImportAsset()
// already preserves an existing file's own Guid across a re-import.
struct JointPhysicsOverride {
    std::int32_t boneIndex = -1; // SkeletonData::bones index - matches DynamicChainDefinition::jointBoneIndices entries.
    float damping = 0.4f;        // Mirrors DynamicJointSettings::damping's own default (DynamicChainDefinition.h).
    float stiffness = 0.02f;     // Mirrors DynamicJointSettings::stiffness's own default.
    float mass = 1.0f;           // Mirrors DynamicJointSettings::mass's own default.
};
```

Note this struct intentionally does NOT include `group`/`collisionMask`/
`collisionRadius` (derived from PMX rigid-body shape, read-only in the
Inspector - see `DynamicJointSettings`'s own doc comment) or a per-chain
`collisionEnabled` flag (see `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT
Do").

### 3.2 — Extend `RigFileData`: `src/Assets/RigFile.h`

```cpp
struct RigFileData {
    std::vector<VertexSkinWeights> skinWeights;
    SkeletonData skeleton;
    MorphData morphs;
    PhysicsData physics;
    MaterialData materials;

    // task_manager/verlet-integration-11, PHASE1 - user-saved per-joint
    // damping/stiffness/mass overrides (see JointPhysicsOverride's own doc
    // comment, PhysicsData.h). ALWAYS encoded as a trailing, OPTIONAL section
    // (see EncodeRigDataToBytes()/DecodeRigDataFromBytes()'s own updated doc
    // comment below for the exact backward/forward-compatibility mechanism) -
    // empty for a model that has never been saved with any joint edits
    // (every model imported before this phase, and every model a user has
    // simply never clicked "Save Joint Physics to Asset" for).
    std::vector<JointPhysicsOverride> jointPhysicsOverrides;
};
```

Update the big on-disk-layout doc comment above `kRigFileMagic` to append a
final bullet describing the new trailing section (see 3.4 below for the exact
wording to use, matching the existing bullet style).

### 3.3 — Encode/decode: `src/Assets/RigFile.cpp`

Add a small writer/reader pair, following `WritePhysics()`/`ReadPhysics()`'s
own exact shape:

```cpp
void WriteJointPhysicsOverrides(BinaryWriter& w, const std::vector<JointPhysicsOverride>& overrides)
{
    w.U32(static_cast<std::uint32_t>(overrides.size()));
    for (const auto& o : overrides) {
        w.I32(o.boneIndex);
        w.F32(o.damping);
        w.F32(o.stiffness);
        w.F32(o.mass);
    }
}

bool ReadJointPhysicsOverrides(BinaryReader& r, std::vector<JointPhysicsOverride>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        JointPhysicsOverride o;
        o.boneIndex = r.I32();
        o.damping = r.F32();
        o.stiffness = r.F32();
        o.mass = r.F32();
        out->push_back(o);
    }
    return r.Ok();
}
```

Wire into `EncodeRigDataToBytes()` — append UNCONDITIONALLY, after
`WriteMaterialData()`:

```cpp
std::vector<std::uint8_t> EncodeRigDataToBytes(const RigFileData& rig)
{
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), kRigFileMagic, kRigFileMagic + sizeof(kRigFileMagic));

    BinaryWriter w(bytes);
    WriteSkinWeights(w, rig.skinWeights);
    WriteBones(w, rig.skeleton.bones);
    WriteMorphs(w, rig.morphs.morphs);
    WritePhysics(w, rig.physics);
    WriteMaterialData(w, rig.materials);
    WriteJointPhysicsOverrides(w, rig.jointPhysicsOverrides); // <-- NEW, always written (possibly count == 0)

    return bytes;
}
```

Wire into `DecodeRigDataFromBytes()` — **guarded**, so a buffer with no
trailing section at all (any file encoded before this phase) still decodes
successfully:

```cpp
std::optional<RigFileData> DecodeRigDataFromBytes(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < sizeof(kRigFileMagic)) { return std::nullopt; }
    if (std::memcmp(bytes.data(), kRigFileMagic, sizeof(kRigFileMagic)) != 0) { return std::nullopt; }

    BinaryReader r(bytes, sizeof(kRigFileMagic));

    RigFileData rig;
    if (!ReadSkinWeights(r, &rig.skinWeights)) { return std::nullopt; }
    if (!ReadBones(r, &rig.skeleton.bones)) { return std::nullopt; }
    if (!ReadMorphs(r, &rig.morphs.morphs)) { return std::nullopt; }
    if (!ReadPhysics(r, &rig.physics)) { return std::nullopt; }
    if (!ReadMaterialData(r, &rig.materials)) { return std::nullopt; }

    // task_manager/verlet-integration-11, PHASE1 - OPTIONAL trailing section:
    // only attempt this read if there is genuinely at least one more byte
    // left. A blob encoded by code that predates this phase ends exactly
    // here (r.Cursor() == bytes.size()) - leaving rig.jointPhysicsOverrides
    // at its default-constructed empty state is the CORRECT, intentional
    // "no saved overrides for this model" outcome, not a decode failure. Do
    // NOT make this an unconditional call like every read above it - an
    // unconditional ReadJointPhysicsOverrides() call against an old, already-
    // fully-consumed buffer would read U32() past the end, and while
    // BinaryReader's sticky-failure design makes that SAFE (it would just
    // return an empty vector with Ok() staying whatever it already was), it
    // would also incorrectly report a truncation-style failure via r.Ok()
    // becoming false the instant EnsureAvailable(4) fails with zero bytes
    // left - which would make DecodeRigDataFromBytes() return std::nullopt
    // for every single pre-existing *.gta file in the project. The cursor
    // check below is what makes this genuinely backward-compatible instead
    // of merely "backward-compatible unless you forget this exact guard."
    if (r.Cursor() < bytes.size()) {
        if (!ReadJointPhysicsOverrides(r, &rig.jointPhysicsOverrides)) {
            return std::nullopt; // A trailing section IS present but is itself truncated/corrupt - a real failure.
        }
    }

    return rig;
}
```

`BinaryReader` already exposes `Cursor()` (see the existing class) - no change
needed there.

### 3.4 — Update `RigFile.h`'s own on-disk-layout doc comment

Append one more bullet to the big comment above `kRigFileMagic`, matching its
existing style exactly:

```
//   uint32_t : joint physics override count (task_manager/verlet-integration-11,
//              PHASE1) - followed by that many fixed-size records (1 int32
//              bone index + 3 floats: damping, stiffness, mass). OPTIONAL -
//              a blob with no bytes left after materials decodes with this
//              list empty rather than failing; this is what lets a *.gta
//              written by any version of the engine PRIOR to this phase keep
//              decoding correctly forever, with no magic bump required (see
//              DecodeRigDataFromBytes()'s own comment for the exact
//              mechanism this relies on).
```

### 3.5 — Compatibility analysis (why no magic bump, unlike `"GTERIG02"` -> `"GTERIG03"`)

The prior `"GTERIG02"` -> `"GTERIG03"` bump (see `RigFile.h`'s own comment) was
required because that change inserted a NEW field (a `Guid`) into the MIDDLE
of an existing, already-fixed-shape per-texture record — every byte AFTER that
insertion point shifts, so an old reader parsing a new blob (or vice versa)
would misinterpret every subsequent field. **This phase's change is
categorically different**: it only ever APPENDS a whole new, independently
length-prefixed section strictly AFTER every existing section, changing
nothing about the shape/meaning of any byte that already existed. This gives
two independent compatibility guarantees, both load-bearing and both to be
covered by PHASE1's own tests:

1. **Backward compatibility (old file, new code):** a `*.gta` written by any
   engine build before this phase has no trailing bytes after its materials
   section. The new decoder's cursor check (`r.Cursor() < bytes.size()`) sees
   `false` and simply leaves `jointPhysicsOverrides` empty — full decode
   success, identical behavior to before this phase existed.
2. **Forward compatibility (new file, old code):** a `*.gta` re-saved by THIS
   phase's own encoder (or by PHASE4's save path) carries extra trailing bytes
   an OLD decoder was never written to look for. Because `BinaryReader` never
   asserts "every byte was consumed" (see 2.2 above), an old decoder reading a
   new file simply stops reading after materials and returns successfully,
   completely unaware the extra bytes exist. (This property is theoretical/
   defensive only — this codebase does not actually ship multiple engine
   binary versions against the same asset folder today — but it comes for
   free from this design and costs nothing to state and test.)

Because of both guarantees, `kGtaCurrentVersion` (`GtaFile.h`) and
`kRigFileMagic` (`RigFile.h`) both stay UNCHANGED by this phase — this is a
genuinely, provably backward-compatible format change per `RigFile.h`'s own
documented bump policy ("bump... the moment the format's layout/meaning
changes in a way that isn't backward compatible" — this change explicitly
does not).

### 3.6 — Thread the new data through `SkinnedMeshData`

`src/Game/Animation/SkeletalRigCache.h`:

```cpp
struct SkinnedMeshData {
    std::vector<Vec3> bindPositions;
    std::vector<Vec3> bindNormals;
    std::vector<Vec2> uvs;
    std::vector<VertexSkinWeights> skinWeights;
    SkeletonData skeleton;
    std::optional<PhysicsData> physics;

    // task_manager/verlet-integration-11, PHASE1 - this model's SAVED
    // per-joint damping/stiffness/mass overrides (JointPhysicsOverride,
    // Assets/PhysicsData.h), copied verbatim from the *.gta's own
    // RigFileData::jointPhysicsOverrides (see MeshAssetGpuCatalog.cpp's
    // EnsureMeshAsset()). Empty for a model that has never been saved with
    // any joint edits - PhysicsSystem::RegisterDynamicChains() (PHASE2)
    // treats an empty list as a pure no-op, applying nothing on top of
    // DetectDynamicChains()'s own defaults, exactly like `physics` being
    // std::nullopt already does for RigidBody-seeded defaults above.
    std::vector<JointPhysicsOverride> jointPhysicsOverrides;
};
```

(`SkeletalRigCache.h` already `#include`s `"../../Assets/PhysicsData.h"` for
`PhysicsData` — `JointPhysicsOverride` is therefore already visible with zero
new includes.)

### 3.7 — Populate it: `src/Game/Instantiation/MeshAssetGpuCatalog.cpp`

Inside `EnsureMeshAsset()`'s existing `if (skinned) { ... }` block (the one
that already sets `skinData.physics = rig->physics;`), add one line:

```cpp
if (skinned) {
    SkinnedMeshData skinData;
    skinData.bindPositions = mesh->positions;
    // ... (unchanged existing lines) ...
    skinData.skinWeights = rig->skinWeights;
    skinData.skeleton = rig->skeleton;
    skinData.physics = rig->physics;
    skinData.jointPhysicsOverrides = rig->jointPhysicsOverrides; // <-- NEW (PHASE1)
    m_skinnedMeshCache.insert_or_assign(absoluteGtaPath, std::move(skinData));
}
```

Note `rig` is a `std::optional<RigFileData>` already proven `has_value()` by
this point (guarded by the surrounding `skinned` condition, which itself
already requires `rig.has_value()`) — no new null-check needed.

### 3.8 — Tests: `tests/Assets/RigFileTests.cpp`

1. **Extend `BuildSampleRigData()`** to also populate
   `rig.jointPhysicsOverrides` with at least two entries (distinct
   `boneIndex`/`damping`/`stiffness`/`mass` values), and extend
   `EncodeThenDecodeRoundTripsSkinWeightsBonesMorphsAndPhysics` (or add a
   sibling test) to assert `decoded->jointPhysicsOverrides` matches exactly,
   field-for-field, in order.
2. **New test — backward compatibility with a pre-PHASE1-shaped blob:**
   ```cpp
   TEST(RigFileTest, DecodeSucceedsOnABlobWithNoTrailingJointOverrideSection)
   {
       RigFileData rig = BuildSampleRigData();
       rig.jointPhysicsOverrides.clear();
       std::vector<std::uint8_t> encoded = EncodeRigDataToBytes(rig);
       // Chop off exactly the trailing "count == 0" u32 this phase's own
       // encoder just wrote, simulating a blob produced by code that
       // predates this phase entirely (never wrote ANY trailing bytes).
       ASSERT_GE(encoded.size(), 4u);
       encoded.resize(encoded.size() - 4);

       const std::optional<RigFileData> decoded = DecodeRigDataFromBytes(encoded);
       ASSERT_TRUE(decoded.has_value());
       EXPECT_TRUE(decoded->jointPhysicsOverrides.empty());
       // Also spot-check that everything ELSE still decoded correctly -
       // proves the cursor-guard didn't accidentally swallow/misread any
       // earlier section.
       EXPECT_EQ(decoded->materials.materials.size(), rig.materials.materials.size());
   }
   ```
3. **New test — a genuinely-present-but-truncated trailing section IS still a
   real failure** (distinguishes "no section at all" from "a corrupt
   section"):
   ```cpp
   TEST(RigFileTest, DecodeFailsWhenJointOverrideSectionIsPresentButTruncated)
   {
       RigFileData rig = BuildSampleRigData();
       rig.jointPhysicsOverrides.push_back(JointPhysicsOverride{ 0, 0.1f, 0.2f, 0.3f });
       std::vector<std::uint8_t> encoded = EncodeRigDataToBytes(rig);
       encoded.resize(encoded.size() - 2); // Mid-way through the last float.

       EXPECT_FALSE(DecodeRigDataFromBytes(encoded).has_value());
   }
   ```
4. **Extend `EncodesAllEmptyRigDataAsAHeaderOnlyBlobThatDecodesBackToEmpty`**
   to also assert `decoded->jointPhysicsOverrides.empty()`.

No other existing test in this file needs to change — every existing
assertion continues to pass unmodified (this is the whole point of the
backward-compatible design in 3.5).

### 3.9 — NEW in v2: preserve `jointPhysicsOverrides` across a `.pmx` re-import: `src/Assets/AssetImporter.cpp`

**Why this step exists:** without it, re-importing an already-tuned model's
source `.pmx` file (a completely ordinary workflow — fixing an unrelated
mesh/texture issue and re-dragging the same file in) would silently discard
every joint override a user had previously saved via PHASE4's "Save Joint
Physics to Asset" button, with zero warning. See
`PHASE0_MASTER_STRATEGY.md`, Step 2.6, for the full failure analysis, and note
this fix applies the EXACT SAME pattern `AssetDatabase::ImportAsset()`
already uses one function away to preserve an existing file's `Guid` across a
re-import — this is not a new idiom for this codebase, just an omission this
phase corrects for the one new field it introduces.

In `ImportAssetFile()`'s `.pmx` branch (`src/Assets/AssetImporter.cpp`), BEFORE
building the fresh `RigFileData` (i.e. before the existing `RigFileData rig;`
line), read whatever `jointPhysicsOverrides` may already exist at the
destination path:

```cpp
if (IsImportableAsMeshAsset(extension)) {
    PmxLoadResult loaded = LoadPmxModel(PathToUtf8(sourcePath));
    if (loaded.success) {
        std::filesystem::path gtaPath = preferredDestinationPath;
        gtaPath.replace_extension(".gta");

        // task_manager/verlet-integration-11, PHASE1 (3.9) - a re-import of
        // an ALREADY-imported model must not silently discard a user's
        // previously-saved joint physics tuning (Editor "Save Joint Physics
        // to Asset", PHASE4) just because the freshly-reparsed .pmx itself
        // obviously carries none of that engine-only, non-PMX data (see
        // JointPhysicsOverride's own doc comment, Assets/PhysicsData.h -
        // "never populated by PmxLoader.h"). Mirrors exactly how the
        // ImportAsset() call below already preserves an EXISTING
        // destination file's own Guid across a re-import (see
        // AssetDatabase.cpp's own comment on that) - read BEFORE this
        // function's own WriteGtaFile() call (via database.ImportAsset()
        // below) overwrites the file, using the SAME gtaPath a moment later.
        // Empty (never touched) for a brand-new import - nothing exists yet
        // to preserve, which is the correct, intentional "no saved overrides
        // yet" starting state for any freshly-imported model.
        std::vector<JointPhysicsOverride> preservedJointPhysicsOverrides;
        if (const std::optional<GtaFileData> existingGta = ReadGtaFile(gtaPath);
            existingGta.has_value() && existingGta->header.Type() == AssetType::Mesh) {
            if (const std::optional<RigFileData> existingRig = DecodeRigDataFromBytes(existingGta->metadata);
                existingRig.has_value()) {
                preservedJointPhysicsOverrides = existingRig->jointPhysicsOverrides;
            }
        }

        ImportPmxMaterialTextures(database, loaded.materials, gtaPath);

        const std::vector<std::uint8_t> payload = EncodeMeshDataToBytes(loaded.mesh);

        RigFileData rig;
        rig.skinWeights = loaded.mesh.skinWeights;
        rig.skeleton = loaded.skeleton;
        rig.morphs = loaded.morphs;
        rig.physics = loaded.physics;
        rig.materials = loaded.materials;
        rig.jointPhysicsOverrides = std::move(preservedJointPhysicsOverrides); // <-- NEW (PHASE1, 3.9)
        const std::vector<std::uint8_t> metadata = EncodeRigDataToBytes(rig);

        const std::optional<Guid> guid = database.ImportAsset(gtaPath, AssetType::Mesh, metadata, payload);
        // ... (unchanged below)
```

Notes on exactly why this is safe and correctly scoped:

- **Deliberately silent/best-effort, never a hard failure.** If `gtaPath`
  doesn't exist yet (a brand-new import), or exists but isn't a Mesh asset,
  or exists but its metadata fails to decode for any reason, this simply
  leaves `preservedJointPhysicsOverrides` empty and proceeds exactly as v1's
  plan already did — this step can only ever ADD preserved data, never turn a
  previously-succeeding import into a failing one.
- **No new include needed**: `AssetImporter.cpp` already includes
  `Assets/GtaFile.h` (for `ReadGtaFile()`/`AssetType`) and `Assets/RigFile.h`
  (for `RigFileData`/`DecodeRigDataFromBytes()`/`EncodeRigDataToBytes()`) —
  confirm both are present; if either is missing, add it (both are small,
  already-project-standard headers with no new dependency implications).
- **Does not affect texture/mesh/skeleton/morph/material re-import
  behavior at all** — every other field of the freshly-built `rig` is still
  populated purely from `loaded` (the freshly reparsed `.pmx`), exactly as
  before this step. Only `jointPhysicsOverrides` is carried forward from the
  OLD file.
- **Interacts correctly with a re-import that changes the skeleton itself**
  (e.g. a `.pmx` re-export with renumbered/added/removed bones): a preserved
  override whose `boneIndex` no longer matches any joint of any chain after
  re-detection is silently ignored by `ApplyJointPhysicsOverrides()` (PHASE2)
  — the exact same "stale override, no match, skip" contract that already
  covers every other reason an override might go stale (see PHASE2's own doc
  comment). No special-casing needed here for that scenario.

**New test — `tests/Assets/AssetImporterTests.cpp`:**

```cpp
TEST_F(AssetImporterTest, ReimportingAPmxPreservesAnAlreadySavedJointPhysicsOverridesList)
{
    const std::filesystem::path source = m_root / "model.pmx";
    WriteBinaryFile(source, BuildMinimalTrianglePmx());
    const std::filesystem::path destination = m_root / "model.pmx"; // Same path both imports below target.

    // --- First import: establishes the *.gta with NO saved overrides yet. ---
    const AssetImportResult firstResult = ImportAssetFile(m_db, source, destination);
    ASSERT_TRUE(firstResult.success) << firstResult.message;

    // --- Simulate a user having saved joint physics via the Editor (PHASE4)
    // sometime after the first import, by directly mutating the *.gta's own
    // RigFileData the same way SaveJointPhysicsOverridesToGtaFile() would. ---
    {
        const std::optional<GtaFileData> gta = ReadGtaFile(firstResult.finalPath);
        ASSERT_TRUE(gta.has_value());
        std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
        ASSERT_TRUE(rig.has_value());
        rig->jointPhysicsOverrides.push_back(JointPhysicsOverride{ 0, 0.77f, 0.06f, 4.5f });
        const std::vector<std::uint8_t> newMetadata = EncodeRigDataToBytes(*rig);
        ASSERT_TRUE(WriteGtaFile(firstResult.finalPath, gta->header.Type(), gta->header.Id(),
            gta->header.Flags(), newMetadata, gta->payload, gta->header.version));
    }

    // --- Re-import the SAME source .pmx onto the SAME destination path -
    // this must NOT wipe out the override just written above. ---
    const AssetImportResult secondResult = ImportAssetFile(m_db, source, destination);
    ASSERT_TRUE(secondResult.success) << secondResult.message;
    EXPECT_EQ(secondResult.finalPath, firstResult.finalPath);

    const std::optional<GtaFileData> gtaAfter = ReadGtaFile(secondResult.finalPath);
    ASSERT_TRUE(gtaAfter.has_value());
    const std::optional<RigFileData> rigAfter = DecodeRigDataFromBytes(gtaAfter->metadata);
    ASSERT_TRUE(rigAfter.has_value());
    ASSERT_EQ(rigAfter->jointPhysicsOverrides.size(), 1u);
    EXPECT_EQ(rigAfter->jointPhysicsOverrides[0].boneIndex, 0);
    EXPECT_FLOAT_EQ(rigAfter->jointPhysicsOverrides[0].damping, 0.77f);
    EXPECT_FLOAT_EQ(rigAfter->jointPhysicsOverrides[0].mass, 4.5f);
}

TEST_F(AssetImporterTest, FirstTimeImportOfAPmxHasNoJointPhysicsOverridesToPreserve)
{
    const std::filesystem::path source = m_root / "model.pmx";
    WriteBinaryFile(source, BuildMinimalTrianglePmx());

    const AssetImportResult result = ImportAssetFile(m_db, source, m_root / "model.pmx");
    ASSERT_TRUE(result.success) << result.message;

    const std::optional<GtaFileData> gta = ReadGtaFile(result.finalPath);
    ASSERT_TRUE(gta.has_value());
    const std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    ASSERT_TRUE(rig.has_value());
    EXPECT_TRUE(rig->jointPhysicsOverrides.empty()); // Nothing to preserve on a brand-new import - not a regression.
}
```

(`tests/Assets/AssetImporterTests.cpp` already `#include`s `Assets/GtaFile.h`
and `Assets/RigFile.h` per its own existing `ConvertedMeshAssetsMetadataDecodesBackToItsRigData`
test at line 351 — no new test-file includes needed.)

### What This Phase Deliberately Does NOT Do

- Does not call `ApplyJointPhysicsOverrides()` anywhere (that function does
  not exist until PHASE2) — `SkinnedMeshData::jointPhysicsOverrides` is
  populated but **unused** at the end of this phase; that's expected and
  correct for a "data model first" phase.
- Does not add any Editor UI (PHASE4).
- Does not modify `CMakeLists.txt` — this phase only edits existing `.h`/
  `.cpp` files that are already registered as build sources; no new files are
  introduced yet (`JointPhysicsOverride` lives inside the ALREADY-registered
  `PhysicsData.h`, a header-only addition, and 3.9 only edits the
  already-registered `AssetImporter.cpp`).
- **(v2)** Does not attempt to fix the SEPARATE "same-session cache
  staleness" gap (`PHASE0_MASTER_STRATEGY.md`, Step 2.5) — that is a
  completely independent problem (in `MeshAssetGpuCatalog`'s spawn-time
  caching, not in the on-disk format or the import path) and is handled
  entirely by PHASE3.
