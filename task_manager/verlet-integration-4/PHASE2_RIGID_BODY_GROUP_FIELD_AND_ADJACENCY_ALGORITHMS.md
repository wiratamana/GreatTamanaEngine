# PHASE2 — Rigid Body `group` Field + Pure Group/Branch Selection Algorithms (v2)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprits B, C, D). Depends on: nothing
from Phase 1 directly (this phase touches different files), but is ordered
after it per the master strategy's own phase numbering; may be developed in
parallel with Phase 1 if desired since neither phase's files overlap, though
Phase 3 needs BOTH Phase 1 and Phase 2 to exist. Produces: the missing
`group` data field on `BoneViewerWindow::RigidBodyEntry`, a joint-adjacency
cache on `BoneViewerWindow`, and a brand new pure, Tier-1-tested module,
`src/Editor/RigidBodyGroupSelection.h/.cpp`, containing the actual
"select all group"/"select all branch" algorithms — completely decoupled
from ImGui/Vulkan/`Selection`, exactly like `RigidBodyWireframe.h` already is
for its own, unrelated problem (per-shape geometry).

**v2 revision note**: unchanged from v1 except one documentation-precision
fix, no functional/behavioral change at all — `SelectRigidBodyBranch()`'s own
doc comment (3.1 below) previously said it "walks outward ... in both
directions," wording that reads like a breadth-first search; the real
implementation is an explicit stack (`pending.back()`/`pop_back()`), i.e. a
depth-first traversal order. This has zero effect on the RESULT (it is
`std::sort()`ed before returning, so the final member set is
traversal-order-independent — every one of this phase's own tests below
still passes exactly as originally written), but the wording is corrected
below so a future reader never assumes a specific (BFS/"distance from seed")
visitation order from it. See `PHASE0_MASTER_STRATEGY.md`'s Revision Notes,
finding #5. Every field name/signature/line citation below was re-verified
against the live source tree and is still byte-for-byte accurate.

## Step 1: The Goal

1. `BoneViewerWindow::RigidBodyEntry` gains a `std::uint8_t group` field,
   populated from `RigidBody::group` (`src/Assets/PhysicsData.h`) in
   `EnsureDataLoaded()` — the data "select all group" needs to compare
   against, which does not exist anywhere in the overlay's own data model
   today.
2. A new, pure function, `SelectRigidBodiesByGroup(groups, seedIndex)`,
   returns every rigid-body index (including the seed) whose `group` value
   matches the seed's own — a flat property comparison, no graph involved.
3. A new, pure function, `BuildRigidBodyAdjacency(edges, rigidBodyCount)`,
   turns the flat list of Joint rigid-body-pairs into a genuine per-rigid-body
   adjacency list (`adjacency[i]` = every OTHER rigid body directly joined to
   body `i` by at least one Joint) — the graph a branch-walk needs, which
   does not exist anywhere today (`BoneViewerWindow::m_joints` is Joint-
   centric, not rigid-body-centric — see `PHASE0_MASTER_STRATEGY.md`'s
   Culprit C).
4. A new, pure function, `SelectRigidBodyBranch(adjacency, seedIndex)`, walks
   outward from `seedIndex` over that adjacency graph, collecting every
   reachable rigid body EXCEPT any "branch" node (a rigid body connected to
   3 or more OTHER rigid bodies at once) — matching the story's own
   `+--A--+` / `|     |` diagram exactly: the `A` segment (every body with
   degree <= 2 along the seed's own chain) is selected; both `+` junction
   endpoints (degree >= 3) are excluded, and the walk never continues past
   them into whatever lies beyond.
5. `BoneViewerWindow` gains a `m_rigidBodyAdjacency` cache, rebuilt via a new
   `RebuildRigidBodyAdjacencyIndex()` method called alongside the existing
   `RebuildBoneHierarchyIndex()` inside `EnsureDataLoaded()`, so Phase 3's
   "Select All (Branch)" button has a ready-built graph to hand to
   `SelectRigidBodyBranch()` without recomputing it every click.

## Step 2: The Situation / The Problem

`src/Assets/PhysicsData.h`'s `RigidBody` struct already has (today, lines
54-60):

```cpp
    // Collision filtering: `group` is this body's own single-bit group
    // membership (0-15), `collisionGroupMask` is the bitmask of groups it IS
    // ALLOWED to collide with - matches Bullet's own btCollisionObject
    // group/mask convention exactly (this data is shaped for eventual direct
    // hand-off to Bullet or an equivalent backend).
    std::uint8_t group = 0;
    std::uint16_t collisionGroupMask = 0;
```

already decoded correctly by `PmxLoader.cpp` and already shown read-only in
`InspectorPanel.cpp`'s `BuildModelPartInspector()` (`"Collision Group: %u"`,
line 224) — this field is NOT missing from the source data at all, only from
`BoneViewerWindow`'s own flattened copy of it.

`BoneViewerWindow.h`'s `RigidBodyEntry` (today, lines 137-144):

```cpp
    struct RigidBodyEntry {
        std::string name;
        Vec3 translate;
        Vec3 rotateRadians; // Euler, PMX convention (see PhysicsData.h) - used only for an approximate visual orientation hint.
        RigidBodyShape shape = RigidBodyShape::Sphere;
        Vec3 shapeSize;
        std::int32_t boneIndex = -1; // Index into m_bones this body is attached to (-1 if unattached) - drawn as a connecting line.
    };
```

has no `group` field, and `EnsureDataLoaded()` (today, `BoneViewerWindow.cpp`
lines 398-402) builds each entry as:

```cpp
        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(
                RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape, body.shapeSize, body.boneIndex });
        }
```

— `body.group` is read from `rig->physics.rigidBodies` right there in scope,
but simply never copied across.

`JointEntry` (today, `BoneViewerWindow.h` lines 147-152) is Joint-centric:

```cpp
    struct JointEntry {
        std::string name;
        Vec3 translate;
        std::int32_t rigidBodyAIndex = -1; // Index into m_rigidBodies (-1 if out of range/unset).
        std::int32_t rigidBodyBIndex = -1;
    };
```

Nothing anywhere inverts this into "which rigid bodies does body N touch" —
the only consumer of `rigidBodyAIndex`/`rigidBodyBIndex` today is `Build()`'s
Joint-mode overlay block (drawing two connector lines per joint), which never
needed a rigid-body-centric view.

## Step 3: The Plan

### 3.1 New file: `src/Editor/RigidBodyGroupSelection.h`

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {

// One joint's rigid-body endpoints only - the minimal shape
// BuildRigidBodyAdjacency() needs, deliberately decoupled from the full
// Joint/BoneViewerWindow::JointEntry struct (src/Assets/PhysicsData.h /
// BoneViewerWindow.h) so this module stays a pure, dependency-free graph/
// selection algorithm, exactly like RigidBodyWireframe.h is a pure,
// dependency-free geometry builder for a different problem (see
// task_manager/verlet-integration-3/PHASE1_RIGID_BODY_WIREFRAME_GEOMETRY_MODULE.md).
struct RigidBodyJointEdge {
    std::int32_t rigidBodyAIndex = -1;
    std::int32_t rigidBodyBIndex = -1;
};

// Builds a per-rigid-body adjacency list - adjacency[i] lists every OTHER
// rigid-body index directly connected to body i by at least one joint edge -
// from a flat list of joint rigid-body-pairs. `rigidBodyCount` bounds the
// returned vector's size (index i exists in the result for every
// 0 <= i < rigidBodyCount, even if it ends up with zero neighbors) and is
// also used to defensively drop any out-of-range edge (mirrors this
// codebase's own established "never trust indices blindly" convention - see
// BoneViewerWindow.cpp's own bounds checks before indexing m_rigidBodies/
// m_bones). A self-referencing edge (rigidBodyAIndex == rigidBodyBIndex) is
// never a real connection and is dropped. Duplicate edges (the same pair
// joined by more than one Joint) are naturally absorbed - adjacency[i] never
// lists the same neighbor twice. This is the graph SelectRigidBodyBranch()
// below walks.
std::vector<std::vector<std::int32_t>> BuildRigidBodyAdjacency(
    const std::vector<RigidBodyJointEdge>& edges, std::size_t rigidBodyCount);

// Returns every rigid-body index (INCLUDING `seedIndex` itself) whose
// groups[i] equals groups[seedIndex] - the "select all group" story
// requirement, verbatim: PMX collision `group` (0-15,
// src/Assets/PhysicsData.h's own RigidBody::group) is the only notion of
// "group" this engine's rigid bodies have; this is a flat property
// comparison across the WHOLE list, no adjacency/graph involved at all.
// Returns an empty result if `seedIndex` is out of range for `groups`
// (nothing to compare against). Result is ascending-sorted (ready to hand
// straight to Selection::SelectModelParts() - see task_manager/
// verlet-integration-4/PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md).
std::vector<std::int32_t> SelectRigidBodiesByGroup(const std::vector<std::uint8_t>& groups, std::int32_t seedIndex);

// Walks outward from `seedIndex` over `adjacency` (see
// BuildRigidBodyAdjacency() above) via a plain graph walk - the exact
// visitation ORDER is deliberately unspecified/implementation-defined (the
// reference implementation below happens to use an explicit stack, i.e.
// depth-first order, but this is not part of this function's contract and
// must never be relied upon by a caller) - collecting every reachable rigid
// body EXCEPT a "branch" node - one connected to 3 or more OTHER rigid
// bodies at once, i.e. a point where more than one chain meets (the story's
// own "+" in "+--A--+"/"|     |": a corner where the horizontal chain and a
// vertical chain both meet, PLUS at least one further, unshown connection,
// is what makes it a genuine branch rather than just an ordinary two-
// neighbor corner). A branch node itself is EXCLUDED from the result, and
// expansion NEVER continues through it (none of ITS OTHER neighbors are
// visited via it) - so the result is exactly the single uninterrupted chain
// of degree-<=2 bodies the seed belongs to, stopping cleanly at every branch
// point on either side, never leaking into a neighboring chain on the far
// side of a junction. If `seedIndex` ITSELF is already a branch node (there
// is no single unambiguous chain to walk - which of its 3+ directions would
// even be "the branch"?), the result is just `{ seedIndex }` alone, by
// design - see this file's own test coverage for this exact edge case. The
// FINAL RESULT (as opposed to the traversal order that produces it) is
// always ascending-sorted and always includes `seedIndex` itself whenever
// the result is non-empty, so it is fully deterministic and reproducible
// regardless of adjacency-list ordering or traversal strategy. Returns an
// empty result if `seedIndex` is out of range for `adjacency`.
std::vector<std::int32_t> SelectRigidBodyBranch(
    const std::vector<std::vector<std::int32_t>>& adjacency, std::int32_t seedIndex);

} // namespace gte
```

### 3.2 New file: `src/Editor/RigidBodyGroupSelection.cpp`

```cpp
#include "RigidBodyGroupSelection.h"

#include <algorithm>

namespace gte {

std::vector<std::vector<std::int32_t>> BuildRigidBodyAdjacency(
    const std::vector<RigidBodyJointEdge>& edges, std::size_t rigidBodyCount)
{
    std::vector<std::vector<std::int32_t>> adjacency(rigidBodyCount);

    for (const RigidBodyJointEdge& edge : edges) {
        if (edge.rigidBodyAIndex < 0 || edge.rigidBodyBIndex < 0) {
            continue;
        }
        if (static_cast<std::size_t>(edge.rigidBodyAIndex) >= rigidBodyCount
            || static_cast<std::size_t>(edge.rigidBodyBIndex) >= rigidBodyCount) {
            continue;
        }
        if (edge.rigidBodyAIndex == edge.rigidBodyBIndex) {
            continue; // Self-joint - never a real connection.
        }

        std::vector<std::int32_t>& neighborsA = adjacency[static_cast<std::size_t>(edge.rigidBodyAIndex)];
        if (std::find(neighborsA.begin(), neighborsA.end(), edge.rigidBodyBIndex) == neighborsA.end()) {
            neighborsA.push_back(edge.rigidBodyBIndex);
        }

        std::vector<std::int32_t>& neighborsB = adjacency[static_cast<std::size_t>(edge.rigidBodyBIndex)];
        if (std::find(neighborsB.begin(), neighborsB.end(), edge.rigidBodyAIndex) == neighborsB.end()) {
            neighborsB.push_back(edge.rigidBodyAIndex);
        }
    }

    return adjacency;
}

std::vector<std::int32_t> SelectRigidBodiesByGroup(const std::vector<std::uint8_t>& groups, std::int32_t seedIndex)
{
    if (seedIndex < 0 || static_cast<std::size_t>(seedIndex) >= groups.size()) {
        return {};
    }

    const std::uint8_t targetGroup = groups[static_cast<std::size_t>(seedIndex)];
    std::vector<std::int32_t> result;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (groups[i] == targetGroup) {
            result.push_back(static_cast<std::int32_t>(i));
        }
    }
    return result; // Already ascending - built by iterating i in order.
}

std::vector<std::int32_t> SelectRigidBodyBranch(
    const std::vector<std::vector<std::int32_t>>& adjacency, std::int32_t seedIndex)
{
    if (seedIndex < 0 || static_cast<std::size_t>(seedIndex) >= adjacency.size()) {
        return {};
    }

    constexpr std::size_t kBranchDegree = 3;
    const auto isBranch = [&adjacency](std::int32_t node) {
        return adjacency[static_cast<std::size_t>(node)].size() >= kBranchDegree;
    };

    if (isBranch(seedIndex)) {
        // No single unambiguous chain to walk from a junction - see this
        // function's own doc comment in RigidBodyGroupSelection.h.
        return { seedIndex };
    }

    std::vector<char> visited(adjacency.size(), 0);
    std::vector<std::int32_t> result;
    std::vector<std::int32_t> pending{ seedIndex };
    visited[static_cast<std::size_t>(seedIndex)] = 1;

    while (!pending.empty()) {
        const std::int32_t current = pending.back();
        pending.pop_back();
        result.push_back(current);

        for (const std::int32_t neighbor : adjacency[static_cast<std::size_t>(current)]) {
            if (neighbor < 0 || static_cast<std::size_t>(neighbor) >= adjacency.size()) {
                continue; // Defensive - BuildRigidBodyAdjacency() never emits these, but never trust blindly.
            }
            if (visited[static_cast<std::size_t>(neighbor)]) {
                continue;
            }
            visited[static_cast<std::size_t>(neighbor)] = 1;
            if (isBranch(neighbor)) {
                // A branch boundary: never added to `result`, and never
                // expanded through - marking it visited still prevents
                // re-examining it from a different direction, without ever
                // walking PAST it.
                continue;
            }
            pending.push_back(neighbor);
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

} // namespace gte
```

### 3.3 `BoneViewerWindow.h` — `RigidBodyEntry` gains `group`

Replace (today, lines 137-144):

```cpp
    struct RigidBodyEntry {
        std::string name;
        Vec3 translate;
        Vec3 rotateRadians; // Euler, PMX convention (see PhysicsData.h) - used only for an approximate visual orientation hint.
        RigidBodyShape shape = RigidBodyShape::Sphere;
        Vec3 shapeSize;
        std::int32_t boneIndex = -1; // Index into m_bones this body is attached to (-1 if unattached) - drawn as a connecting line.
    };
```

with:

```cpp
    struct RigidBodyEntry {
        std::string name;
        Vec3 translate;
        Vec3 rotateRadians; // Euler, PMX convention (see PhysicsData.h) - used only for an approximate visual orientation hint.
        RigidBodyShape shape = RigidBodyShape::Sphere;
        Vec3 shapeSize;
        std::int32_t boneIndex = -1; // Index into m_bones this body is attached to (-1 if unattached) - drawn as a connecting line.
        // PMX collision group (0-15, PhysicsData.h's own RigidBody::group) -
        // added by task_manager/verlet-integration-4/
        // PHASE2_RIGID_BODY_GROUP_FIELD_AND_ADJACENCY_ALGORITHMS.md purely so
        // the Bone Viewer's "Select All (Group)" toolbar button
        // (PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md)
        // has something to compare against - never drawn/used for anything
        // else in this window.
        std::uint8_t group = 0;
    };
```

Also add, near the other private members holding per-load derived state
(today, right after `m_rootBoneIndices` at line 216):

```cpp
    // Per-rigid-body adjacency derived from every JointEntry's own
    // rigidBodyAIndex/rigidBodyBIndex above (m_rigidBodyAdjacency[i] lists
    // every OTHER rigid body directly joined to body i) - the graph the
    // Bone Viewer's "Select All (Branch)" toolbar button
    // (PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md) walks
    // via RigidBodyGroupSelection.h's SelectRigidBodyBranch(). Rebuilt once
    // per (re)load, right alongside m_rigidBodies/m_joints themselves - see
    // RebuildRigidBodyAdjacencyIndex().
    std::vector<std::vector<std::int32_t>> m_rigidBodyAdjacency;
```

Add the new include near the top (alongside `#include "../Assets/PhysicsData.h"`,
today line 003):

```cpp
#include "RigidBodyGroupSelection.h" // RigidBodyJointEdge, BuildRigidBodyAdjacency()
```

Declare the new private method near `RebuildBoneHierarchyIndex()` (today,
line 279):

```cpp
    // Rebuilds m_rigidBodyAdjacency from m_joints' own rigidBodyAIndex/
    // rigidBodyBIndex fields - called once right after m_joints itself is
    // (re)populated in EnsureDataLoaded(), mirroring
    // RebuildBoneHierarchyIndex()'s own "derive an index right after the
    // source data it's built from" convention.
    void RebuildRigidBodyAdjacencyIndex();
```

### 3.4 `BoneViewerWindow.cpp` — populate `group`, build the adjacency cache

In `EnsureDataLoaded()`, replace (today, lines 398-402):

```cpp
        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(
                RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape, body.shapeSize, body.boneIndex });
        }
```

with:

```cpp
        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape,
                body.shapeSize, body.boneIndex, body.group });
        }
```

Right after (today, line 409):

```cpp
    RebuildBoneHierarchyIndex(); // Still only walks m_bones - RigidBody/Joint have no tree to build.
```

add:

```cpp
    RebuildBoneHierarchyIndex(); // Still only walks m_bones - RigidBody/Joint have no tree to build.
    RebuildRigidBodyAdjacencyIndex(); // Derives the rigid-body-centric graph "Select All (Branch)" walks.
```

Add the new method definition, right after `RebuildBoneHierarchyIndex()`'s own
definition (today, lines 504-516):

```cpp
void BoneViewerWindow::RebuildRigidBodyAdjacencyIndex()
{
    std::vector<RigidBodyJointEdge> edges;
    edges.reserve(m_joints.size());
    for (const JointEntry& joint : m_joints) {
        edges.push_back(RigidBodyJointEdge{ joint.rigidBodyAIndex, joint.rigidBodyBIndex });
    }
    m_rigidBodyAdjacency = BuildRigidBodyAdjacency(edges, m_rigidBodies.size());
}
```

Finally, in `Reset()` (today, alongside the other per-load vectors cleared at
lines 152-156), add `m_rigidBodyAdjacency.clear();` right after
`m_rootBoneIndices.clear();` — same "every cached derived structure gets
wiped on Reset(), not just the raw entries" convention already applied there.

### 3.5 `CMakeLists.txt` — new source files

In the `GTE_ENABLE_PROJECT_PANEL` block (today, lines 490-509), add the two
new files alongside `RigidBodyWireframe.h/.cpp` (this module is gated the
same way — it exists purely to serve `BoneViewerWindow`, which is itself
gated behind `GTE_ENABLE_PROJECT_PANEL`):

```cmake
    if(GTE_ENABLE_PROJECT_PANEL)
        target_sources(gte_core PRIVATE
            src/Editor/ProjectPanelData.h
            src/Editor/ProjectPanelData.cpp
            src/Editor/Panels/ProjectPanel.h
            src/Editor/Panels/ProjectPanel.cpp
            src/Editor/AssetInspectorData.h
            src/Editor/AssetInspectorData.cpp
            src/Editor/AssetPreviewTexture.h
            src/Editor/AssetPreviewTexture.cpp
            src/Editor/AssetPreviewMesh.h
            src/Editor/AssetPreviewMesh.cpp
            src/Editor/ModelRigCache.h
            src/Editor/ModelRigCache.cpp
            src/Editor/RigidBodyWireframe.h
            src/Editor/RigidBodyWireframe.cpp
            src/Editor/RigidBodyGroupSelection.h
            src/Editor/RigidBodyGroupSelection.cpp
            src/Editor/BoneViewerWindow.h
            src/Editor/BoneViewerWindow.cpp
        )
    endif()
```

(Phase 3's own v2 revision adds two MORE files, `FlatListRangeSelection.h/.cpp`,
to this exact same block — see that phase document's own Step 3.7, which
shows the full block again with both phases' additions present together.)

### 3.6 New test file: `tests/Editor/RigidBodySelectionAlgorithmsTests.cpp`

```cpp
// Unit tests for RigidBodyGroupSelection (src/Editor/RigidBodyGroupSelection.h)
// - the pure "select all group"/"select all branch" algorithms behind the
// Bone Viewer's Rigid Body toolbar buttons (see AGENTS.md, "Testability and
// Regression Safety", and task_manager/verlet-integration-4/
// PHASE2_RIGID_BODY_GROUP_FIELD_AND_ADJACENCY_ALGORITHMS.md). Deliberately
// pure logic with no ImGui/SDL/Vulkan dependency at all - only compiled/
// linked when GTE_ENABLE_EDITOR AND GTE_ENABLE_PROJECT_PANEL are both ON,
// since RigidBodyGroupSelection itself is only ever compiled into gte_core
// then (see tests/CMakeLists.txt, alongside RigidBodyWireframeTests.cpp).

#include "Editor/RigidBodyGroupSelection.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(RigidBodyGroupSelectionTest, BuildAdjacencyProducesBidirectionalNeighborLists)
{
    const std::vector<RigidBodyJointEdge> edges{ { 0, 1 }, { 1, 2 } };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 3);

    ASSERT_EQ(adjacency.size(), 3u);
    EXPECT_EQ(adjacency[0], (std::vector<std::int32_t>{ 1 }));
    EXPECT_EQ(adjacency[1], (std::vector<std::int32_t>{ 0, 2 }));
    EXPECT_EQ(adjacency[2], (std::vector<std::int32_t>{ 1 }));
}

TEST(RigidBodyGroupSelectionTest, BuildAdjacencyDropsOutOfRangeAndSelfAndDuplicateEdges)
{
    const std::vector<RigidBodyJointEdge> edges{
        { 0, 1 }, { 0, 1 }, // Duplicate - must not double-list.
        { 5, 0 }, // Out of range (rigidBodyCount is 2) - dropped.
        { 1, 1 }, // Self-joint - dropped.
        { -1, 0 }, // Negative - dropped.
    };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 2);

    ASSERT_EQ(adjacency.size(), 2u);
    EXPECT_EQ(adjacency[0], (std::vector<std::int32_t>{ 1 }));
    EXPECT_EQ(adjacency[1], (std::vector<std::int32_t>{ 0 }));
}

TEST(RigidBodyGroupSelectionTest, BuildAdjacencyOnEmptyEdgesStillSizesToRigidBodyCount)
{
    const auto adjacency = BuildRigidBodyAdjacency({}, 4);
    ASSERT_EQ(adjacency.size(), 4u);
    for (const auto& neighbors : adjacency) {
        EXPECT_TRUE(neighbors.empty());
    }
}

TEST(RigidBodyGroupSelectionTest, SelectByGroupReturnsEverySameGroupIndexIncludingSeed)
{
    const std::vector<std::uint8_t> groups{ 0, 1, 1, 0, 1 };
    const auto result = SelectRigidBodiesByGroup(groups, /*seedIndex=*/2);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 1, 2, 4 }));
}

TEST(RigidBodyGroupSelectionTest, SelectByGroupWithUniqueGroupReturnsOnlyTheSeed)
{
    const std::vector<std::uint8_t> groups{ 0, 1, 2, 3 };
    const auto result = SelectRigidBodiesByGroup(groups, /*seedIndex=*/2);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 2 }));
}

TEST(RigidBodyGroupSelectionTest, SelectByGroupReturnsEmptyForOutOfRangeSeed)
{
    const std::vector<std::uint8_t> groups{ 0, 1 };
    EXPECT_TRUE(SelectRigidBodiesByGroup(groups, /*seedIndex=*/5).empty());
    EXPECT_TRUE(SelectRigidBodiesByGroup(groups, /*seedIndex=*/-1).empty());
}

// The story's own topology, transcribed to indices:
//
//   +--A--+
//   |     |
//
// Left '+' = index 0, right '+' = index 5. The 'A' segment between them is
// three interior bodies: 1, 2, 3 (0-1-2-3-5 forms the top chain). Each '+'
// ALSO connects downward to a further body (4 for the left one, 6 for the
// right one) that isn't part of 'A' at all - this third connection is
// exactly what makes each '+' a genuine branch (degree 3: one neighbor
// along 'A', one neighbor going down, per this test's own minimal
// topology - a real model would have a 4th connection completing a loop,
// but 3 is already sufficient to qualify as a branch per this module's own
// >= 3 rule).
TEST(RigidBodyGroupSelectionTest, SelectBranchMatchesTheStorysOwnDiagramExactly)
{
    // 0 = '+' (left junction): neighbors 1 (along A), 4 (down), 6 (extra third edge to prove degree 3).
    // 1,2,3 = 'A' interior chain.
    // 5 = '+' (right junction): neighbors 3 (along A), 7 (down), 8 (extra third edge).
    const std::vector<RigidBodyJointEdge> edges{
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 5 }, // The top chain, 0 through 5.
        { 0, 4 }, { 0, 6 }, // Left junction's other two connections.
        { 5, 7 }, { 5, 8 }, // Right junction's other two connections.
    };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 9);

    ASSERT_EQ(adjacency[0].size(), 3u); // Confirms index 0 really is a branch (degree 3).
    ASSERT_EQ(adjacency[5].size(), 3u); // Confirms index 5 really is a branch (degree 3).

    for (const std::int32_t seed : { 1, 2, 3 }) {
        const auto result = SelectRigidBodyBranch(adjacency, seed);
        EXPECT_EQ(result, (std::vector<std::int32_t>{ 1, 2, 3 })) << "seed was " << seed;
    }
}

TEST(RigidBodyGroupSelectionTest, SelectBranchFromABranchNodeItselfReturnsOnlyTheSeed)
{
    const std::vector<RigidBodyJointEdge> edges{ { 0, 1 }, { 0, 2 }, { 0, 3 } };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 4);

    const auto result = SelectRigidBodyBranch(adjacency, /*seedIndex=*/0);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 0 }));
}

TEST(RigidBodyGroupSelectionTest, SelectBranchOnASimpleClosedLoopWithNoBranchesSelectsTheWholeLoop)
{
    // A plain 4-body ring, every node degree 2 - no branches at all, so the
    // whole loop is one single chain/branch, whichever node you start from.
    const std::vector<RigidBodyJointEdge> edges{ { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 4);

    const auto result = SelectRigidBodyBranch(adjacency, /*seedIndex=*/1);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 0, 1, 2, 3 }));
}

TEST(RigidBodyGroupSelectionTest, SelectBranchOnAnOpenEndedChainSelectsEveryBodyToBothLeafEnds)
{
    // A plain straight chain with no branches - leaves at both ends
    // (degree 1), never mistaken for a branch (branch requires degree >= 3).
    const std::vector<RigidBodyJointEdge> edges{ { 0, 1 }, { 1, 2 }, { 2, 3 } };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 4);

    const auto result = SelectRigidBodyBranch(adjacency, /*seedIndex=*/2);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 0, 1, 2, 3 }));
}

TEST(RigidBodyGroupSelectionTest, SelectBranchWithNoJointsAtAllSelectsOnlyTheSeed)
{
    const auto adjacency = BuildRigidBodyAdjacency({}, 3);
    const auto result = SelectRigidBodyBranch(adjacency, /*seedIndex=*/1);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 1 }));
}

TEST(RigidBodyGroupSelectionTest, SelectBranchReturnsEmptyForOutOfRangeSeed)
{
    const auto adjacency = BuildRigidBodyAdjacency({}, 3);
    EXPECT_TRUE(SelectRigidBodyBranch(adjacency, /*seedIndex=*/9).empty());
    EXPECT_TRUE(SelectRigidBodyBranch(adjacency, /*seedIndex=*/-1).empty());
}

} // namespace
} // namespace gte
```

### 3.7 `tests/CMakeLists.txt` — register the new test file

In the `GTE_ENABLE_PROJECT_PANEL` nested block (today, lines 1221-1228), add
the new file alongside `RigidBodyWireframeTests.cpp`:

```cmake
    if(GTE_ENABLE_PROJECT_PANEL)
        list(APPEND GTE_TEST_SOURCES
            Editor/ProjectPanelDataTests.cpp
            Editor/AssetInspectorDataTests.cpp
            Editor/ModelRigCacheTests.cpp
            Editor/RigidBodyWireframeTests.cpp
            Editor/RigidBodySelectionAlgorithmsTests.cpp
        )
    endif()
```

(Phase 3's own v2 revision adds `Editor/FlatListRangeSelectionTests.cpp` to
this exact same list — see that phase document's own Step 3.8.)

## Step 4: What We Will NOT Do

- We will **not** make `SelectRigidBodyBranch()`'s branch-degree threshold
  (3) configurable — a fixed, documented constant matching the story's own
  "iterate ... until it find branch" wording exactly (a node with only 2
  connections is just a normal chain link, never a branch).
- We will **not** have `BuildRigidBodyAdjacency()`/`SelectRigidBodiesByGroup()`/
  `SelectRigidBodyBranch()` depend on `RigidBody`, `Joint`, `RigidBodyEntry`,
  or `JointEntry` directly — they take/return only plain
  `int32_t`/`uint8_t`/`RigidBodyJointEdge` values, exactly like
  `RigidBodyWireframe.h`'s own functions take only plain `Vec3`/enum
  parameters rather than a `RigidBody` reference — keeping this module
  reusable and trivially Tier-1-testable with no engine-type dependencies
  at all.
- We will **not** touch `Selection.h/.cpp` in this phase (that is Phase 1,
  already complete by the time this phase's own button-wiring — Phase 3 —
  needs it) or `BoneViewerWindow.cpp`'s toolbar/click-handling code (that is
  Phase 3) — this phase is scoped to data model + pure algorithms only.
- We will **not** attempt to detect/report cycles or malformed joint data as
  an error — `BuildRigidBodyAdjacency()`/`SelectRigidBodyBranch()` both
  already tolerate cycles/duplicate edges/self-joints gracefully (see 3.6's
  own test coverage), matching this codebase's existing "defensive, never
  crash on malformed imported data" convention (e.g.
  `BoneMatchesFilterRecursive()`'s own depth-guard against a cyclic
  `parentIndex` chain).
- We will **not** (v2) commit to a specific traversal order (BFS/DFS) as part
  of `SelectRigidBodyBranch()`'s contract — only the final, sorted RESULT is
  guaranteed; see this document's own v2 revision note above.

## Step 5: Their Role

Implementer checklist for this phase:

1. Add `src/Editor/RigidBodyGroupSelection.h/.cpp` exactly per 3.1/3.2.
2. Apply 3.3/3.4 to `BoneViewerWindow.h/.cpp`.
3. Apply 3.5 to the root `CMakeLists.txt`.
4. Add `tests/Editor/RigidBodySelectionAlgorithmsTests.cpp` exactly per 3.6,
   and register it per 3.7.
5. Build `gte_core` + `GreatTamanaEngineTests` (`GTE_ENABLE_EDITOR=ON` AND
   `GTE_ENABLE_PROJECT_PANEL=ON`) and confirm every
   `RigidBodyGroupSelectionTest.*` case passes, plus every pre-existing test
   (especially `RigidBodyWireframeTest.*`/`ModelRigCacheTest.*`, which share
   this same CMake gating block) still passes unchanged.
6. Confirm `BoneViewerWindow.cpp` still compiles and the Bone Viewer still
   opens/renders exactly as before against a real jiggle-bone/skirt MMD
   model (this phase adds data plumbing only — no visible behavior change
   yet, since nothing calls the new algorithms until Phase 3).
