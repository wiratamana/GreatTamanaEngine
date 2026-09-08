# PHASE4 (v2, renamed from v1's PHASE3) — Saving Live Joint Edits Back Into the `*.gta` File From the Editor

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
depends on: `PHASE1_GTA_METADATA_JOINT_OVERRIDE_DATA_MODEL.md`,
`PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md`,
`PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md`
required by: `PHASE5_END_TO_END_ROUND_TRIP_REGRESSION_AND_BUILD_REGISTRATION.md`

> **v2 changes from v1's PHASE3:** (1) this phase now also depends on the new
> PHASE3 and, as its final step, calls PHASE3's new
> `RefreshCachedJointPhysicsOverridesFromDisk()` right after a successful
> save (Step 3.3 below) — without this one extra call, PHASE3's own
> infrastructure would exist but never actually run in production, and this
> campaign's "later in the same session" promise would still silently not
> hold (see `PHASE0_MASTER_STRATEGY.md`, Step 2.5). (2) v1's one open "check
> before choosing" question (where the button's transient feedback state
> lives) is now resolved definitively, with the verified fact backing the
> decision (Step 3.3). Everything else (the persistence function itself,
> `Step 3.1`/`3.2`, its tests `3.4`) is unchanged from v1 — re-verified
> against the current source tree and still accurate.

## Step 1: The Goal

Give the user a real, one-click way to turn the LIVE, in-memory
`DynamicJointSettings` values they just drag-edited in the Inspector's
"Dynamic Chain Physics" section into a durable write to the model's own
`*.gta` file — using PHASE1's new `RigFileData::jointPhysicsOverrides` field —
**without corrupting or losing any other data already stored in that file**
(mesh geometry payload, skeleton, skin weights, morphs, materials, GUID,
flags, version), and **without leaving THIS SESSION'S own cache stale for any
model spawned again afterward** (PHASE3's new refresh call, Step 3.3 below).

## Step 2: The Situation

- `Panels/InspectorPanel.cpp`'s "Dynamic Chain Physics" section (line 649
  onward, verified against the current file) already resolves, for the
  currently-inspected entity:
  ```cpp
  DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity);
  DynamicChainRigCache::ModelEntry* model = physicsSystem.GetDynamicChainRigCache().TryGetMutable(rig->meshGtaPath);
  ```
  — i.e. it already has BOTH the exact absolute `*.gta` path (`rig->
  meshGtaPath`) AND a live, mutable reference to every chain/joint's CURRENT
  `DynamicJointSettings` (`model->chains`) sitting right there in scope,
  inside the very same `if (DynamicChainRig* rig = ...)` block this phase adds
  its button to.
- Persisting requires: read the file, decode its EXISTING `RigFileData`
  (never fabricate a fresh, empty one — see PHASE0's risk register), replace
  only `jointPhysicsOverrides`, re-encode, re-write — all of which are
  operations on plain data (`GtaFileData`, `RigFileData`,
  `DynamicChainDefinition`), with **zero ImGui dependency**. Per `AGENTS.md`'s
  "Editor Module Structure" (only the ImGui-facing call site belongs under
  `src/Editor/`) and "Testability & Regression Safety" ("extract as a small
  pure function... before wiring new logic directly into a GPU/SDL-owning
  class"), the actual save logic must NOT live inside `InspectorPanel.cpp`
  itself — it must be a small, independently testable function under
  `src/Game/Physics/` (the same layer that already owns
  `DynamicChainRigCache`/`PhysicsSystem`), called FROM `InspectorPanel.cpp`.
- **(v2)** PHASE3 already added `MeshInstantiationSystem&
  meshInstantiationSystem` as a new parameter to BOTH `BuildInspectorPanel()`
  overloads, and `MeshInstantiationSystem::RefreshCachedJointPhysicsOverridesFromDisk()`
  as the one call needed to keep THIS SESSION'S `MeshAssetGpuCatalog` cache
  entry (if any) fresh after a save. This phase's button handler is the one
  production call site for it.
- **(v2)** `InspectorPanel.cpp` currently contains ZERO function-local
  `static` variables anywhere (verified: a text search for `static ` in the
  current file matches only enum-label strings like `"Static (follows
  bone)"` and comments about PMX `RigidBodyMotionType::Static` — never an
  actual `static` local declaration). This fact directly resolves v1's open
  question — see Step 3.3 below.

## Step 3: The Plan

### 3.1 — New module: `src/Game/Physics/DynamicChainPhysicsPersistence.h`

```cpp
#pragma once
#include "../../Physics/DynamicChainDefinition.h"

#include <string>
#include <vector>

namespace gte {

// task_manager/verlet-integration-11, PHASE4 - the ONE sanctioned way to
// persist a model's CURRENT, live DynamicJointSettings (damping/stiffness/
// mass - see DynamicChainDefinition.h) back into its own *.gta file's
// metadata section (RigFileData::jointPhysicsOverrides, PHASE1), so a future
// PhysicsSystem::RegisterDynamicChains() (PHASE2) re-applies them the next
// time this model path is instantiated, in this session or any future one.
//
// Flattens EVERY joint of EVERY chain in `chains` into one JointPhysicsOverride
// per joint (keyed by DynamicChainDefinition::jointBoneIndices - see
// PHASE0_MASTER_STRATEGY.md, Step 2.4) - this is a full SNAPSHOT of the
// model's current joint tuning, not a diff of only what a user actually
// touched this session; every joint's current value (whether hand-edited or
// still exactly the auto-detected default) is written, which is what makes a
// later ApplyJointPhysicsOverrides() (PHASE2) call reproduce the saved state
// exactly, with no ambiguity about which joints were "really" edited.
//
// Reads the EXISTING *.gta at `absoluteGtaPath`, decodes its CURRENT
// RigFileData, and rewrites ONLY jointPhysicsOverrides before writing the
// file back out - every other piece of that file (mesh payload, GUID, flags,
// version, skeleton, skin weights, morphs, materials) is preserved
// byte-for-byte/value-for-value unchanged. Deliberately conservative on
// failure: returns false (and writes NOTHING to disk) if the file can't be
// read, isn't an AssetType::Mesh *.gta, or its existing metadata can't be
// decoded as a RigFileData - this function must NEVER silently replace an
// undecodable file's metadata with a mostly-empty, freshly-constructed
// RigFileData, which would destroy that file's real skeleton/skin-weight/
// morph/material data (see PHASE0_MASTER_STRATEGY.md's risk register).
//
// Pure w.r.t. `chains` (never mutates it) - the one side effect is the file
// write itself. No ImGui/Editor dependency whatsoever, so this is callable
// (and Tier-1-testable against a real temp file, matching GtaFileTests.cpp/
// RigFileTests.cpp's own existing precedent for real-file-I/O tests with no
// GPU/SDL/ImGui involved) from anywhere, not just Panels/InspectorPanel.cpp.
// Does NOT itself refresh any MeshAssetGpuCatalog cache entry - that is a
// deliberately separate concern (PHASE3's RefreshCachedJointPhysicsOverridesFromDisk(),
// called by this phase's own Editor button handler, Step 3.3 below) so this
// function stays free of any Game/Instantiation-layer dependency and remains
// trivially unit-testable on its own.
//
// `outErrorMessage`, if non-null, is set to a short, human-readable reason on
// failure (for a future Editor toast/log line) - left untouched on success.
bool SaveJointPhysicsOverridesToGtaFile(const std::string& absoluteGtaPath,
    const std::vector<DynamicChainDefinition>& chains, std::string* outErrorMessage = nullptr);

} // namespace gte
```

### 3.2 — Implementation: `src/Game/Physics/DynamicChainPhysicsPersistence.cpp`

```cpp
#include "DynamicChainPhysicsPersistence.h"

#include "../../Assets/AssetTypes.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/RigFile.h"

#include <filesystem>

namespace gte {

namespace {

// Mirrors MeshAssetGpuCatalog.cpp's own Utf8PathFromGamePath() - same
// std::u8string round-trip, same reasoning (a std::string holding UTF-8 game/
// asset-relative-or-absolute path text must be reinterpreted through
// std::u8string before handing it to std::filesystem::path, or non-ASCII
// characters get mis-decoded via the platform's native narrow encoding on
// Windows). Duplicated here rather than shared/exported from
// MeshAssetGpuCatalog.cpp specifically because that function is an anonymous-
// namespace implementation detail of a class in a different layer
// (Game/Instantiation) - not a case to introduce a new shared utility header
// for; if a THIRD call site ever needs this, promote it then.
std::filesystem::path Utf8PathFromGamePath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

void SetError(std::string* outErrorMessage, const char* message)
{
    if (outErrorMessage != nullptr) {
        *outErrorMessage = message;
    }
}

} // namespace

bool SaveJointPhysicsOverridesToGtaFile(
    const std::string& absoluteGtaPath, const std::vector<DynamicChainDefinition>& chains, std::string* outErrorMessage)
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

    std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    if (!rig.has_value()) {
        // Deliberately refuse rather than fabricate a fresh RigFileData -
        // see this function's own header comment. A Mesh *.gta with a
        // DynamicChainRig-bearing entity spawned from it is guaranteed to
        // already have decodable rig metadata (PhysicsSystem::
        // RegisterDynamicChains() would never have detected any chain
        // otherwise) - reaching this branch means something is genuinely
        // wrong with the file, not merely "this model has no rig data yet."
        SetError(outErrorMessage, "This *.gta file's existing metadata could not be decoded - refusing to overwrite it.");
        return false;
    }

    // Flatten every joint of every chain into one JointPhysicsOverride each -
    // a full current-state snapshot, not a diff (see header comment).
    std::vector<JointPhysicsOverride> overrides;
    std::size_t totalJoints = 0;
    for (const DynamicChainDefinition& chain : chains) {
        totalJoints += chain.jointBoneIndices.size();
    }
    overrides.reserve(totalJoints);
    for (const DynamicChainDefinition& chain : chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            const DynamicJointSettings& settings = chain.jointSettings[i];
            overrides.push_back(JointPhysicsOverride{ chain.jointBoneIndices[i], settings.damping, settings.stiffness, settings.mass });
        }
    }
    rig->jointPhysicsOverrides = std::move(overrides);

    const std::vector<std::uint8_t> newMetadata = EncodeRigDataToBytes(*rig);
    const bool wrote = WriteGtaFile(Utf8PathFromGamePath(absoluteGtaPath), gta->header.Type(), gta->header.Id(),
        gta->header.Flags(), newMetadata, gta->payload, gta->header.version);
    if (!wrote) {
        SetError(outErrorMessage, "Failed to write the *.gta file back to disk.");
        return false;
    }
    return true;
}

} // namespace gte
```

Note every header field (`Type()`, `Id()`, `Flags()`, `version`) and the
`payload` byte vector are threaded straight from the just-read `gta` back into
`WriteGtaFile()` completely unchanged — only `newMetadata` (derived from the
mutated `rig`) differs from what was read. This is what guarantees "everything
else in the file is untouched" (PHASE5's own round-trip test verifies this
directly).

### 3.3 — Wire the Editor button: `src/Editor/Panels/InspectorPanel.cpp`

**(v2) Definitive answer to v1's one open question:** the two small pieces of
transient success/failure feedback text this button needs
(`lastJointPhysicsSaveSucceeded`/`lastJointPhysicsSaveError`) are held as
plain function-local `static` variables, scoped as narrowly as possible
(inside the `if (DynamicChainRig* rig = ...)` block itself, not at file/
function scope) — **not** by promoting `InspectorPanel` to a class. This is
now a definite decision, not an implementation-time choice, for three
verified reasons:

1. `InspectorPanel.h`/`.cpp` confirmed to still be a plain free function with
   no class/persistent state of its own (per `AGENTS.md`'s "Editor Module
   Structure": *"a future panel that genuinely needs its own persistent
   state across frames MAY become a small class instead"* — "may", not
   "must"; `BoneViewerWindow`/`ProfilerPanel` needed enough state
   (open/closed state, selection, timeline history) to justify that cost —
   two short-lived cosmetic strings do not).
2. This file currently has **zero** existing function-local `static`
   variables (verified directly: every `static ` match in the current file
   is either an enum-label string literal or a comment about PMX
   `RigidBodyMotionType::Static` — never an actual local `static`
   declaration) — so there is no existing pattern in this specific file this
   choice could conflict with or need to reconcile with.
3. This is purely cosmetic, UI-only feedback text (never read by
   `PhysicsSystem`/ECS/anything outside this one button's own next few
   frames) in a codebase with exactly one Inspector panel and one ImGui
   context — the same "ImGui-pragmatism, ImGui types fine to use directly"
   spirit `AGENTS.md` already extends to other ImGui-facing code.

Inside the existing `if (DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity))`
block (see `PHASE0_MASTER_STRATEGY.md`, Step 2.3, for the exact surrounding
code), immediately after the existing `Checkbox("Enabled")`/`Checkbox("Freeze")`
pair and their `TextDisabled` explanation, and BEFORE the
`DynamicChainRigCache::ModelEntry* model = ...` lookup (so the button is
visible even while `model` may turn out null — though in practice it never
will for an entity that already has a `DynamicChainRig`), add:

The two `static` locals below MUST be declared ONCE, before the `Button()`
call itself, so both the button's own click handler AND the status text
drawn every frame afterward can see the same storage:

```cpp
static bool s_lastJointPhysicsSaveSucceeded = false;
static std::string s_lastJointPhysicsSaveError;

if (ImGui::Button("Save Joint Physics to Asset")) {
    std::string errorMessage;
    const DynamicChainRigCache::ModelEntry* currentModel
        = physicsSystem.GetDynamicChainRigCache().TryGet(rig->meshGtaPath);
    if (currentModel == nullptr) {
        s_lastJointPhysicsSaveError = "No detected dynamic bone chains to save for this model.";
        s_lastJointPhysicsSaveSucceeded = false;
    } else {
        s_lastJointPhysicsSaveSucceeded
            = SaveJointPhysicsOverridesToGtaFile(rig->meshGtaPath, currentModel->chains, &errorMessage);
        s_lastJointPhysicsSaveError = errorMessage;
        if (s_lastJointPhysicsSaveSucceeded) {
            meshInstantiationSystem.RefreshCachedJointPhysicsOverridesFromDisk(rig->meshGtaPath); // PHASE3/4, see above.
        }
    }
}
if (!s_lastJointPhysicsSaveError.empty() || s_lastJointPhysicsSaveSucceeded) {
    if (s_lastJointPhysicsSaveSucceeded) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Saved joint physics to asset.");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Save failed: %s", s_lastJointPhysicsSaveError.c_str());
    }
}
ImGui::TextDisabled(
    "Writes the CURRENT damping/stiffness/mass of every joint below into this model's *.gta file, so it is "
    "restored automatically the next time this model is instantiated (this session or a future one).");
```

`#include "../../Game/Physics/DynamicChainPhysicsPersistence.h"` alongside
this file's existing includes (`MeshInstantiationSystem.h` is already
`#include`d per PHASE3's own Step 3.4).

**On the `s_` prefix:** used purely to visually distinguish these two
frame-persistent statics from every other ordinary per-frame local in this
same block at a glance — not a project-wide naming convention requirement
(this codebase has no existing `static` locals in this file to match a prior
convention against, per Step 2's own verified finding); drop the prefix if a
reviewer prefers plain names, it carries no other significance.

### 3.4 — Tests: new file `tests/Game/Physics/DynamicChainPhysicsPersistenceTests.cpp`

Tier 1-adjacent (real disk I/O against a temp file, exactly like
`tests/Assets/GtaFileTests.cpp` already does — no GPU/SDL/ImGui involved).

```cpp
#include "Game/Physics/DynamicChainPhysicsPersistence.h"
#include "Assets/GtaFile.h"
#include "Assets/MeshFile.h"
#include "Assets/RigFile.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace gte {
namespace {

std::filesystem::path TempGtaPath(const char* name)
{
    return std::filesystem::temp_directory_path() / name;
}

// Builds a real, on-disk Mesh *.gta with a NON-trivial RigFileData (skeleton
// with >= 3 bones, so it can carry a couple of jointPhysicsOverrides-worthy
// bone indices) plus a real mesh payload, and returns the path written to.
std::filesystem::path WriteSampleMeshGtaFile(const std::filesystem::path& path) { /* ... */ }

TEST(DynamicChainPhysicsPersistenceTest, SavesOverridesWithoutDisturbingAnyOtherData)
{
    const std::filesystem::path path = WriteSampleMeshGtaFile(TempGtaPath("JointOverridePersistenceTest.gta"));
    const std::optional<GtaFileData> before = ReadGtaFile(path);
    ASSERT_TRUE(before.has_value());
    const std::optional<RigFileData> rigBefore = DecodeRigDataFromBytes(before->metadata);
    ASSERT_TRUE(rigBefore.has_value());

    DynamicChainDefinition chain;
    chain.rootBoneIndex = 0;
    chain.jointBoneIndices = { 1, 2 };
    chain.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(2);
    chain.jointSettings = { DynamicJointSettings{ 0.6f, 0.03f, 2.0f }, DynamicJointSettings{ 0.8f, 0.01f, 0.5f } };

    std::string error;
    const bool ok = SaveJointPhysicsOverridesToGtaFile(path.string(), { chain }, &error);
    ASSERT_TRUE(ok) << error;

    const std::optional<GtaFileData> after = ReadGtaFile(path);
    ASSERT_TRUE(after.has_value());
    // --- Header fields unchanged ---
    EXPECT_EQ(after->header.Id(), before->header.Id());
    EXPECT_EQ(after->header.Flags(), before->header.Flags());
    EXPECT_EQ(after->header.version, before->header.version);
    EXPECT_EQ(after->header.Type(), AssetType::Mesh);
    // --- Payload (mesh geometry) byte-for-byte unchanged ---
    EXPECT_EQ(after->payload, before->payload);

    const std::optional<RigFileData> rigAfter = DecodeRigDataFromBytes(after->metadata);
    ASSERT_TRUE(rigAfter.has_value());
    // --- Everything besides jointPhysicsOverrides unchanged ---
    EXPECT_EQ(rigAfter->skeleton.bones.size(), rigBefore->skeleton.bones.size());
    EXPECT_EQ(rigAfter->skinWeights.size(), rigBefore->skinWeights.size());
    EXPECT_EQ(rigAfter->materials.materials.size(), rigBefore->materials.materials.size());
    // --- The new overrides ARE present, matching exactly what was passed in ---
    ASSERT_EQ(rigAfter->jointPhysicsOverrides.size(), 2u);
    EXPECT_EQ(rigAfter->jointPhysicsOverrides[0].boneIndex, 1);
    EXPECT_FLOAT_EQ(rigAfter->jointPhysicsOverrides[0].damping, 0.6f);
    EXPECT_EQ(rigAfter->jointPhysicsOverrides[1].boneIndex, 2);
    EXPECT_FLOAT_EQ(rigAfter->jointPhysicsOverrides[1].mass, 0.5f);
}

TEST(DynamicChainPhysicsPersistenceTest, FailsGracefullyWhenFileDoesNotExist)
{
    std::string error;
    EXPECT_FALSE(SaveJointPhysicsOverridesToGtaFile((std::filesystem::temp_directory_path() / "DoesNotExist.gta").string(),
        {}, &error));
    EXPECT_FALSE(error.empty());
}

TEST(DynamicChainPhysicsPersistenceTest, FailsGracefullyOnANonMeshAssetType)
{
    const std::filesystem::path path = TempGtaPath("NotAMesh.gta");
    ASSERT_TRUE(WriteGtaFile(path, AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));

    std::string error;
    EXPECT_FALSE(SaveJointPhysicsOverridesToGtaFile(path.string(), {}, &error));
    EXPECT_FALSE(error.empty());
}

TEST(DynamicChainPhysicsPersistenceTest, OverwritingASecondTimeReplacesRatherThanAccumulatingOverrides)
{
    const std::filesystem::path path = WriteSampleMeshGtaFile(TempGtaPath("JointOverrideReplaceTest.gta"));

    DynamicChainDefinition chain;
    chain.jointBoneIndices = { 1 };
    chain.jointSettings = { DynamicJointSettings{ 0.1f, 0.1f, 1.0f } };
    ASSERT_TRUE(SaveJointPhysicsOverridesToGtaFile(path.string(), { chain }, nullptr));

    chain.jointSettings = { DynamicJointSettings{ 0.9f, 0.9f, 9.0f } }; // Same joint, new values.
    ASSERT_TRUE(SaveJointPhysicsOverridesToGtaFile(path.string(), { chain }, nullptr));

    const std::optional<GtaFileData> gta = ReadGtaFile(path);
    ASSERT_TRUE(gta.has_value());
    const std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    ASSERT_TRUE(rig.has_value());
    ASSERT_EQ(rig->jointPhysicsOverrides.size(), 1u); // NOT 2 - the second save REPLACED, not appended.
    EXPECT_FLOAT_EQ(rig->jointPhysicsOverrides[0].damping, 0.9f);
}

} // namespace
} // namespace gte
```

(These four tests are unchanged from v1 — `SaveJointPhysicsOverridesToGtaFile()`
itself has no `MeshInstantiationSystem` dependency, so none of them need to
change to account for the new PHASE3 refresh call, which lives ONLY in the
Editor button handler, Step 3.3 above, never inside the persistence function
itself.)

### What This Phase Deliberately Does NOT Do

- Does not add an "Are you sure?" confirmation modal, undo support, or any
  autosave/dirty-tracking - a single explicit button click, matching the
  Editor's existing "no Command-pattern/undo system yet" state (see
  `AGENTS.md`, "Editor Module Structure", `Selection.h`'s own "deliberately
  just a plain gate-keeper with no history/undo of its own" note).
- Does not add a corresponding "Revert to Saved" / "Reload from Asset" button
  — a user who wants to discard live edits can already do so by respawning
  the model (out of scope to add a dedicated one-click reload path here).
- Does not touch `AssetImporter.cpp`/`PmxLoader.cpp` for its OWN concerns —
  this save path only ever RE-writes an already-imported Mesh `*.gta`, never
  re-imports from a source `.pmx` (PHASE1's separate 3.9 step already
  protects a re-import from destroying whatever THIS phase's button
  previously saved).
- **(v2)** Does not itself implement `RefreshCachedJointPhysicsOverridesFromDisk()`
  — that method and its full contract belong entirely to PHASE3; this phase
  only adds the ONE call to it, at the ONE point it is actually needed
  (immediately after a successful save).
