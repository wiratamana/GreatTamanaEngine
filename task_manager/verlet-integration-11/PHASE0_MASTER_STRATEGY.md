# PHASE0_MASTER_STRATEGY.md (v2) — Persisting User-Edited Dynamic-Chain Joint Physics Into the `*.gta` File

campaign: `verlet-integration-11`
orchestrator for: `PHASE1_GTA_METADATA_JOINT_OVERRIDE_DATA_MODEL.md`,
`PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md`,
`PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md`,
`PHASE4_EDITOR_SAVE_TO_ASSET_PERSISTENCE.md`,
`PHASE5_END_TO_END_ROUND_TRIP_REGRESSION_AND_BUILD_REGISTRATION.md`

---

> **v2 revision notice (this file replaces the v1 five-file set in place, same
> filenames where the topic is unchanged).** v1 was independently re-verified
> line-by-line against the actual source tree for this v2 pass (every quoted
> function signature, field name, line number, and file path below was
> re-checked against `src/` as it exists today — none needed correcting, v1's
> research was accurate). v2 exists because that verification surfaced **two
> real, user-visible functional gaps v1 did not cover** and **one unresolved
> implementation ambiguity** that an LLM-driven programmer should not be left
> to guess at. Concretely, v2:
> 1. Splits v1's PHASE3 ("Editor Save to Asset Persistence") into two phases:
>    a new PHASE3 ("Same-Session Cache Freshness Infrastructure") plus a
>    renamed PHASE4 (the old PHASE3's own content, now depending on the new
>    PHASE3) — see Step 2.5 below for exactly why this split is necessary,
>    not optional polish.
>    - **This is the single most important change in v2.** Without it, this
>      campaign's own literal, explicit "Definition of done" (v1's own words:
>      *"the next time ANY entity is spawned from that same `*.gta` path —
>      **later in the same session**, or after a full Editor/engine
>      restart"*) would silently NOT hold for the "later in the same
>      session" half — see Step 2.5.
> 2. Adds one new required step to PHASE1 (3.9: preserving
>    `jointPhysicsOverrides` across a `.pmx` **re-import** in
>    `AssetImporter.cpp`) — without it, the ordinary, expected workflow of
>    re-importing an already-tuned model (e.g. after fixing a UV seam in the
>    source `.pmx`) would silently DESTROY a user's previously-saved joint
>    tuning — see Step 2.6.
> 3. Resolves PHASE4's (v1's PHASE3's) one open "check before choosing"
>    design question — where the Editor's transient save-feedback state
>    lives — with a definite, verified answer instead of leaving it for the
>    implementer to decide mid-flight (see PHASE4's own Step 3.3).
> 4. Renames the old PHASE4 to PHASE5 (unchanged content otherwise, plus one
>    new test and one new cross-check item for the new PHASE3).
>
> Every phase below still stands on its own as a fully-specified, directly
> implementable unit — v2 is a **strategy correction**, not a rewrite of
> already-sound decisions (PHASE1's data model, PHASE2's apply-on-load
> function, and the shape of the save function itself all remain unchanged
> from v1).

---

## Step 1: The Goal (Where are we going?)

Today, `Panels/InspectorPanel.cpp`'s "Dynamic Chain Physics" section already lets
a user drag-edit a detected dynamic bone chain's per-joint `DynamicJointSettings`
(`damping` / `stiffness` / `mass`, see `src/Physics/DynamicChainDefinition.h`)
directly in the running Editor. Those edits are real and immediately affect the
live simulation — but they live **only** in `PhysicsSystem`'s in-memory
`DynamicChainRigCache` (`src/Game/Physics/DynamicChainRigCache.h`). The instant
the model's `*.gta` file is re-loaded (Editor restart, `RefreshFromDirectory()`,
spawning a fresh instance of the same model path from scratch, ...), every edit
is silently gone and the joints fall back to whatever
`DetectDynamicChains()`/`DynamicChainDetectionDefaults` compute from scratch.

**The goal of this campaign is to make those edits durable**, by writing them
into the model's own `*.gta` file (per the user's own request: "save the
information into `*.gta` file itself"), specifically into the METADATA section
that already sits between the 64-byte common header and the payload:

```
[ Header (64 bytes, fixed) ] [ Metadata ] [ Payload ]
```

and to make loading a model **read that same metadata back and apply it to the
live simulation at instantiation time** — i.e. exactly the flow the user
described: *"whenever the model got instantiated into scene, it will load the
metadata and apply it [at] actual runtime."*

Concretely, when this campaign is done:

1. A user drags "Damping"/"Stiffness"/"Weight (Mass)" sliders in the Inspector's
   "Dynamic Chain Physics" section (exactly as today — unchanged UX for the
   live-tuning part).
2. A NEW "Save Joint Physics to Asset" button in that same section writes the
   CURRENT live values of every joint of every chain belonging to that model
   back into the model's own `*.gta` file's metadata section, without touching
   the mesh geometry payload or any other metadata (skeleton/skin
   weights/morphs/materials) already stored there.
3. **The next time ANY entity is spawned from that same `*.gta` path — whether
   that is the very next spawn call in the SAME running session (e.g. a second
   copy of the same character dragged in right after saving), or after a full
   Editor/engine restart — the freshly-detected chain's joints reflect the
   saved values, not the original auto-detected defaults.** (v2: this is now
   PRECISELY true for both cases — see Step 2.5 for why v1's plan alone only
   guaranteed the "after a restart" half, and PHASE3/PHASE4 for the fix that
   makes the "same session" half hold too.)
4. Re-importing the SAME already-tuned model's source `.pmx` file again later
   (e.g. after fixing something unrelated in the mesh/materials) does not
   silently discard the user's previously-saved joint tuning (v2, Step 2.6 /
   PHASE1 3.9 — this did not hold under v1's plan and is now fixed).

This is **strictly additive, backward- and forward-compatible on-disk format
work plus a small, well-scoped set of runtime/Editor code changes** — no
rewrite of the existing dynamic-chain detection/simulation machinery, and no
change to the existing "edits apply to every entity spawned from the same
model path, never per-instance" scoping rule the Inspector section's own
header comment already documents (see
`PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md` of
`task_manager/verlet-integration-1`, "What We Will NOT Do").

---

## Step 2: The Situation (Where are we now?)

### 2.1 — The `*.gta` container format (already exists, untouched by this campaign)

`src/Assets/GtaFile.h`/`.cpp` already implements exactly the layout the user
described:

```cpp
#pragma pack(push, 1)
struct GtaHeader {
    char magic[16];          // "GREATTAMANAASSET"
    std::uint64_t assetType; // AssetType enum
    std::uint64_t version;
    std::uint64_t guidLow, guidHigh;
    std::uint64_t flags;
    std::uint64_t payloadOffset; // metadata ends / payload starts here
};
#pragma pack(pop)
static_assert(sizeof(GtaHeader) == 64, ...);

struct GtaFileData {
    GtaHeader header;
    std::vector<std::uint8_t> metadata; // bytes [64, payloadOffset)
    std::vector<std::uint8_t> payload;  // bytes [payloadOffset, EOF)
};

bool WriteGtaFile(path, type, guid, flags, metadata, payload, version);
std::optional<GtaFileData> ReadGtaFile(path);
```

Nothing here needs to change. `WriteGtaFile()` already accepts an arbitrary
`metadata` byte blob and computes `payloadOffset` automatically — this campaign
only needs to change WHAT bytes get put into that `metadata` vector for an
`AssetType::Mesh` asset, and read them back out the same way.

### 2.2 — What already lives in a Mesh `*.gta`'s metadata section

For `AssetType::Mesh`, the metadata section is **not** a free-form blob today —
it already has an owner: `src/Assets/RigFile.h`'s `RigFileData`, encoded via
`EncodeRigDataToBytes()` / decoded via `DecodeRigDataFromBytes()` (magic
`"GTERIG03"`):

```cpp
struct RigFileData {
    std::vector<VertexSkinWeights> skinWeights;
    SkeletonData skeleton;
    MorphData morphs;
    PhysicsData physics;   // RAW, imported PMX RigidBody/Joint data — never edited by a user
    MaterialData materials;
};
```

`src/Game/Instantiation/MeshAssetGpuCatalog.cpp`'s `EnsureMeshAsset()` is the
ONE place that reads a Mesh `*.gta`'s metadata at load time:

```cpp
const std::optional<GtaFileData> gta = ReadGtaFile(...);
std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
...
skinData.physics = rig->physics; // -> SkinnedMeshData::physics
```

`skinData` (a `SkinnedMeshData`, `src/Game/Animation/SkeletalRigCache.h`) is
handed to `Game::CreateMeshEntityFromGtaFile()` (`src/Game/Game.cpp`), which
calls:

```cpp
m_physicsSystem.RegisterDynamicChains(absoluteGtaPath, *skin);
```

`PhysicsSystem::RegisterDynamicChains()` (`src/Game/Physics/PhysicsSystem.cpp`)
runs `DetectDynamicChains(data.skeleton, &*data.physics, defaults)` — which
seeds every joint's `DynamicJointSettings::damping/stiffness/mass` from
**pure defaults plus whatever the matched PMX RigidBody says**, never from
anything a user edited in a previous session — and stores the result in
`PhysicsSystem::m_rigCache` (`DynamicChainRigCache`, keyed by
`absoluteGtaPath`).

### 2.3 — How a user edits a joint's settings TODAY (live, in-memory only)

`Panels/InspectorPanel.cpp`, "Dynamic Chain Physics" section (line 649
onward, verified against the current file): for an entity with a
`DynamicChainRig` component, it calls
`physicsSystem.GetDynamicChainRigCache().TryGetMutable(rig->meshGtaPath)` and
directly mutates `DynamicJointSettings& settings = chain.jointSettings[jointIndex]`
via `ImGui::DragFloat("Damping", &settings.damping, ...)` etc. (lines 692-694).
This mutation:

- Lives entirely inside `PhysicsSystem::m_rigCache`'s `ModelEntry` for that
  path — **never** written to disk, **never** touches the `*.gta` file.
- Is shared by every entity currently spawned from that same model path (by
  design — see the section's own header comment) — but is **lost** the moment
  that `ModelEntry` is replaced (`RegisterDynamicChains()` calls
  `m_rigCache.Register()`, which is an `insert_or_assign` — a FRESH spawn of
  the same model path, even in the very same running session, overwrites the
  in-memory edits with freshly auto-detected defaults again).

This confirms the user's own framing precisely: nothing about this edit
survives past the `DynamicChainRigCache` entry's own lifetime today.

### 2.4 — The stable joint identity available at load time

`DynamicChainDefinition::jointBoneIndices` (`src/Physics/
DynamicChainDefinition.h`) holds, for each joint IN ORDER, an index into
`SkeletonData::bones` — a **skeleton bone index**. `PhysicsSystem::
RegisterDynamicChains()`'s own debug-only assert already proves (and relies
on) that **a bone index appears in at most one chain's `jointBoneIndices`,
ever** (`DetectDynamicChains()`'s own disjointness guarantee). This makes bone
index the one stable, natural, already-available-before-detection-runs key
to persist a joint's override by — **not** "chain index + joint-in-chain
index" (those positions are recomputed fresh by `DetectDynamicChains()` every
single load and are NOT guaranteed stable across engine versions/algorithm
tweaks), and **not** a synthetic new joint ID (would require inventing and
maintaining a whole new identity scheme for no benefit — bone index is already
exactly right).

### 2.5 — NEW in v2: why "later in the same session" needs MORE than PHASE1+PHASE2 alone

This is the central finding of the v2 review, verified directly against
`src/Game/Instantiation/MeshAssetGpuCatalog.h`/`.cpp` and
`src/Game/Instantiation/MeshInstantiationSystem.h`.

`MeshAssetGpuCatalog::EnsureMeshAsset()` — the ONE place a Mesh `*.gta` is ever
actually read off disk at spawn time — is explicitly documented and
implemented as **"Loads (once per distinct `absoluteGtaPath`, then cached)"**:

```cpp
const std::vector<MeshAssetPart>& MeshAssetGpuCatalog::EnsureMeshAsset(
    RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath)
{
    if (const auto found = m_meshAssetCache.find(absoluteGtaPath); found != m_meshAssetCache.end()) {
        return found->second; // <-- cache HIT: returns WITHOUT ever touching disk again.
    }
    const std::optional<GtaFileData> gta = ReadGtaFile(...);
    ...
    if (skinned) {
        SkinnedMeshData skinData;
        ...
        m_skinnedMeshCache.insert_or_assign(absoluteGtaPath, std::move(skinData)); // <-- populated ONCE, ever, per path.
    }
    ...
}
```

`Game::CreateMeshEntityFromGtaFile()` calls `SpawnMeshAsset()` (which calls
`EnsureMeshAsset()` above) and THEN, regardless of whether that call was a
cache hit or miss, unconditionally does:

```cpp
if (const SkinnedMeshData* skin = m_meshInstantiationSystem.TryGetSkinnedMeshData(absoluteGtaPath)) {
    ...
    m_physicsSystem.RegisterDynamicChains(absoluteGtaPath, *skin);
```

`TryGetSkinnedMeshData()` is a plain, read-only `m_skinnedMeshCache.find()` —
it returns whatever `SkinnedMeshData` was captured at that path's FIRST
`EnsureMeshAsset()` call this process's lifetime, **forever**, until the
process exits. `PhysicsSystem::RegisterDynamicChains()` itself IS re-run on
every single spawn (it is never cached/skipped) — but the `SkinnedMeshData`
it is handed on the SECOND-and-later spawn of an already-loaded path is the
STALE, first-load snapshot, not a fresh read of whatever is on disk NOW.

Concretely, the failure sequence under v1's plan alone would have been:

1. Spawn model `Miku.gta` (entity A) — first `EnsureMeshAsset()` call for this
   path, reads the file fresh (no saved overrides yet) — `m_skinnedMeshCache`
   now permanently holds `jointPhysicsOverrides = {}` for this path.
2. User drag-edits entity A's joints live (mutates `DynamicChainRigCache`
   directly — this part is unaffected and still works).
3. User clicks "Save Joint Physics to Asset" — `SaveJointPhysicsOverridesToGtaFile()`
   correctly writes the new values into `Miku.gta`'s metadata on disk.
4. **Later in the SAME session**, the user spawns a second copy of the same
   model (entity B, same `absoluteGtaPath`). `EnsureMeshAsset()` sees a cache
   HIT and returns immediately; `TryGetSkinnedMeshData()` returns the SAME
   `SkinnedMeshData` object captured in step 1 — still `jointPhysicsOverrides
   = {}`. `RegisterDynamicChains()` re-runs `DetectDynamicChains()` +
   (PHASE2's) `ApplyJointPhysicsOverrides(chains, {})` — a no-op — and
   `m_rigCache.Register()` REPLACES the shared `ModelEntry` with fresh,
   un-overridden defaults. **Both entity A and entity B now show the
   pre-save defaults**, even though the file on disk has been correctly
   updated and even though entity A itself was showing the edited values one
   frame earlier. Only a full process restart (a genuinely fresh
   `MeshAssetGpuCatalog` with an empty `m_skinnedMeshCache`) would have made
   the saved values actually take effect.

This is a real, user-visible break of this campaign's own literal promise (the
user's own words: *"whenever the model got instantiated into scene"* — not
"whenever the model is instantiated for the first time in a process's
lifetime"). **PHASE3 (new in v2) fixes this** by giving `MeshAssetGpuCatalog`
a narrow, explicit "refresh just this one cached field from disk" operation,
called once, right after a successful save (PHASE4) — see PHASE3's own Step 1
for the full design and exactly why this is the right fix (versus, e.g.,
re-reading the whole file on every single spawn, which this campaign
deliberately rejects — see PHASE3's own "alternatives considered").

### 2.6 — NEW in v2: re-importing an already-tuned model silently destroys saved overrides

Verified directly against `src/Assets/AssetImporter.cpp`, lines 173-228 (the
`.pmx` import branch of `ImportAssetFile()`):

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

`RigFileData rig;` here is **freshly default-constructed** from nothing but
the just-reparsed `.pmx` file — after PHASE1 adds `jointPhysicsOverrides` to
`RigFileData`, this freshly-built `rig` would have it EMPTY every single time,
regardless of whether `gtaPath` already existed on disk with previously-saved
overrides in it. `AssetDatabase::ImportAsset()` (`AssetDatabase.cpp`, line
116) unconditionally calls `WriteGtaFile()`, overwriting the ENTIRE existing
file — metadata included.

The ordinary, entirely expected user workflow this breaks: a user imports
`Miku.pmx`, tunes and saves its joint physics (PHASE1-4), then later
re-imports the SAME `Miku.pmx` again (e.g. after fixing an unrelated UV/mesh
issue in the source file, or simply re-running a batch import) — this would
silently wipe out every saved joint override with no warning, no error, and
no way to recover it (short of re-tuning from scratch).

Notably, this exact class of problem — "a re-import must not destroy data
that only exists in the ALREADY-ON-DISK `*.gta`, never in the freshly
reparsed source file" — already has an established precedent and fix
pattern immediately above, in `AssetDatabase::ImportAsset()` itself:

```cpp
Guid guid = Guid::Generate();
// Reuse an existing asset's Guid when overwriting it in place, so any
// existing scene cross-reference to it survives a re-import.
if (const std::optional<GtaHeader> existing = ReadGtaHeader(destinationGtaPath); existing.has_value()) {
    guid = existing->Id();
}
```

**PHASE1 (v2, new step 3.9) applies this exact same, already-precedented
pattern** to `jointPhysicsOverrides`: read whatever `RigFileData` already
exists at the destination path (if any) BEFORE building the fresh one, and
carry its `jointPhysicsOverrides` forward unchanged.

---

## Step 3: The Plan (How do we get there?)

Five phases, each independently buildable/testable, in this order:

### PHASE1 — `PHASE1_GTA_METADATA_JOINT_OVERRIDE_DATA_MODEL.md`
Add the on-disk data model: a new `JointPhysicsOverride{ boneIndex, damping,
stiffness, mass }` record type (`src/Assets/PhysicsData.h`), a new
`std::vector<JointPhysicsOverride> jointPhysicsOverrides` field on
`RigFileData` (`src/Assets/RigFile.h`/`.cpp`), encoded as a **backward- AND
forward-compatible trailing section** (no magic bump needed — see that
phase's own compatibility analysis), threaded through `SkinnedMeshData`
(`src/Game/Animation/SkeletalRigCache.h`) and populated by
`MeshAssetGpuCatalog::EnsureMeshAsset()`. **(v2, new step 3.9)** also makes
`AssetImporter.cpp`'s `.pmx` re-import path preserve an already-saved
`jointPhysicsOverrides` list instead of silently discarding it (Step 2.6
above). This phase touches ONLY the asset/data layer — nothing simulates or
applies anything yet.

### PHASE2 — `PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md`
Add the "apply on load" half: a new pure function `ApplyJointPhysicsOverrides()`
(`src/Physics/JointPhysicsOverrideApplication.h`/`.cpp`) that takes the
freshly-`DetectDynamicChains()`-produced chains plus the loaded
`jointPhysicsOverrides` list and rewrites each matching joint's
`damping`/`stiffness`/`mass` in place (matched by bone index, silently
ignoring anything that doesn't match — never a hard failure). Wire this one
call into `PhysicsSystem::RegisterDynamicChains()`, right after
`DetectDynamicChains()` returns and before the result is cached. This phase is
what makes "load metadata -> apply at runtime" real for a FRESH process
(unchanged from v1).

### PHASE3 — `PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md` (NEW in v2)
Fixes the gap in Step 2.5 above: gives `MeshAssetGpuCatalog` a new
`RefreshCachedJointPhysicsOverridesFromDisk(absoluteGtaPath)` method that
re-reads ONLY the trailing `jointPhysicsOverrides` section of an
ALREADY-CACHED path's metadata and updates the cached `SkinnedMeshData` in
place (a no-op for a path never spawned this session — nothing to keep in
sync yet, its own eventual first load will read the file correctly anyway).
Threads a narrow forwarding accessor through `MeshInstantiationSystem` and
`Game` (mirroring the exact pattern `TryGetSkinnedMeshData()`/
`GetPhysicsSystem()` already establish) so the Editor can reach it. This
phase adds pure plumbing/infrastructure only — nothing calls the new method
yet; that wiring is PHASE4's job.

### PHASE4 — `PHASE4_EDITOR_SAVE_TO_ASSET_PERSISTENCE.md` (renamed from v1's PHASE3)
Add the "save to disk" half: a new free function
`SaveJointPhysicsOverridesToGtaFile()` (`src/Game/Physics/
DynamicChainPhysicsPersistence.h`/`.cpp`) that reads the target `*.gta` file,
decodes its existing `RigFileData`, replaces only `jointPhysicsOverrides` with
a fresh flattened snapshot of the CURRENT live `DynamicChainDefinition`
settings, re-encodes, and re-writes the file (byte-identical payload/GUID/
flags/version — only the metadata's joint-override section actually changes).
Wire a new "Save Joint Physics to Asset" button into `Panels/
InspectorPanel.cpp`'s existing "Dynamic Chain Physics" section, right next to
the existing Enabled/Freeze checkboxes. **(v2)** Immediately after a
successful save, also calls PHASE3's new
`RefreshCachedJointPhysicsOverridesFromDisk()` so THIS session's cache stays
honest too — without this one extra call, PHASE3's own infrastructure would
exist but never actually be exercised by the one real production caller.
**(v2)** Also definitively resolves v1's one open question (where the
button's transient success/failure feedback text lives) instead of leaving
it as an implementation-time choice.

### PHASE5 — `PHASE5_END_TO_END_ROUND_TRIP_REGRESSION_AND_BUILD_REGISTRATION.md` (renamed from v1's PHASE4)
Wires every new `.cpp` into `CMakeLists.txt`/`tests/CMakeLists.txt`, and adds
the test that actually proves the FULL user-facing story end-to-end: edit ->
save -> (simulate a fresh process) reload -> observe the edited values, not
the original auto-detected defaults, driving the live simulation. **(v2)**
Also adds a SECOND end-to-end test proving the "same session, second spawn"
guarantee from Step 2.5/PHASE3 actually holds (two `RegisterDynamicChains()`
calls against the SAME `PhysicsSystem` instance, the second one fed
PHASE3-refreshed data), and cross-checks PHASE3's own deliverables landed.
Also extends/adds every phase's own narrower unit tests.

### Why this order

PHASE1 must exist before PHASE2 has anything to apply. PHASE2 must exist
before PHASE3's cache-refresh has any effect worth adding (refreshing a field
that is never applied to anything would be a pointless no-op). PHASE3 must
exist before PHASE4's save button can close the loop for the same-session
case (PHASE4 calls PHASE3's new method as its very last step). PHASE4 is the
only phase touching `src/Editor/` (ImGui-dependent, `GTE_ENABLE_EDITOR`-only
code) besides the small, mechanical `InspectorPanel.h` signature change PHASE3
also needs — kept as late as the dependency graph allows, per `AGENTS.md`'s
"Editor Module Structure" boundary discipline. PHASE5 closes the loop with
real, automated proof and is also the only phase that touches build files, so
every new symbol from PHASE1-4 exists before it's referenced there.

### Architecture recap (after all 5 phases land)

```
IMPORT/RE-IMPORT (AssetImporter.cpp)                  LOAD PATH (unchanged call sites, new inner step)
  ImportAssetFile() - PHASE1 3.9                          Game::CreateMeshEntityFromGtaFile()
    reads EXISTING *.gta's jointPhysicsOverrides                    |
    (if any) BEFORE building a fresh RigFileData                   v
    from the reparsed .pmx - carries it forward           MeshAssetGpuCatalog::EnsureMeshAsset()
                                                              reads *.gta -> DecodeRigDataFromBytes()
EDITOR (InspectorPanel.cpp)                                    -> SkinnedMeshData::jointPhysicsOverrides
  "Save Joint Physics to Asset" button                                 |
        |                                                              v
        v                                                     PhysicsSystem::RegisterDynamicChains()
  SaveJointPhysicsOverridesToGtaFile()   <- PHASE4               DetectDynamicChains() -> chains (pure defaults)
   (Game/Physics/DynamicChainPhysicsPersistence)                  ApplyJointPhysicsOverrides(chains, overrides)  <- PHASE2
        |  reads current chain.jointSettings                          |
        |  from DynamicChainRigCache::ModelEntry                      v
        v                                                     DynamicChainRigCache::Register() (now carries
  ReadGtaFile() -> DecodeRigDataFromBytes()                     the SAVED values, not just pure defaults)
   -> mutate RigFileData::jointPhysicsOverrides
   -> EncodeRigDataToBytes() -> WriteGtaFile()
        |
        v
  MeshInstantiationSystem::RefreshCachedJointPhysicsOverridesFromDisk()  <- PHASE3, called by PHASE4
   -> MeshAssetGpuCatalog::RefreshCachedJointPhysicsOverridesFromDisk()
   -> keeps THIS SESSION'S m_skinnedMeshCache entry (if any) in sync with disk,
      so the NEXT spawn of this same path (even without a restart) sees the saved values too.
```

### What We Will NOT Do (explicit non-goals, keeping scope tight)

- **No per-instance overrides.** Persisted overrides remain per-MODEL-PATH,
  exactly matching the existing in-memory `DynamicChainRigCache` scoping rule
  already established by `task_manager/verlet-integration-1`'s PHASE4. Two
  entities spawned from the same `*.gta` continue to share one set of joint
  settings, saved or not.
- **No persistence of `DynamicChainDefinition::collisionEnabled`** (the
  per-chain "Enable Collision" checkbox) or of `group`/`collisionMask`/
  `collisionRadius` (already read-only/derived-from-PMX-shape fields in the
  Inspector). The user's own request names `mass`, `damping`, `stiffness`
  specifically; those three are exactly `DynamicJointSettings`'s three
  Inspector-editable float sliders. A future campaign can extend
  `JointPhysicsOverride` with more fields the same way, if ever asked for.
- **No new asset-type/GUID/versioning scheme.** This reuses the existing Mesh
  `*.gta`'s own `RigFileData` metadata slot; no new `AssetType`, no new sidecar
  file, no `GtaHeader::version` bump (see PHASE1's own compatibility
  analysis for why none of this is needed).
- **No scene/prefab serialization work.** Out of scope entirely — this is
  purely about one model asset's own saved joint-tuning data, not ECS/Registry
  serialization (see `AssetTypes.h`'s own `AssetType::Scene`/`Prefab`
  comments, a separate, unstarted roadmap item per `TODO.md`).
- **(v2) No re-reading the whole `*.gta` file on every single entity spawn.**
  PHASE3 deliberately fixes the same-session staleness gap by refreshing a
  cache ONCE, right after a Save click (a bounded, deliberate, infrequent user
  action) — not by removing `MeshAssetGpuCatalog`'s per-path caching or adding
  a disk read to the hot "spawn N copies of the same model" path. See PHASE3's
  own "Alternatives Considered" for the full reasoning.
- **(v2) No automated GPU/Renderer-backed test of `MeshAssetGpuCatalog`
  itself.** That class is already, and remains, "Tier 2, no automated
  coverage yet" per its own existing doc comment and `AGENTS.md`'s testing
  tiers — PHASE3's new method is exercised by a Tier-1 test for its cheap,
  always-testable "not cached yet" branch, and by manual Editor verification
  for its cache-mutating branch, exactly like PHASE4's own Editor button
  already is. This is a pre-existing, accepted, project-wide limitation, not
  something this campaign introduces or is expected to fix.

### Risk register

| Risk | Mitigation |
|---|---|
| Bumping `RigFile.h`'s magic/layout could silently invalidate every already-imported model's rig data (skeleton/skin weights/morphs/materials would fail to decode, matching the documented `"GTERIG01"`->`"GTERIG02"`->`"GTERIG03"` precedent). | PHASE1 deliberately does NOT bump the magic — the new section is appended as an OPTIONAL TRAILING block, guarded by a cursor-position check on read, so an old file with no such section decodes exactly as before (empty overrides), and a new file decodes correctly under an intentionally-unmodified OLD reader too (trailing bytes it never reads are harmless) - see PHASE1's "Compatibility" section for the exact mechanism and why this is safe. |
| Saving could corrupt an asset if the metadata fails to decode for any reason (e.g. a hand-crafted/corrupted file). | `SaveJointPhysicsOverridesToGtaFile()` (PHASE4) refuses to write anything and returns `false` if `DecodeRigDataFromBytes()` fails on the EXISTING file - it never fabricates a fresh, mostly-empty `RigFileData` and overwrites a file that already had real skeleton/skin-weight/morph/material data, which would otherwise silently destroy that data. |
| A saved override could reference a bone index that no longer exists (or no longer belongs to any detected chain) after some future change to the model or the detection algorithm. | `ApplyJointPhysicsOverrides()` (PHASE2) is a pure lookup-and-skip: any override whose `boneIndex` doesn't match a CURRENTLY detected joint is silently ignored, never an error/crash - documented explicitly in that phase and covered by a dedicated test. |
| Forgetting to register new `.cpp` files in `CMakeLists.txt`/`tests/CMakeLists.txt` (this codebase lists sources explicitly, no glob). | PHASE5 is dedicated entirely to this, with the exact line-by-line diff spelled out. |
| **(v2, NEW) A model already spawned once this session never picks up a just-saved override, because `MeshAssetGpuCatalog::m_skinnedMeshCache` is populated once per path and never refreshed** (Step 2.5). Silently contradicts this campaign's own "whenever the model got instantiated" promise for any session with more than one spawn of the same path. | PHASE3 adds `RefreshCachedJointPhysicsOverridesFromDisk()`; PHASE4 calls it as the final step of a successful save; PHASE5 adds a same-`PhysicsSystem`-instance regression test proving it. |
| **(v2, NEW) Re-importing an already-tuned model's source `.pmx` silently discards every previously-saved joint override**, because `AssetImporter.cpp` builds a brand-new, empty-by-default `RigFileData` purely from the freshly reparsed file and unconditionally overwrites the existing `*.gta` (Step 2.6). | PHASE1's new step 3.9 reads whatever `jointPhysicsOverrides` already exists at the destination path (if any) BEFORE building the fresh `RigFileData`, and carries it forward — the exact same precedented pattern `AssetDatabase::ImportAsset()` already uses to preserve an existing asset's `Guid` across a re-import. Covered by a dedicated `AssetImporterTests.cpp` test. |

### Definition of done (all verifiable by reading/running code, per this task's own instructions)

- `RigFileData` round-trips a non-empty `jointPhysicsOverrides` list through
  `EncodeRigDataToBytes()`/`DecodeRigDataFromBytes()` (PHASE1 test).
- A `RigFileData` blob encoded by CODE THAT PREDATES this campaign (no
  trailing section at all) still decodes successfully today, with
  `jointPhysicsOverrides` empty (PHASE1 backward-compatibility test).
- **(v2)** Re-importing a `.pmx` onto a destination path that already has a
  Mesh `*.gta` with saved `jointPhysicsOverrides` preserves them unchanged in
  the freshly-written file (PHASE1 test).
- `PhysicsSystem::RegisterDynamicChains()`, given a `SkinnedMeshData` whose
  `jointPhysicsOverrides` names a joint's bone index with custom
  damping/stiffness/mass, produces a `DynamicChainRigCache::ModelEntry` whose
  matching joint's `DynamicJointSettings` reflect those custom values, not the
  auto-detected defaults (PHASE2 test).
- **(v2)** `MeshAssetGpuCatalog::RefreshCachedJointPhysicsOverridesFromDisk()`
  is a documented no-op (returns `false`, touches nothing) for a path with no
  existing cache entry (PHASE3 test - the one branch that is genuinely
  Tier-1-testable without a live Renderer).
- Calling `SaveJointPhysicsOverridesToGtaFile()` against a real temp `*.gta`
  file, then independently re-reading that file from disk, yields a
  `RigFileData::jointPhysicsOverrides` that matches exactly what was live in
  the `DynamicChainDefinition`s passed in — and every OTHER piece of that
  file's data (mesh payload, skeleton, skin weights, morphs, materials, GUID,
  flags, version) is provably byte-for-byte/value-for-value unchanged (PHASE4
  test).
- A full end-to-end test: detect chains from a built `*.gta` (defaults) ->
  mutate `DynamicJointSettings` (simulating an Editor drag-edit) -> save ->
  construct a FRESH `PhysicsSystem` and re-run `RegisterDynamicChains()`
  against the same path from scratch -> the freshly detected chain's joint
  settings match the SAVED values, not the original defaults (PHASE5 test).
- **(v2)** A second end-to-end test: the SAME sequence, but the second
  `RegisterDynamicChains()` call reuses the SAME `PhysicsSystem` instance AND
  is fed data that has gone through PHASE3's refresh path, proving the
  "later in the same session" half of this campaign's promise (not just "after
  a restart") genuinely holds (PHASE5 test).
