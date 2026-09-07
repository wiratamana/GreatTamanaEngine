# PHASE1 — Chain Lookup and Selection Foundation (`src/Editor/Selection.h`, `src/Physics/DynamicChainDefinition.h`)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprits B, C, E — the parts of each
this phase alone can close). Depends on: nothing new (only already-existing,
already-compiling code). Produces: `ModelPartKind::Verlet`, plus a new,
pure, zero-ECS/zero-Editor `FindDynamicChainJointByBoneIndex()` helper in
`src/Physics/DynamicChainDefinition.h`, both fully unit-tested. Nothing in
this phase touches `BoneViewerWindow.h/.cpp` or
`Panels/InspectorPanel.h/.cpp` at all — it exists purely so Phases 2 and 3
each have a stable, already-tested foundation to build on.

## Step 1: The Goal

1. Give `Selection`'s `ModelPartKind` enum a 4th value, `Verlet`, so every
   later phase can write `ModelPartKind::Verlet` and
   `ctx.selection.SelectModelPart(entity, ModelPartKind::Verlet, boneIndex)`
   exactly like the existing three values already work.
2. Decide, explicitly and in writing (this document), that a Verlet
   particle's `partIndex` **is its skeleton bone index** — not a freshly
   invented "0..N-1 flattened joint counter" — and provide the one small,
   pure lookup function that turns a bone index back into "which chain, and
   which position within that chain" for any caller that needs to actually
   read/edit that joint's settings (Phases 2 and 3 both do).
3. Prove both are correct via dedicated Tier-1 tests before either later
   phase depends on them.

## Step 2: The Situation / The Problem

`Selection.h`'s `ModelPartKind` enum (see its own class comment) has
exactly three values, each mapped to a single `int partIndex` per
`Selection::SelectModelPart()`/`SelectModelParts()`/`SelectedModelPartIndices()`.
For Bone/Rigid Body/Joint, `partIndex` is simply "this thing's own position
in `BoneViewerWindow`'s own flat `m_bones`/`m_rigidBodies`/`m_joints`
array" — a trivial, dense, 0..count-1 identity that both `BoneViewerWindow`
(building `m_bones[i]`) and `InspectorPanel.cpp` (indexing
`rig->skeleton.bones[index]`) already independently, but consistently, rely
on.

A Verlet particle has no such natural "flat array" home of its own —
`DynamicChainDefinition::jointBoneIndices` is a per-CHAIN list of bone
indices, and a model can have any number of independent chains
(`DynamicChainRigCache::ModelEntry::chains`, a `std::vector<
DynamicChainDefinition>`). Two candidate index schemes exist:

- **A freshly flattened "every joint of every chain, concatenated in
  chain order" counter.** This requires BOTH `BoneViewerWindow` (building
  the tree/overlay) AND `InspectorPanel` (resolving a selection back to a
  joint) to independently re-flatten `model->chains` in the exact same
  order every single time, a "two independently-written iterations must
  never desync" trap this codebase's own conventions (see
  `AGENTS.md`'s "never hand-roll a second, subtly different copy of the
  same walk" rule, already invoked once for `Animation/
  BoneChainResolver.h`) explicitly warns against.
- **The bone index itself**, i.e. exactly the same `std::int32_t` already
  sitting in `DynamicChainDefinition::jointBoneIndices[j]`. This requires no
  flattening/re-derivation at all — `partIndex` IS
  `chain.jointBoneIndices[j]`, verbatim. The only question is uniqueness:
  can the same bone index legitimately appear as a JOINT in two different
  chains at once? No — `DetectDynamicChains()`'s own documented contract
  (`Physics/DynamicChainDetection.h`) guarantees a bone with more than one
  `deformAfterPhysics` child STARTS A NEW CHAIN PER CHILD rather than
  folding into one branching definition, and a bone can only ever have one
  parent, so a bone can be `jointBoneIndices[j]` of at most one chain, ever.
  (A bone CAN simultaneously be one chain's `rootBoneIndex` — the anchor,
  never itself simulated/selectable as a joint — while also being a
  DIFFERENT chain's own joint or another's root; that is fine and does not
  create any joint-selection ambiguity, since `rootBoneIndex` is never a
  candidate `partIndex` in this scheme at all — see Phase 2's own "root/
  anchor marker is drawn but not independently selectable" decision.)

This phase picks the bone-index scheme: it is unique, requires no parallel
flattening logic anywhere, and — as a pleasant side effect — means the SAME
numeric index a user sees while in Bone mode (`ModelPartKind::Bone`,
`partIndex` = bone index) refers to the exact same underlying bone when
later viewed as a Verlet joint (`ModelPartKind::Verlet`, `partIndex` = same
bone index) — the two modes share one index space by construction, not by
coincidence.

Given that, every later consumer (Phase 2's overlay/tree click handling,
Phase 3's Inspector section) needs exactly one small, pure, reusable
function: "given `model->chains` and a bone index, which
`(chainIndex, jointIndexInChain)` does it correspond to, if any?" Writing
this function independently, twice, in `BoneViewerWindow.cpp` AND
`InspectorPanel.cpp` would be exactly the "two subtly different hand-rolled
copies" anti-pattern `AGENTS.md` already calls out once for bone-ancestor
walking — so it belongs in `src/Physics/`, alongside the data it operates
on, written and tested exactly once.

## Step 3: The Plan

### 3.1 `src/Editor/Selection.h` — add `ModelPartKind::Verlet`

```cpp
enum class ModelPartKind {
    Bone,
    RigidBody,
    Joint,
    Verlet, // task_manager/verlet-integration-5 - a physics-simulated
            // ("jiggle") bone chain joint, drawn/selected in the Bone
            // Viewer's "Verlet" mode - see BoneViewerWindow.h. partIndex
            // for this kind is the joint's own SKELETON BONE INDEX (the
            // same index space Bone mode already uses), NOT a freshly
            // flattened per-chain joint counter - see
            // Physics/DynamicChainDefinition.h's
            // FindDynamicChainJointByBoneIndex() for how a caller turns
            // this back into "which chain, which position in it."
};
```

This is a pure, additive enum change — every existing method on `Selection`
(`SelectModelPart()`, `SelectModelParts()`, `ToggleModelPartInSelection()`,
`ClearModelPartIfEntity()`, `IsModelPartSelected()`, `SelectedModelPartKind()`,
`SelectedModelPartIndices()`) already operates generically on whatever
`ModelPartKind` value is passed to it — **none of their bodies need to
change**, only this one enum declaration in `Selection.h`. Verify this by
re-reading `Selection.cpp` (nothing there switches on `ModelPartKind` at
all today) — confirmed nothing to change there.

**Audit note (Culprit B, partial closure):** grep the whole `src/Editor/`
tree for `ModelPartKind::Joint` (the current last enum value) to find every
place written as an exhaustive branch that silently needs a 4th case/branch
now that `Verlet` exists after it. This phase's own scope only closes
whichever of these are trivial, self-contained one-liners with zero new
UI/behavior to design (documented below); anything requiring new UI/data
plumbing is explicitly deferred to its own later phase (Phase 2 for
`BoneViewerWindow.cpp`, Phase 3 for `InspectorPanel.cpp`'s single-selection
switch, Phase 4 for `InspectorPanel.cpp`'s multi-selection summary) — do
NOT attempt to fix `BoneViewerWindow.cpp`/`InspectorPanel.cpp` in this
phase; they are intentionally out of scope here so Phase 1 stays a small,
self-contained, GPU/ImGui-free change.

### 3.2 `src/Physics/DynamicChainDefinition.h` — `FindDynamicChainJointByBoneIndex()`

Add, at the bottom of the existing file (after `DynamicChainDefinition`
itself, still inside `namespace gte`):

```cpp
// The result of FindDynamicChainJointByBoneIndex() below - "no match" is
// represented as { -1, -1 }, never a thrown exception/assert (a bone that
// is not a physics-driven joint at all - the overwhelmingly common case for
// most bones in most models - is an entirely normal, expected input, not an
// error).
struct DynamicChainJointLocation {
    std::int32_t chainIndex = -1;
    std::int32_t jointIndexInChain = -1;

    bool IsValid() const noexcept { return chainIndex >= 0 && jointIndexInChain >= 0; }
};

// Finds which chain (if any) has `boneIndex` as one of its OWN
// jointBoneIndices entries, and at what position within that chain's own
// ordered list. Returns a default-constructed (invalid, both fields -1)
// DynamicChainJointLocation if `boneIndex` is not a joint of ANY chain in
// `chains` - e.g. it is an ordinary (non-physics-driven) bone, or it is
// some chain's OWN rootBoneIndex (the anchor - see
// DynamicChainDefinition::rootBoneIndex's own doc comment: the root is
// never itself a member of jointBoneIndices, by construction, so it never
// matches here either).
//
// A bone index can appear in jointBoneIndices of AT MOST ONE chain, ever -
// see DynamicChainDetection.h's own DetectDynamicChains() contract (a
// branch point starts one NEW chain per child rather than folding into a
// shared definition, and a bone has exactly one parent) - so this always
// returns at most one match; the moment one is found, this returns
// immediately without scanning the remaining chains.
//
// Deliberately pure/free (no ECS, no Editor, no GPU) so both
// src/Editor/BoneViewerWindow.cpp (task_manager/verlet-integration-5,
// Phase 2 - resolving a clicked gizmo dot/tree row back to its chain) and
// src/Editor/Panels/InspectorPanel.cpp (Phase 3 - resolving the current
// Model-Part selection back to the exact DynamicJointSettings to show/
// edit) can share ONE tested implementation, rather than each hand-rolling
// their own subtly-different linear scan.
DynamicChainJointLocation FindDynamicChainJointByBoneIndex(
    const std::vector<DynamicChainDefinition>& chains, std::int32_t boneIndex);
```

Implementation (new `.cpp` — see 3.3 below — this header today has no
matching `.cpp`, since `DynamicChainDefinition` itself is plain data with no
behavior; this is the first FUNCTION this file's data model needs, so it
gains its first `.cpp` here):

```cpp
DynamicChainJointLocation FindDynamicChainJointByBoneIndex(
    const std::vector<DynamicChainDefinition>& chains, std::int32_t boneIndex)
{
    if (boneIndex < 0) {
        return DynamicChainJointLocation{};
    }
    for (std::size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex) {
        const std::vector<std::int32_t>& joints = chains[chainIndex].jointBoneIndices;
        for (std::size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex) {
            if (joints[jointIndex] == boneIndex) {
                return DynamicChainJointLocation{
                    static_cast<std::int32_t>(chainIndex), static_cast<std::int32_t>(jointIndex) };
            }
        }
    }
    return DynamicChainJointLocation{};
}
```

### 3.3 `src/Physics/DynamicChainDefinition.cpp` (new file)

`DynamicChainDefinition.h` is currently a header-only, pure-data file (no
matching `.cpp`, confirmed by directory listing — `src/Physics/` has no
`DynamicChainDefinition.cpp` today). Create one, containing exactly the
`#include "DynamicChainDefinition.h"` plus the function body from 3.2
above, `namespace gte { ... }`. Add it to `CMakeLists.txt`'s existing
`src/Physics/*.cpp` source list (alongside `DynamicChainDetection.cpp`,
`ChainConstraints.cpp`, etc. — find the exact list via the existing
`DynamicChainDetection.cpp` entry and add this new file immediately next to
it, same directory grouping).

### 3.4 Tests

Add `tests/Physics/DynamicChainDefinitionTests.cpp` (new file — mirrors the
existing `tests/Physics/DynamicChainDetectionTests.cpp`'s own style/
framework exactly):

- A `std::vector<DynamicChainDefinition>` with two hand-built chains: chain
  0 has `jointBoneIndices = {2, 3, 4}`, chain 1 has `jointBoneIndices =
  {7, 8}`. `FindDynamicChainJointByBoneIndex(chains, 3)` returns
  `{chainIndex=0, jointIndexInChain=1}`. `FindDynamicChainJointByBoneIndex(chains, 8)`
  returns `{chainIndex=1, jointIndexInChain=1}`.
- A bone index that is NOT in either chain's `jointBoneIndices` (e.g. `5`,
  or one of the chains' own `rootBoneIndex` values) returns an invalid
  (`IsValid() == false`, both fields `-1`) result.
- A negative `boneIndex` (e.g. `-1`) returns invalid immediately, without
  scanning.
- An empty `chains` vector returns invalid for any input.
- `DynamicChainJointLocation::IsValid()` itself is asserted true/false in
  the obvious cases above (both fields set vs. both fields `-1` — there is
  no partially-valid state this function ever produces, assert that too:
  a location is never `{chainIndex >= 0, jointIndexInChain == -1}` or vice
  versa).

Add a matching descriptive paragraph for this new test file to
`tests/CMakeLists.txt`'s own header "Test taxonomy" comment block
(immediately alongside the existing `Physics/DynamicChainDetectionTests.cpp`
entry), and add the file itself to the actual test-executable source list
lower in that same file.

No test changes are needed for `Selection.h`'s enum addition itself (3.1) —
`tests/Editor/SelectionTests.cpp` today tests `Selection`'s methods
generically across whatever `ModelPartKind` values are passed to them; it
does not enumerate all values anywhere, so it needs no edit here. (Phase 4
revisits `SelectionTests.cpp` only if Phase 2/3's own behavior turns up a
gap — track that there, not here.)

## Step 4: What We Will NOT Do

- We will **not** touch `BoneViewerWindow.h/.cpp` or `Panels/
  InspectorPanel.h/.cpp` in this phase at all — every exhaustive
  switch/ternary in those two files that needs a 4th branch is Phase 2/3/4's
  own job, tracked there, not here. This phase's own diff is confined to
  `Selection.h`, `Physics/DynamicChainDefinition.h`, a new
  `Physics/DynamicChainDefinition.cpp`, `CMakeLists.txt`, and
  `tests/Physics/DynamicChainDefinitionTests.cpp` + `tests/CMakeLists.txt`.
- We will **not** invent a flattened "joint counter" index scheme — see
  Step 2's full reasoning for why bone index is the correct, permanent
  choice; do not revisit this decision in a later phase.
- We will **not** make `FindDynamicChainJointByBoneIndex()` also search
  `rootBoneIndex` fields — a chain's root is never independently selectable
  as a Verlet joint (Phase 2's own scope decision), so this function
  correctly has no notion of "found as a root" at all, only "found as a
  joint" or "not found."

## Step 5: Their Role

1. Add `ModelPartKind::Verlet` to `Selection.h` (3.1) — a one-line enum
   addition plus its doc comment; confirm `Selection.cpp` needs no change.
2. Add `DynamicChainJointLocation`/`FindDynamicChainJointByBoneIndex()`'s
   declaration to `Physics/DynamicChainDefinition.h` (3.2) and its
   definition in a new `Physics/DynamicChainDefinition.cpp` (3.3); wire the
   new `.cpp` into `CMakeLists.txt`.
3. Write `tests/Physics/DynamicChainDefinitionTests.cpp` (3.4) covering
   every case listed there; register it in `tests/CMakeLists.txt` (both the
   taxonomy comment and the actual source list).
4. Build and run the full test suite — this phase adds no ImGui/GPU-
   touching code at all, so a plain `tests` target build+run is a complete,
   sufficient verification for this phase in isolation, with nothing
   visual to check yet (Phase 2 is where this becomes visible).
