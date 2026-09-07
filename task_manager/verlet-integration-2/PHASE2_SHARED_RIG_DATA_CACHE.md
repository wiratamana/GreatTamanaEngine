# PHASE2 — Shared Rig Data Cache (`src/Editor/ModelRigCache.h/.cpp`)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit A/E). Depends on: nothing (this
phase is independent of Phase 1 — it touches a disjoint set of files; may be
implemented before or after Phase 1, but this document assumes Phase 1 is
already done, matching `PHASE0`'s stated execution order). Produces: a new,
small, Tier-1-testable class that loads + mtime-caches ONE model's complete
`RigFileData` (bones, rigid bodies, joints, morphs, skin weights, materials)
straight from its source `*.gta` file, extracted from logic that today lives
ad hoc, private, and window-scoped inside `BoneViewerWindow::EnsureDataLoaded()`.

## Step 1: The Goal

Give both `BoneViewerWindow` (Phase 3) and `InspectorPanel` (Phase 4) ONE
shared, reusable way to answer "what are the bones/rigid bodies/joints of
the model this entity was spawned from, right now" — without either of them
re-deriving their own independent copy of "read the `*.gta`, decode its
METADATA section as `RigFileData`, only reload when the file's mtime
actually changed" logic. This must be genuinely Tier-1-testable (a real temp
`*.gta` file on disk, no ImGui/Renderer/live-Vulkan-device/SDL-video
involved at all — the exact same bar `Editor/ProjectPanelDataTests.cpp`
already clears for a different Editor-side, real-filesystem-touching class).

## Step 2: The Situation / The Problem

`BoneViewerWindow::EnsureDataLoaded()` (`BoneViewerWindow.cpp`) today does,
all inline, inside one private method:

1. `std::filesystem::last_write_time()` on the absolute `*.gta` path, and a
   short-circuit "unchanged since last call" return if the path AND mtime
   both still match what was cached last time (`m_cachedPath`/
   `m_cachedWriteTime`/`m_cachedIsValid`).
2. `ReadGtaFile()` + a `header.Type() != AssetType::Mesh` check.
3. `DecodeMeshDataFromBytes(gta->payload)` — the GPU-mesh-only half (out of
   scope for this phase entirely; stays exactly where it is).
4. `DecodeRigDataFromBytes(gta->metadata)` — the METADATA half this phase
   cares about (`RigFileData`, containing `skeleton.bones` AND
   `physics.rigidBodies`/`physics.joints` — see `Assets/RigFile.h`) — but
   only ever copies `bone.name`/`bone.position`/`bone.parentBoneIndex` out
   into its own private `BoneEntry` struct; `rig->physics` is discarded
   completely (Culprit A).

This whole block is private to one window, keyed to whatever ONE entity that
window currently has open (`m_targetEntity`) — there is no way for
`InspectorPanel` (which must be able to show model-part info for whatever
entity `Selection::SelectedModelPartEntity()` currently names, independent
of whether the Bone Viewer window even happens to be open right now, or
happens to currently be showing a DIFFERENT entity's model) to reach this
data at all (Culprit E).

## Step 3: The Plan

### 3.1 New file: `src/Editor/ModelRigCache.h`

```cpp
#pragma once

#include "../Assets/RigFile.h"

#include <filesystem>
#include <optional>
#include <string>

namespace gte {

// Loads + mtime-caches ONE model's complete RigFileData (per-vertex skin
// weights, bone hierarchy, morphs, rigid-body/joint physics setup,
// materials - see Assets/RigFile.h) straight from its source Mesh *.gta
// file's own METADATA section - the exact "read straight from source,
// reload only when mtime changes" logic BoneViewerWindow::EnsureDataLoaded()
// already implemented privately for its own internal use, extracted here so
// it can be shared, verbatim, by any OTHER Editor consumer that needs the
// same rig data WITHOUT also wanting a GPU mesh/pipeline (see
// PHASE0_MASTER_STRATEGY.md, Culprit A/E) - the same "never maintain two
// independent, silently-divergible copies of the same [...] pattern"
// precedent AGENTS.md already documents for bone-ancestor-chain walking
// (see "Skeletal Animation Pose Resolution").
//
// Deliberately does NOT touch the *.gta's PAYLOAD (the mesh vertex/index
// data) at all - that stays BoneViewerWindow's own private concern (its own
// GPU vertex/index buffers), since InspectorPanel (the other consumer of
// this class, PHASE4_INSPECTOR_MODEL_PART_SECTION.md) never needs a live
// mesh, only the metadata. A caller that ALSO needs the mesh payload (only
// BoneViewerWindow does) still calls ReadGtaFile() itself separately for
// that - this is a small, deliberate, documented duplication of ONE
// ReadGtaFile() disk read on an (infrequent, mtime-gated) reload event, not
// a duplication of the actual decode-and-cache-invalidation LOGIC, which is
// the part that actually matters (see Culprit E).
//
// Not tied to any one entity/window - a single instance can safely be asked
// about many different model paths over its lifetime (each tracked
// independently would be overkill for this campaign's actual need: exactly
// ONE shared instance, owned by ImGuiEditorLayer, is asked about whichever
// ONE path is relevant at a time - see BoneViewerWindow.h/InspectorPanel.h's
// own updated signatures). Deliberately a single-slot cache (like
// BoneViewerWindow's own m_cachedPath/m_cachedWriteTime before this
// extraction) rather than a path-keyed map - if a future need arises to
// track more than one model's rig data live at once, extend this class
// then, don't build that speculatively now.
class ModelRigCache {
public:
    // Returns a pointer to the cached RigFileData for `absoluteGtaPath`,
    // reloading from disk only if `absoluteGtaPath` differs from whichever
    // path was cached last, or the file's mtime has changed since then
    // (mirrors BoneViewerWindow::EnsureDataLoaded()'s own short-circuit
    // exactly). Returns nullptr if `absoluteGtaPath` doesn't exist, doesn't
    // resolve to a valid *.gta, or that *.gta's header does not declare
    // AssetType::Mesh - nullptr specifically means "not a loadable Mesh
    // asset at all", NOT "no bones" (a boneless mesh, or one imported
    // before rig extraction existed, still decodes to a valid, merely EMPTY
    // RigFileData - see RigFile.h's own doc comment - and this method
    // returns a valid pointer to that empty struct in that case, exactly
    // like DecodeRigDataFromBytes() itself never fails on a well-formed,
    // even entirely empty, RigFileData).
    //
    // The returned pointer is only valid until the NEXT call to
    // GetOrLoad() on this same instance (a subsequent call for a different
    // path, or the same path with a newer mtime, replaces the cached
    // value) - callers must not hold onto it across frames; re-call
    // GetOrLoad() every frame it's needed, exactly like
    // BoneViewerWindow::EnsureDataLoaded()'s own existing calling
    // convention already does for m_cachedIsValid-gated members.
    const RigFileData* GetOrLoad(const std::string& absoluteGtaPath);

private:
    std::string m_cachedPath;
    std::filesystem::file_time_type m_cachedWriteTime{};
    std::optional<RigFileData> m_cachedRig; // std::nullopt means "last load attempt failed / not a Mesh *.gta".
};

} // namespace gte
```

### 3.2 New file: `src/Editor/ModelRigCache.cpp`

```cpp
#include "ModelRigCache.h"

#include "ProjectPanelData.h" // Utf8ToPath()
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()

namespace gte {

const RigFileData* ModelRigCache::GetOrLoad(const std::string& absoluteGtaPath)
{
    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(Utf8ToPath(absoluteGtaPath), timeEc);

    if (!absoluteGtaPath.empty() && absoluteGtaPath == m_cachedPath && !timeEc && writeTime == m_cachedWriteTime) {
        return m_cachedRig.has_value() ? &m_cachedRig.value() : nullptr;
    }

    m_cachedPath = absoluteGtaPath;
    m_cachedRig.reset();
    if (!timeEc) {
        m_cachedWriteTime = writeTime;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8ToPath(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return nullptr;
    }

    m_cachedRig = DecodeRigDataFromBytes(gta->metadata);
    // A well-formed but EMPTY metadata blob (a Mesh *.gta imported before
    // rig extraction existed) fails DecodeRigDataFromBytes() (bad
    // magic/too-short) rather than decoding to an empty RigFileData - map
    // that specific case to a valid, empty RigFileData instead of nullptr,
    // matching RigFile.h's own "a boneless/riggless mesh simply has an
    // empty metadata blob" documented convention (see
    // BoneViewerWindow::EnsureDataLoaded()'s own equivalent handling today).
    if (!m_cachedRig.has_value() && gta->metadata.empty()) {
        m_cachedRig = RigFileData{};
    }

    return m_cachedRig.has_value() ? &m_cachedRig.value() : nullptr;
}

} // namespace gte
```

Note the one subtlety called out explicitly in the code above: an empty
`gta->metadata` (a Mesh asset imported before rig extraction existed at all)
makes `DecodeRigDataFromBytes()` fail outright (its magic-check rejects a
too-short blob) — `BoneViewerWindow::EnsureDataLoaded()`'s existing code
handles this today via its own `if (!gta->metadata.empty())` guard BEFORE
ever calling decode, leaving `m_bones` empty either way. `ModelRigCache`
must preserve the SAME observable "boneless mesh → valid, empty
`RigFileData`, not a hard failure" behavior for its own callers (Phase 3/4
both rely on being able to tell "not a Mesh asset at all" apart from "a
valid Mesh asset with nothing rigged" via nullptr vs. non-null-but-empty).

### 3.3 CMake registration

Add to `CMakeLists.txt`'s `if(GTE_ENABLE_PROJECT_PANEL)` `target_sources()`
block (the same block `BoneViewerWindow.h/.cpp` already lives in — this
class is only ever used by that window and by Phase 4's Inspector section,
both of which only exist under this same switch):

```
src/Editor/ModelRigCache.h
src/Editor/ModelRigCache.cpp
```

Insert it alongside/before the existing `BoneViewerWindow.h`/`.cpp` entries.

### 3.4 Tests — new file `tests/Editor/ModelRigCacheTests.cpp`

Tier 1, real temp directory, no ImGui/Renderer/live-Vulkan-device/SDL-video
involved — mirrors `tests/Editor/ProjectPanelDataTests.cpp`'s own "creates/
tears down a real temp directory" convention exactly, and reuses
`Assets/RigFileTests.cpp`'s own "hand-build a `RigFileData`, round-trip it"
style for constructing fixtures, composed with `GtaFile.h`'s `WriteGtaFile()`
to actually produce an on-disk `*.gta`:

- `ReturnsNullptrForANonexistentPath` — `GetOrLoad("C:/does/not/exist.gta")`
  returns `nullptr`.
- `ReturnsNullptrForAFileThatIsNotAMeshGta` — write a real `*.gta` via
  `WriteGtaFile(path, AssetType::Texture, ...)` (any non-Mesh type); assert
  `GetOrLoad(path)` returns `nullptr`.
- `LoadsBonesRigidBodiesAndJointsFromARealMeshGta` — build a `RigFileData`
  with at least one `Bone`, one `RigidBody`, and one `Joint` (distinct,
  hand-picked field values, e.g. specific names/positions/shapeSize/
  motionType so the assertions are meaningful, not just "non-empty"), encode
  it via `EncodeRigDataToBytes()`, write it as a real Mesh `*.gta`'s metadata
  section via `WriteGtaFile(path, AssetType::Mesh, guid, flags, metadataBytes,
  /*payload=*/{})`; assert `GetOrLoad(path)` returns non-null and every
  field of the first bone/rigid body/joint matches exactly what was written.
- `ReturnsAValidEmptyRigFileDataForABonelessMeshGta` — write a real Mesh
  `*.gta` with an EMPTY metadata blob (`WriteGtaFile(path, AssetType::Mesh,
  guid, flags, /*metadata=*/{}, /*payload=*/{})`); assert `GetOrLoad(path)`
  returns NON-null (not "not a Mesh asset"), and that pointer's
  `skeleton.bones`/`physics.rigidBodies`/`physics.joints` are all empty —
  the specific regression this phase's own Step 3.2 note calls out.
- `ReloadsWhenTheFileIsRewrittenWithANewerMtime` — `GetOrLoad(path)` once
  against a fixture with one bone named `"A"`; overwrite the SAME path with
  a DIFFERENT `RigFileData` (one bone named `"B"`) via `WriteGtaFile()`
  again (ensure the mtime actually advances — e.g. explicitly call
  `std::filesystem::last_write_time()` to set a strictly later timestamp if
  the two writes could otherwise land in the same filesystem-mtime-
  resolution tick); `GetOrLoad(path)` again and assert the SECOND call
  returns the "B" fixture's data, not a stale cached "A".
- `DoesNotReloadWhenNeitherPathNorMtimeChanged` — `GetOrLoad(path)` twice in
  a row with no on-disk change between calls; assert both calls return a
  pointer to a struct with identical field values (this test does not need
  to assert the exact same POINTER address — `ModelRigCache` makes no such
  promise beyond "valid until the next `GetOrLoad()` call" — just that the
  content is correct and the short-circuit path doesn't corrupt anything).
- `SwitchingBetweenTwoDifferentPathsReloadsCorrectlyEachTime` — build two
  distinct fixture files at two different paths; call `GetOrLoad(pathA)`,
  then `GetOrLoad(pathB)`, then `GetOrLoad(pathA)` again; assert each call
  returns the correct fixture's own data (proves the single-slot cache
  correctly treats "different path" as a cache-invalidating change, not just
  "different mtime").

Register the new file in `tests/CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` →
`if(GTE_ENABLE_PROJECT_PANEL)` block (same nesting as
`Editor/ProjectPanelDataTests.cpp`/`Editor/AssetInspectorDataTests.cpp`
today), and add a matching descriptive paragraph to that file's own header
"Test taxonomy" comment block, in the same style/detail as the existing
`Editor/ProjectPanelDataTests.cpp` entry — every current Tier-1 test file in
this codebase has one.

## Step 4: What We Will NOT Do

- We will **not** make `ModelRigCache` a path-keyed multi-entry cache (e.g.
  `std::unordered_map<std::string, RigFileData>`) — a single-slot cache is
  sufficient for this campaign's actual usage pattern (one shared instance,
  asked about whichever ONE model is currently relevant at a time — see
  3.1's own class comment) and mirrors `BoneViewerWindow`'s own prior
  single-slot design exactly; do not build a speculative multi-entry version
  preemptively.
- We will **not** have `ModelRigCache` also load/cache the mesh PAYLOAD
  (vertex/index data) — that stays `BoneViewerWindow`'s own private concern,
  unchanged from today, since it alone needs a live GPU buffer for it (see
  3.1's own class comment for the full reasoning).
- We will **not** thread `ModelRigCache` through `Game`/`Renderer`/any
  non-Editor layer — this is purely an Editor-side debug/inspection cache,
  symmetrical with `AssetPreviewMesh`/`AssetPreviewTexture`'s own existing
  Editor-only scope.

## Step 5: Their Role

Implementer checklist for this phase:

1. Create `src/Editor/ModelRigCache.h`/`.cpp` exactly per 3.1/3.2 (including
   the empty-metadata → valid-empty-`RigFileData` mapping — do not skip
   this, it is the one genuinely subtle piece of behavior being preserved
   from the code being extracted from).
2. Add both files to `CMakeLists.txt`'s `GTE_ENABLE_PROJECT_PANEL` source
   block, per 3.3.
3. Create `tests/Editor/ModelRigCacheTests.cpp` with every case in 3.4, and
   register it (+ its taxonomy-comment paragraph) in
   `tests/CMakeLists.txt`'s nested `GTE_ENABLE_EDITOR`/
   `GTE_ENABLE_PROJECT_PANEL` block.
4. Build `gte_core` + `GreatTamanaEngineTests` and confirm every new test
   passes before starting Phase 3 — Phase 3's rewiring of
   `BoneViewerWindow` calls `ModelRigCache::GetOrLoad()` directly and cannot
   be correctness-checked itself if this layer is already wrong.
