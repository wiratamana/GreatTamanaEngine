# PHASE3 (NEW in v2) — Same-Session Cache Freshness Infrastructure

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
depends on: `PHASE1_GTA_METADATA_JOINT_OVERRIDE_DATA_MODEL.md`,
`PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md`
required by: `PHASE4_EDITOR_SAVE_TO_ASSET_PERSISTENCE.md`,
`PHASE5_END_TO_END_ROUND_TRIP_REGRESSION_AND_BUILD_REGISTRATION.md`

> **This entire file is new in v2.** It did not exist in v1's four-phase
> plan. It exists to fix a real, verified functional gap: without it, this
> campaign's own promise — *"whenever the model got instantiated into
> scene"* — would silently only be true for a model's FIRST spawn in a given
> process's lifetime, never for a second-or-later spawn of an
> already-loaded path within the same running session. See
> `PHASE0_MASTER_STRATEGY.md`, Step 2.5, for the full root-cause analysis this
> phase is the fix for. Read that section first if the "why" below feels
> unmotivated.

---

## Step 1: The Goal

Give `MeshAssetGpuCatalog` — the ONE place a Mesh `*.gta`'s `SkinnedMeshData`
is cached, keyed by absolute path, for the lifetime of the running process —
a narrow, explicit, on-demand way to refresh JUST the `jointPhysicsOverrides`
field of an ALREADY-cached entry from whatever is currently on disk, without
re-reading/re-decoding/re-uploading anything else (mesh geometry, skeleton,
skin weights, morphs, materials, GPU buffers). This is infrastructure only —
by the end of this phase, nothing calls the new method in production yet
(that is PHASE4's job, as the last step of a successful save); this phase's
own job is purely to make the capability exist, be correctly scoped, and be
tested wherever it genuinely can be.

Concretely, when this phase is done:

1. `MeshAssetGpuCatalog::RefreshCachedJointPhysicsOverridesFromDisk(absoluteGtaPath)`
   exists, is narrow and side-effect-free for any path it has never cached
   (returns `false`, touches nothing), and for a path it HAS already cached,
   re-reads that exact file's metadata and replaces only that one cached
   `SkinnedMeshData::jointPhysicsOverrides` field in place.
2. `MeshInstantiationSystem` and `Game` each expose a narrow, one-line
   forwarding accessor to reach it, mirroring the EXACT pattern already
   established by `TryGetSkinnedMeshData()` (`MeshInstantiationSystem.h`) and
   `GetPhysicsSystem()` (`Game.h`) respectively — so `src/Editor/` code can
   reach it in PHASE4 without Game/MeshInstantiationSystem's internals
   leaking any further than they already do today.

## Step 2: The Situation

### 2.1 — The exact caching behavior this phase must work around

Verified directly against the current source:

`src/Game/Instantiation/MeshAssetGpuCatalog.h`:
```cpp
class MeshAssetGpuCatalog {
public:
    EntityBlueprint Resolve(RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath);
    const std::vector<MeshAssetPart>* TryGetParts(const std::string& absoluteGtaPath) const;
    const SkinnedMeshData* TryGetSkinnedMeshData(const std::string& absoluteGtaPath) const;

private:
    const std::vector<MeshAssetPart>& EnsureMeshAsset(
        RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath);

    std::unordered_map<std::string, std::vector<MeshAssetPart>> m_meshAssetCache;
    std::unordered_map<std::string, SkinnedMeshData> m_skinnedMeshCache;
};
```

`src/Game/Instantiation/MeshAssetGpuCatalog.cpp`'s `EnsureMeshAsset()`:
```cpp
const std::vector<MeshAssetPart>& MeshAssetGpuCatalog::EnsureMeshAsset(
    RenderSystem& renderSystem, Renderer& renderer, const std::string& absoluteGtaPath)
{
    static const std::vector<MeshAssetPart> kEmpty;
    if (const auto found = m_meshAssetCache.find(absoluteGtaPath); found != m_meshAssetCache.end()) {
        return found->second; // <-- cache HIT for this path: returns immediately, no disk access at all.
    }
    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteGtaPath));
    ...
    if (skinned) {
        SkinnedMeshData skinData;
        ...
        m_skinnedMeshCache.insert_or_assign(absoluteGtaPath, std::move(skinData)); // <-- populated ONCE per path, ever.
    }
    ...
}
```

`m_skinnedMeshCache[absoluteGtaPath]` is written exactly once, the first time
that exact path is ever resolved by this `MeshAssetGpuCatalog` instance (one
instance lives for the lifetime of the owning `Game`/process — see
`Game.h`'s `MeshInstantiationSystem m_meshInstantiationSystem{ m_renderSystem };`
member, constructed once, never recreated mid-session). Nothing in the
existing codebase ever invalidates or re-populates an entry after that first
write.

### 2.2 — Why `RegisterDynamicChains()` being re-run every spawn does NOT already solve this

`Game::CreateMeshEntityFromGtaFile()` (`Game.cpp`) DOES call
`m_physicsSystem.RegisterDynamicChains(absoluteGtaPath, *skin)` on every
single spawn, unconditionally — that part is never cached/skipped. The
problem is entirely upstream: `*skin` (the `SkinnedMeshData*` from
`TryGetSkinnedMeshData()`) is a pointer into the SAME, single,
never-refreshed `m_skinnedMeshCache` entry described in 2.1. Re-running
`RegisterDynamicChains()` with stale input still produces a stale result — the
function itself has no bug; the data it is fed is simply out of date after a
save.

### 2.3 — Where `Game`/`MeshInstantiationSystem` are reachable from the Editor today

Verified against `src/Editor/ImGuiEditorLayer.cpp`, lines 472-476:
```cpp
BuildHierarchyPanel(game, renderer, m_ctx);
#if GTE_ENABLE_PROJECT_PANEL
BuildInspectorPanel(registry, m_ctx, renderer, m_assetPreview, m_assetPreviewMesh, m_boneViewer, game.GetPhysicsSystem(), m_modelRigCache);
#else
BuildInspectorPanel(registry, m_ctx, game.GetPhysicsSystem());
#endif
```
`game` (a `Game&`) is already in scope in the function that calls
`BuildInspectorPanel()` — it is NOT itself passed into that function; instead,
a narrow accessor (`game.GetPhysicsSystem()`) is passed. This is the
established, precedented convention this phase's own new accessor should
follow, rather than passing the whole `Game&` (which `Panels/InspectorPanel.cpp`
does not, and per `AGENTS.md`'s "Editor Module Structure"/`Game.h`'s own class
comment ("Editor only ever *observes* Game... through its existing public
accessors"), should not either).

`src/Game/Instantiation/MeshInstantiationSystem.h` already establishes the
exact forwarding-accessor shape to mirror:
```cpp
const SkinnedMeshData* TryGetSkinnedMeshData(const std::string& absoluteGtaPath) const
{
    return m_meshAssetCatalog.TryGetSkinnedMeshData(absoluteGtaPath);
}
```

`src/Game/Game.h` already establishes the exact `Game`-level accessor shape to
mirror (`GetPhysicsSystem()`):
```cpp
PhysicsSystem& GetPhysicsSystem() noexcept { return m_physicsSystem; }
```

### 2.4 — Alternatives considered (and rejected)

- **Re-read the whole `*.gta` file on every single spawn, bypassing the cache
  entirely for `jointPhysicsOverrides`.** Rejected: this reintroduces a full
  file read + FULL `RigFileData` decode (skeleton, every skin weight, every
  morph, every material — not just the small trailing joint-override
  section) on every spawn, including the common "spawn N copies of the same
  model in a loop" case this cache exists specifically to make cheap. The
  chosen design instead pays this cost exactly once per Save click — a
  deliberate, infrequent, user-initiated action — never on the hot spawn
  path.
- **Remove `MeshAssetGpuCatalog`'s caching altogether.** Rejected outright —
  wildly out of scope, would regress load performance for every model in the
  project, not just ones using this campaign's feature.
- **Have `PhysicsSystem` own its own independent copy of `jointPhysicsOverrides`,
  refreshed some other way.** Rejected: `PhysicsSystem` already re-derives
  its own `DynamicChainRigCache::ModelEntry` fresh on every
  `RegisterDynamicChains()` call from whatever `SkinnedMeshData` it is given
  — duplicating a SECOND, independently-refreshed copy of the same data one
  layer away would be strictly more state to keep in sync for no benefit;
  fixing the ONE actual source of staleness (`MeshAssetGpuCatalog`) is
  simpler and removes the problem at its root.

## Step 3: The Plan

### 3.1 — New method: `src/Game/Instantiation/MeshAssetGpuCatalog.h`/`.cpp`

Header addition (`MeshAssetGpuCatalog.h`), alongside the existing
`TryGetSkinnedMeshData()` declaration:

```cpp
// task_manager/verlet-integration-11, PHASE3 - re-reads ONLY the *.gta's
// metadata's trailing jointPhysicsOverrides section (see Assets/RigFile.h,
// PHASE1) for `absoluteGtaPath` and updates the ALREADY-CACHED
// SkinnedMeshData for that exact path in place - never the mesh payload,
// skeleton, skin weights, morphs, materials, or any GPU resource. Called by
// Editor/Panels/InspectorPanel.cpp (via MeshInstantiationSystem's own
// forwarding accessor, PHASE3) immediately after
// SaveJointPhysicsOverridesToGtaFile() (PHASE4) succeeds, so a model
// respawned LATER IN THE SAME SESSION (not just after a full engine
// restart) also picks up the just-saved values - see
// PHASE0_MASTER_STRATEGY.md, Step 2.5, for why EnsureMeshAsset()'s own
// "once per distinct path, then cached forever" contract otherwise leaves
// an already-loaded path's cached SkinnedMeshData permanently stale.
//
// Returns false (and touches nothing) if `absoluteGtaPath` has never been
// cached by this MeshAssetGpuCatalog instance (nothing to keep in sync yet -
// that path's own eventual FIRST EnsureMeshAsset() call will read the
// freshly-saved file correctly on its own regardless), or if the file can't
// be read/decoded as a valid Mesh *.gta with decodable rig metadata (the
// existing cached entry is left untouched rather than being corrupted or
// cleared - a failed refresh is a silent no-op, never destructive).
bool RefreshCachedJointPhysicsOverridesFromDisk(const std::string& absoluteGtaPath);
```

Implementation (`MeshAssetGpuCatalog.cpp`), placed right after
`TryGetSkinnedMeshData()`:

```cpp
bool MeshAssetGpuCatalog::RefreshCachedJointPhysicsOverridesFromDisk(const std::string& absoluteGtaPath)
{
    const auto found = m_skinnedMeshCache.find(absoluteGtaPath);
    if (found == m_skinnedMeshCache.end()) {
        return false; // Never cached this session - nothing to refresh (see this method's own header comment).
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return false; // Leave the existing cached entry exactly as it was - never clear/corrupt it on a failed refresh.
    }
    const std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    if (!rig.has_value()) {
        return false;
    }

    found->second.jointPhysicsOverrides = rig->jointPhysicsOverrides;
    return true;
}
```

No new includes needed — `MeshAssetGpuCatalog.cpp` already includes
`Assets/AssetTypes.h`, `Assets/GtaFile.h`, and `Assets/RigFile.h` (see its
existing `EnsureMeshAsset()` body), and already defines the anonymous-namespace
`Utf8PathFromGamePath()` helper this reuses verbatim.

### 3.2 — Forwarding accessor: `src/Game/Instantiation/MeshInstantiationSystem.h`

Alongside the existing `TryGetMeshAssetParts()`/`TryGetSkinnedMeshData()`
forwarding methods:

```cpp
// task_manager/verlet-integration-11, PHASE3 - forwards straight to
// MeshAssetGpuCatalog::RefreshCachedJointPhysicsOverridesFromDisk() (see its
// own doc comment for the full contract) - NOT const, since it mutates the
// underlying cache, unlike TryGetMeshAssetParts()/TryGetSkinnedMeshData()
// above.
bool RefreshCachedJointPhysicsOverridesFromDisk(const std::string& absoluteGtaPath)
{
    return m_meshAssetCatalog.RefreshCachedJointPhysicsOverridesFromDisk(absoluteGtaPath);
}
```

### 3.3 — `Game`-level accessor: `src/Game/Game.h`

Mirroring `GetPhysicsSystem()`'s exact shape and placement (right after it):

```cpp
// Editor-facing accessor (task_manager/verlet-integration-11, PHASE3) -
// mirrors GetPhysicsSystem()'s own "Editor observes/acts through a public
// accessor" convention, this time so the Inspector's "Save Joint Physics to
// Asset" button (PHASE4) can keep THIS SESSION'S already-cached
// SkinnedMeshData in sync with whatever it just wrote to disk - see
// MeshInstantiationSystem::RefreshCachedJointPhysicsOverridesFromDisk()'s own
// doc comment for the full contract this forwards to.
MeshInstantiationSystem& GetMeshInstantiationSystem() noexcept { return m_meshInstantiationSystem; }
```

### 3.4 — Thread the new accessor into `BuildInspectorPanel()`'s signature

**`src/Editor/Panels/InspectorPanel.h`:** add a forward declaration
alongside the existing ones, and a new parameter to BOTH overloads:

```cpp
class Registry;
class Renderer;
class PhysicsSystem;
class MeshInstantiationSystem; // <-- NEW (PHASE3)
struct EditorContext;
...
#if GTE_ENABLE_PROJECT_PANEL
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview,
    AssetPreviewMesh& assetPreviewMesh, BoneViewerWindow& boneViewer, PhysicsSystem& physicsSystem,
    MeshInstantiationSystem& meshInstantiationSystem, ModelRigCache& rigCache); // <-- NEW parameter
#else
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, PhysicsSystem& physicsSystem,
    MeshInstantiationSystem& meshInstantiationSystem); // <-- NEW parameter
#endif
```

**`src/Editor/Panels/InspectorPanel.cpp`:** add
`#include "../../Game/Instantiation/MeshInstantiationSystem.h"` alongside the
existing `#include "../../Game/Physics/PhysicsSystem.h"`, and update both
`BuildInspectorPanel()` definitions' parameter lists to match the header
exactly (the parameter is threaded through but not yet USED anywhere inside
this function's body — that happens in PHASE4, which adds the one call site
that actually needs it). Update this file's own doc comment (mirroring how
`physicsSystem`'s own arrival was documented, per `InspectorPanel.h`'s
existing comment) to note `meshInstantiationSystem` exists in EVERY signature
for the same reason `physicsSystem` does.

**`src/Editor/ImGuiEditorLayer.cpp`:** update both call sites (lines 474 and
476) to pass the new accessor:

```cpp
#if GTE_ENABLE_PROJECT_PANEL
BuildInspectorPanel(registry, m_ctx, renderer, m_assetPreview, m_assetPreviewMesh, m_boneViewer,
    game.GetPhysicsSystem(), game.GetMeshInstantiationSystem(), m_modelRigCache);
#else
BuildInspectorPanel(registry, m_ctx, game.GetPhysicsSystem(), game.GetMeshInstantiationSystem());
#endif
```

### 3.5 — Tests

The "not cached yet" branch is genuinely Tier-1-testable with zero Renderer
dependency (the cache is a plain, empty `std::unordered_map` for any
never-touched path) — this is the one piece of this phase that gets
automated coverage:

**New file — `tests/Game/Instantiation/MeshAssetGpuCatalogJointOverrideRefreshTests.cpp`:**

```cpp
#include "Game/Instantiation/MeshAssetGpuCatalog.h"
#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(MeshAssetGpuCatalogTest, RefreshingAPathNeverCachedThisSessionIsANoOpThatReturnsFalse)
{
    MeshAssetGpuCatalog catalog;
    // No EnsureMeshAsset()/Resolve() call has ever been made against this
    // path on this catalog instance - m_skinnedMeshCache is genuinely empty
    // for it, so this needs no Renderer/GPU/real *.gta file at all (see this
    // phase's own Step 3.5 for why the OTHER branch - an already-cached path
    // - cannot be exercised this way).
    EXPECT_FALSE(catalog.RefreshCachedJointPhysicsOverridesFromDisk("C:/does/not/matter/NeverSpawned.gta"));
}

} // namespace
} // namespace gte
```

**The "already cached, refresh actually replaces the field" branch** requires
a real `Renderer`/GPU device to first populate `m_skinnedMeshCache` via
`EnsureMeshAsset()`/`Resolve()` at all (this is not a limitation this phase
introduces — `MeshAssetGpuCatalog`'s own existing class-level doc comment
already states plainly: *"Genuinely needs a live Renderer to do its real job -
Tier 2, no automated coverage yet"*, and every existing method on this class
carries that same limitation today). Per `AGENTS.md`'s own testing tiers,
this is an accepted, pre-existing, project-wide bucket, never a blocker for
landing a change — verify this branch manually instead:

**Manual Editor verification checklist (record the outcome in PHASE5's own
"cross-check every earlier phase" step, alongside PHASE4's own Editor-button
manual check):**
1. Spawn a model with at least one detected dynamic chain twice into the same
   scene (two entities, same source `*.gta`).
2. Drag-edit one entity's joint sliders, click "Save Joint Physics to Asset"
   (PHASE4).
3. Spawn a THIRD entity from the SAME `*.gta` path, in the SAME running
   Editor session (no restart).
4. Confirm the third entity's Inspector "Dynamic Chain Physics" section shows
   the SAVED values, not the original auto-detected defaults — this is only
   possible if `RefreshCachedJointPhysicsOverridesFromDisk()` genuinely ran
   and updated the shared cache entry (PHASE4 wires the call; this phase only
   provides the method).

### What This Phase Deliberately Does NOT Do

- Does not call the new method anywhere yet — `SaveJointPhysicsOverridesToGtaFile()`
  does not exist until PHASE4, which is also where the one production call
  site is added. This phase only makes the capability exist, correctly
  scoped and reachable from the Editor.
- Does not change `MeshAssetGpuCatalog::EnsureMeshAsset()` itself in any way —
  the FIRST load of any given path is already correct (it reads the file
  fresh, including any already-saved `jointPhysicsOverrides` from a prior
  session, per PHASE1); this phase only addresses a SECOND-or-later load
  within the SAME session.
- Does not attempt to refresh/invalidate `m_meshAssetCache` (the GPU
  `MeshAssetPart` list) or anything skeleton/skin-weight/morph/material-
  related — this campaign never changes any of that data, so there is
  nothing to refresh there; refreshing it anyway would be wasted GPU work for
  zero behavioral benefit.
- Does not add a general-purpose "reload this model from disk" Editor feature
  (a `MeshAssetGpuCatalog::InvalidatePath()`-style full cache-clear) — out of
  scope; this phase's fix is deliberately as narrow as the one field this
  campaign actually needs kept fresh.
