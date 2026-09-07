# PHASE1 — Rigid Body Wireframe Geometry Module (`src/Editor/RigidBodyWireframe.h/.cpp`)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit — no code anywhere turns a
`RigidBody`'s shape/size/rotation into real 3D geometry). Depends on:
nothing new — only already-existing `src/Assets/PhysicsData.h`
(`RigidBodyShape`), `src/Math/Vec3.h`, `src/Math/Quat.h`,
`src/Math/MathTypes.h` (`kEpsilon`/`DegToRad`/`RadToDeg`). Produces: a new,
pure, Tier-1-tested function, `BuildRigidBodyWireframe()`, that Phase 2 will
call from `BoneViewerWindow::Build()`.

## Step 1: The Goal

Add one new small module that answers exactly one question, correctly, for
all three `RigidBodyShape` values: **"what are the actual 3D line segments
that trace this rigid body's real outline, positioned and oriented exactly
like the real physics body?"** — so Phase 2 never has to touch shape/size/
rotation math itself, only draw whatever this function returns.

Concretely, `BuildRigidBodyWireframe(RigidBodyShape shape, const Vec3&
shapeSize, const Vec3& translate, const Vec3& rotateRadians)` must return:

- **`Sphere`** — 3 mutually-perpendicular great circles (the classic "wire
  sphere" look), radius `shapeSize.x`, centered at `translate`.
- **`Box`** — the 12 edges of an oriented box, half-extents
  `shapeSize.x/y/z`, centered at `translate`, rotated by `rotateRadians`.
- **`Capsule`** — 2 circular rings (the cylindrical body's silhouette) + 4
  straight lines connecting them (the cylinder's "sides") + 4 hemisphere
  arcs (2 per end cap) — radius `shapeSize.x`, cylinder length `shapeSize.y`,
  centered at `translate`, rotated by `rotateRadians`, capsule axis along
  local +Y (matching `PhysicsData.h`'s own doc comment: *"Capsule uses x
  (radius) and y (height)"*).

Every returned segment's two endpoints are already in the SAME model-local
space as `RigidBody::translate`/`MeshData::positions` — fully positioned and
oriented, ready for a caller to project through a `viewProj` matrix and draw
directly, with zero further transform needed.

## Step 2: The Situation / The Problem

See `PHASE0_MASTER_STRATEGY.md`'s Culprit #2/#3 for the full detail: no such
function exists anywhere in the engine today, and the correct Euler-angle
convention for `rotateRadians` (PMX's own `Ry * Rx * Rz` order) is already
established elsewhere (saba's `MMDPhysics.cpp`) and already matches this
engine's own `Quat::FromEulerDegrees(pitchX, yawY, rollZ)` composition order
exactly — this phase must call that existing function correctly, not
reinvent rotation-order math.

## Step 3: The Plan

### 3.1 New file `src/Editor/RigidBodyWireframe.h`

```cpp
#pragma once

#include "../Assets/PhysicsData.h" // RigidBodyShape
#include "../Math/Vec3.h"

#include <vector>

namespace gte {

// One straight-line segment of a rigid body's wireframe outline, already
// fully positioned/oriented in the SAME model-local space as
// RigidBody::translate/MeshData::positions (see PhysicsData.h) - ready to
// be projected through a viewProj matrix and drawn directly (e.g. via
// ImDrawList::AddLine on each endpoint's projected screen position), with
// no further transform needed by the caller.
struct WireframeSegment {
    Vec3 a;
    Vec3 b;
};

// Builds a wireframe outline for one rigid body - a small set of straight
// LINE SEGMENTS that, drawn together, trace the ACTUAL silhouette of
// `shape` at `shapeSize`, positioned/oriented by `translate`/
// `rotateRadians` (Euler angles, radians, PMX convention - see
// PhysicsData.h's own RigidBody::rotateRadians doc comment). This is
// honest, per-shape geometry - a Sphere/Box/Capsule each produce a visually
// distinct, correctly-proportioned outline - NOT the flat, shape-blind
// screen-space circle approximation this replaces (see
// task_manager/verlet-integration-3/PHASE0_MASTER_STRATEGY.md's Culprit).
//
// `rotateRadians` is applied via Quat::FromEulerDegrees(RadToDeg(x),
// RadToDeg(y), RadToDeg(z)) - Quat::FromEulerDegrees's own documented
// Yaw(Y)*Pitch(X)*Roll(Z) composition order is ALREADY the exact same
// order PMX's own rigid-body rotation uses (verified directly against
// third_party/saba/src/Saba/Model/MMD/MMDPhysics.cpp's own
// `rotMat = ry * rx * rz` - see PHASE0_MASTER_STRATEGY.md, Culprit #3) -
// do not change this to a different Euler order.
//
// Degenerate/non-positive size components mean "nothing meaningful to
// draw for this shape" and return an EMPTY vector, never NaN/zero-length/
// garbage segments:
//   - Sphere: shapeSize.x (radius) <= 0.
//   - Box: ALL of shapeSize.x/y/z (half-extents) <= 0 (a box degenerate on
//     only one or two axes still draws its remaining, genuinely flat
//     outline - a flattened box is still a meaningful shape to see).
//   - Capsule: shapeSize.x (radius) <= 0. A non-positive shapeSize.y
//     (cylinder length) is treated as 0 (a "pure sphere" capsule, i.e. two
//     coincident hemispheres) rather than emptying the whole result.
std::vector<WireframeSegment> BuildRigidBodyWireframe(
    RigidBodyShape shape, const Vec3& shapeSize, const Vec3& translate, const Vec3& rotateRadians);

} // namespace gte
```

### 3.2 New file `src/Editor/RigidBodyWireframe.cpp`

```cpp
#include "RigidBodyWireframe.h"

#include "../Math/MathTypes.h" // kEpsilon, DegToRad, RadToDeg
#include "../Math/Quat.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

// Matches ImGui-overlay-quality roundness at the small on-screen sizes this
// window typically draws these at - not meant to be a configurable knob.
constexpr int kCircleSegments = 24;
constexpr int kHemisphereArcSegments = 16;

// PMX's own rigid-body rotation order - see this file's header comment and
// PHASE0_MASTER_STRATEGY.md's Culprit #3 for why this exact call is correct
// and must not be "simplified" to a different Euler order.
Quat RotationFromPmxEuler(const Vec3& rotateRadians) noexcept
{
    return Quat::FromEulerDegrees(
        RadToDeg(rotateRadians.x), RadToDeg(rotateRadians.y), RadToDeg(rotateRadians.z));
}

Vec3 ToWorld(const Vec3& local, const Quat& rotation, const Vec3& translate) noexcept
{
    return rotation.RotateVector(local) + translate;
}

// Appends one closed loop of `segmentCount` points (localPointAt(i) for i
// in [0, segmentCount)) as `segmentCount` connected line segments (last
// point back to the first) - shared by every circle this file draws
// (sphere's 3 great circles, capsule's 2 rings).
template <typename LocalPointFn>
void AppendClosedLoop(std::vector<WireframeSegment>& out, int segmentCount, const Quat& rotation,
    const Vec3& translate, LocalPointFn localPointAt)
{
    Vec3 previous = ToWorld(localPointAt(0), rotation, translate);
    const Vec3 first = previous;
    for (int i = 1; i <= segmentCount; ++i) {
        const Vec3 current = i == segmentCount ? first : ToWorld(localPointAt(i), rotation, translate);
        out.push_back(WireframeSegment{ previous, current });
        previous = current;
    }
}

// Appends one OPEN arc of `segmentCount` segments (segmentCount + 1 sampled
// points, localPointAt(i) for i in [0, segmentCount]) - shared by every
// hemisphere arc this file draws (capsule's 4 end-cap arcs).
template <typename LocalPointFn>
void AppendOpenArc(std::vector<WireframeSegment>& out, int segmentCount, const Quat& rotation, const Vec3& translate,
    LocalPointFn localPointAt)
{
    Vec3 previous = ToWorld(localPointAt(0), rotation, translate);
    for (int i = 1; i <= segmentCount; ++i) {
        const Vec3 current = ToWorld(localPointAt(i), rotation, translate);
        out.push_back(WireframeSegment{ previous, current });
        previous = current;
    }
}

std::vector<WireframeSegment> BuildSphereWireframe(const Vec3& shapeSize, const Vec3& translate, const Quat& rotation)
{
    const float radius = shapeSize.x;
    if (radius <= kEpsilon) {
        return {};
    }

    std::vector<WireframeSegment> segments;
    segments.reserve(static_cast<std::size_t>(kCircleSegments) * 3);

    // Three great circles, one per cardinal plane - together they read as a
    // "wire sphere" from any viewing angle, and (since a sphere is
    // rotationally symmetric) rotation only changes WHICH three great
    // circles are drawn, never the overall silhouette - still applied for
    // consistency/correctness with the other two shapes.
    const auto xyCircle = [radius](int i) {
        const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(kCircleSegments);
        return Vec3(std::cos(t) * radius, std::sin(t) * radius, 0.0f);
    };
    const auto yzCircle = [radius](int i) {
        const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(kCircleSegments);
        return Vec3(0.0f, std::cos(t) * radius, std::sin(t) * radius);
    };
    const auto xzCircle = [radius](int i) {
        const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(kCircleSegments);
        return Vec3(std::cos(t) * radius, 0.0f, std::sin(t) * radius);
    };

    AppendClosedLoop(segments, kCircleSegments, rotation, translate, xyCircle);
    AppendClosedLoop(segments, kCircleSegments, rotation, translate, yzCircle);
    AppendClosedLoop(segments, kCircleSegments, rotation, translate, xzCircle);
    return segments;
}

std::vector<WireframeSegment> BuildBoxWireframe(const Vec3& shapeSize, const Vec3& translate, const Quat& rotation)
{
    const float hx = std::max(0.0f, shapeSize.x);
    const float hy = std::max(0.0f, shapeSize.y);
    const float hz = std::max(0.0f, shapeSize.z);
    if (hx <= kEpsilon && hy <= kEpsilon && hz <= kEpsilon) {
        return {}; // Fully degenerate on every axis - nothing meaningful to draw.
    }

    // 8 corners, indexed by 3 bits (bit0 = +/-X, bit1 = +/-Y, bit2 = +/-Z).
    Vec3 localCorners[8];
    for (int i = 0; i < 8; ++i) {
        localCorners[i] = Vec3((i & 1) ? hx : -hx, (i & 2) ? hy : -hy, (i & 4) ? hz : -hz);
    }
    Vec3 worldCorners[8];
    for (int i = 0; i < 8; ++i) {
        worldCorners[i] = ToWorld(localCorners[i], rotation, translate);
    }

    // 12 edges: every pair of corners whose indices differ in EXACTLY one
    // bit (i.e. differ along exactly one axis) - the standard box edge
    // adjacency, derived directly from the 3-bit corner indexing above
    // rather than a hand-written literal edge list, so it can never drift
    // out of sync with localCorners' own indexing convention.
    std::vector<WireframeSegment> segments;
    segments.reserve(12);
    for (int i = 0; i < 8; ++i) {
        for (int bit = 0; bit < 3; ++bit) {
            const int j = i ^ (1 << bit);
            if (j > i) { // Each edge counted once (i < j).
                segments.push_back(WireframeSegment{ worldCorners[i], worldCorners[j] });
            }
        }
    }
    return segments;
}

std::vector<WireframeSegment> BuildCapsuleWireframe(const Vec3& shapeSize, const Vec3& translate, const Quat& rotation)
{
    const float radius = shapeSize.x;
    if (radius <= kEpsilon) {
        return {};
    }
    const float halfHeight = std::max(0.0f, shapeSize.y) * 0.5f; // Capsule axis = local +Y (see PhysicsData.h).

    std::vector<WireframeSegment> segments;
    segments.reserve(static_cast<std::size_t>(kCircleSegments) * 2 + 4
        + static_cast<std::size_t>(kHemisphereArcSegments) * 4);

    // Two rings (the cylindrical body's top/bottom silhouette), in the
    // local XZ plane, at y = +/-halfHeight.
    const auto ringAt = [radius](float y) {
        return [radius, y](int i) {
            const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(kCircleSegments);
            return Vec3(std::cos(t) * radius, y, std::sin(t) * radius);
        };
    };
    AppendClosedLoop(segments, kCircleSegments, rotation, translate, ringAt(halfHeight));
    AppendClosedLoop(segments, kCircleSegments, rotation, translate, ringAt(-halfHeight));

    // Four straight lines connecting the two rings (the cylinder's
    // "sides"), at 0/90/180/270 degrees around the ring.
    for (int i = 0; i < 4; ++i) {
        const float t = (kHalfPi * static_cast<float>(i));
        const Vec3 top = ToWorld(Vec3(std::cos(t) * radius, halfHeight, std::sin(t) * radius), rotation, translate);
        const Vec3 bottom =
            ToWorld(Vec3(std::cos(t) * radius, -halfHeight, std::sin(t) * radius), rotation, translate);
        segments.push_back(WireframeSegment{ top, bottom });
    }

    // Four hemisphere arcs - 2 per end cap (one in the local XY plane, one
    // in the local ZY plane), each a half-circle from the ring's edge up/
    // down to the pole - the standard "wire capsule" cap treatment.
    const auto topXyArc = [radius, halfHeight](int i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(kHemisphereArcSegments);
        return Vec3(std::cos(t) * radius, halfHeight + std::sin(t) * radius, 0.0f);
    };
    const auto topZyArc = [radius, halfHeight](int i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(kHemisphereArcSegments);
        return Vec3(0.0f, halfHeight + std::sin(t) * radius, std::cos(t) * radius);
    };
    const auto bottomXyArc = [radius, halfHeight](int i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(kHemisphereArcSegments);
        return Vec3(std::cos(t) * radius, -halfHeight - std::sin(t) * radius, 0.0f);
    };
    const auto bottomZyArc = [radius, halfHeight](int i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(kHemisphereArcSegments);
        return Vec3(0.0f, -halfHeight - std::sin(t) * radius, std::cos(t) * radius);
    };
    AppendOpenArc(segments, kHemisphereArcSegments, rotation, translate, topXyArc);
    AppendOpenArc(segments, kHemisphereArcSegments, rotation, translate, topZyArc);
    AppendOpenArc(segments, kHemisphereArcSegments, rotation, translate, bottomXyArc);
    AppendOpenArc(segments, kHemisphereArcSegments, rotation, translate, bottomZyArc);

    return segments;
}

} // namespace

std::vector<WireframeSegment> BuildRigidBodyWireframe(
    RigidBodyShape shape, const Vec3& shapeSize, const Vec3& translate, const Vec3& rotateRadians)
{
    const Quat rotation = RotationFromPmxEuler(rotateRadians);
    switch (shape) {
    case RigidBodyShape::Sphere:
        return BuildSphereWireframe(shapeSize, translate, rotation);
    case RigidBodyShape::Box:
        return BuildBoxWireframe(shapeSize, translate, rotation);
    case RigidBodyShape::Capsule:
        return BuildCapsuleWireframe(shapeSize, translate, rotation);
    }
    return {};
}

} // namespace gte
```

Note `kPi`/`kTwoPi`/`kHalfPi` come from `src/Math/MathTypes.h` (already
included transitively via `Quat.h`/`Vec3.h`, but include
`"../Math/MathTypes.h"` directly rather than relying on transitive includes,
matching this codebase's existing convention elsewhere).

### 3.3 `CMakeLists.txt` (root) — add the two new files

Add `src/Editor/RigidBodyWireframe.h` and `src/Editor/RigidBodyWireframe.cpp`
to the SAME `if(GTE_ENABLE_PROJECT_PANEL)` `target_sources(gte_core PRIVATE
...)` block that already lists `ModelRigCache.h/.cpp`/
`BoneViewerWindow.h/.cpp` (lines 490-507 today) — right alongside them,
since this module exists purely to serve `BoneViewerWindow`, which is only
ever compiled under that same switch:

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
        src/Editor/BoneViewerWindow.h
        src/Editor/BoneViewerWindow.cpp
    )
endif()
```

### 3.4 New test file `tests/Editor/RigidBodyWireframeTests.cpp`

Mirrors `tests/Editor/SelectionTests.cpp`/`ModelRigCacheTests.cpp`'s own
plain-GoogleTest, no-fixture style:

```cpp
// Unit tests for RigidBodyWireframe (src/Editor/RigidBodyWireframe.h) - the
// pure, per-shape wireframe geometry builder behind the Bone Viewer's
// Rigid Body gizmo (see AGENTS.md, "Testability & Regression Safety", and
// task_manager/verlet-integration-3/PHASE1_RIGID_BODY_WIREFRAME_GEOMETRY_MODULE.md).
// Deliberately pure logic with no ImGui/SDL/Vulkan dependency at all - only
// compiled/linked when GTE_ENABLE_EDITOR AND GTE_ENABLE_PROJECT_PANEL are
// both ON, since RigidBodyWireframe itself is only ever compiled into
// gte_core then (see tests/CMakeLists.txt).

#include "Editor/RigidBodyWireframe.h"

#include "Math/MathTypes.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

constexpr int kCircleSegments = 24;
constexpr int kHemisphereArcSegments = 16;

float DistanceFromAxis(const Vec3& point, const Vec3& axisOrigin, const Vec3& axisDirection)
{
    // Perpendicular distance from `point` to the infinite line through
    // axisOrigin along axisDirection (assumed already normalized) - used to
    // check a capsule ring's points all lie at exactly `radius` from the
    // capsule's own long axis.
    const Vec3 toPoint = point - axisOrigin;
    const float along = Dot(toPoint, axisDirection);
    const Vec3 closestOnAxis = axisOrigin + axisDirection * along;
    return Length(point - closestOnAxis);
}

TEST(RigidBodyWireframeTest, SphereIsEmptyForNonPositiveRadius)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Sphere, Vec3(0.0f, 0.0f, 0.0f), Vec3::Zero(), Vec3::Zero());
    EXPECT_TRUE(segments.empty());
}

TEST(RigidBodyWireframeTest, SphereHasThreeCirclesEveryPointAtExactRadiusFromTranslate)
{
    const Vec3 translate(5.0f, 1.0f, -3.0f);
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Sphere, Vec3(2.0f, 0.0f, 0.0f), translate, Vec3::Zero());

    ASSERT_EQ(segments.size(), static_cast<std::size_t>(kCircleSegments) * 3);
    for (const WireframeSegment& seg : segments) {
        EXPECT_NEAR(Length(seg.a - translate), 2.0f, 0.001f);
        EXPECT_NEAR(Length(seg.b - translate), 2.0f, 0.001f);
    }
}

TEST(RigidBodyWireframeTest, BoxIsEmptyWhenAllHalfExtentsNonPositive)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Box, Vec3(0.0f, 0.0f, 0.0f), Vec3::Zero(), Vec3::Zero());
    EXPECT_TRUE(segments.empty());
}

TEST(RigidBodyWireframeTest, BoxHasTwelveEdgesWithLengthsMatchingHalfExtentsWhenUnrotated)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Box, Vec3(1.0f, 2.0f, 3.0f), Vec3::Zero(), Vec3::Zero());

    ASSERT_EQ(segments.size(), 12u);
    int countNearX = 0, countNearY = 0, countNearZ = 0; // Edge lengths 2, 4, 6.
    for (const WireframeSegment& seg : segments) {
        const float len = Length(seg.b - seg.a);
        if (std::fabs(len - 2.0f) < 0.001f) ++countNearX;
        else if (std::fabs(len - 4.0f) < 0.001f) ++countNearY;
        else if (std::fabs(len - 6.0f) < 0.001f) ++countNearZ;
    }
    // 4 edges run along each of the 3 axes.
    EXPECT_EQ(countNearX, 4);
    EXPECT_EQ(countNearY, 4);
    EXPECT_EQ(countNearZ, 4);
}

TEST(RigidBodyWireframeTest, BoxAppliesRotationAndTranslation)
{
    // A 90-degree yaw (around Y) should swap the box's own X/Z half-extent
    // directions in world space - verified by checking every corner's
    // distance from `translate` still equals the same diagonal length
    // (rotation preserves distances), and that at least one corner now
    // sits along world +/-Z rather than +/-X for a non-cubic box.
    const Vec3 translate(10.0f, 0.0f, 0.0f);
    const auto segments = BuildRigidBodyWireframe(
        RigidBodyShape::Box, Vec3(1.0f, 1.0f, 3.0f), translate, Vec3(0.0f, DegToRad(90.0f), 0.0f));

    ASSERT_EQ(segments.size(), 12u);
    const float expectedDiagonal = Length(Vec3(1.0f, 1.0f, 3.0f));
    bool foundCornerNearWorldZAxis = false;
    for (const WireframeSegment& seg : segments) {
        EXPECT_NEAR(Length(seg.a - translate), expectedDiagonal, 0.01f);
        if (std::fabs(seg.a.x - translate.x) < 0.01f && std::fabs(std::fabs(seg.a.z) - 1.0f) < 0.01f) {
            foundCornerNearWorldZAxis = true;
        }
    }
    EXPECT_TRUE(foundCornerNearWorldZAxis);
}

TEST(RigidBodyWireframeTest, CapsuleIsEmptyForNonPositiveRadius)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Capsule, Vec3(0.0f, 2.0f, 0.0f), Vec3::Zero(), Vec3::Zero());
    EXPECT_TRUE(segments.empty());
}

TEST(RigidBodyWireframeTest, CapsuleHasExpectedSegmentCount)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Capsule, Vec3(1.0f, 2.0f, 0.0f), Vec3::Zero(), Vec3::Zero());

    const std::size_t expected = static_cast<std::size_t>(kCircleSegments) * 2 // two rings
        + 4 // four verticals
        + static_cast<std::size_t>(kHemisphereArcSegments) * 4; // four hemisphere arcs
    EXPECT_EQ(segments.size(), expected);
}

TEST(RigidBodyWireframeTest, CapsuleRingPointsAreAtCorrectRadiusFromLocalAxisWhenUnrotated)
{
    const Vec3 translate(0.0f, 0.0f, 0.0f);
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Capsule, Vec3(1.5f, 4.0f, 0.0f), translate, Vec3::Zero());

    ASSERT_FALSE(segments.empty());
    // Every ring/vertical/arc point must be within radius 1.5 (rings/
    // verticals exactly at 1.5 from the local Y axis; hemisphere arc points
    // trace the same radius around their own cap center) of SOME point on
    // the local Y axis - a loose but meaningful bound checking nothing
    // drifted arbitrarily far from the capsule's own axis.
    for (const WireframeSegment& seg : segments) {
        EXPECT_LE(DistanceFromAxis(seg.a, translate, Vec3::Up()), 1.5f + 0.01f);
        EXPECT_LE(DistanceFromAxis(seg.b, translate, Vec3::Up()), 1.5f + 0.01f);
    }
}

} // namespace
} // namespace gte
```

### 3.5 `tests/CMakeLists.txt` — register the new test file

Add `Editor/RigidBodyWireframeTests.cpp` to the SAME
`if(GTE_ENABLE_PROJECT_PANEL)` block that already lists
`Editor/ModelRigCacheTests.cpp` (lines 1221-1226 today):

```cmake
    if(GTE_ENABLE_PROJECT_PANEL)
        list(APPEND GTE_TEST_SOURCES
            Editor/ProjectPanelDataTests.cpp
            Editor/AssetInspectorDataTests.cpp
            Editor/ModelRigCacheTests.cpp
            Editor/RigidBodyWireframeTests.cpp
        )
    endif()
```

## Step 4: What We Will NOT Do

- We will **not** expose `BuildSphereWireframe()`/`BuildBoxWireframe()`/
  `BuildCapsuleWireframe()` individually in the header — only the single
  dispatching `BuildRigidBodyWireframe()` is public API; the three
  per-shape builders stay `static`/anonymous-namespace implementation
  details, exactly like `BoneViewerWindow.cpp`'s own existing
  `ProjectToScreen()`/`ToLower()` helpers.
- We will **not** make the segment/arc resolution (`kCircleSegments`/
  `kHemisphereArcSegments`) a runtime-configurable parameter — fixed
  constants are sufficient for a debug gizmo; do not add an ImGui slider
  for "wireframe quality".
- We will **not** attempt to model PMX's `RigidBodyMotionType` (Static/
  Dynamic/DynamicAndBoneMerge) in this module at all — the wireframe is
  purely a REST-POSE shape visualization, identical regardless of motion
  type; that field stays untouched, unread by this module.
- We will **not** touch `BoneViewerWindow.h/.cpp` in this phase — Phase 2
  owns wiring this function into the viewport overlay.

## Step 5: Their Role

Implementer checklist for this phase:

1. Add `src/Editor/RigidBodyWireframe.h` and `.cpp` per 3.1/3.2.
2. Edit the root `CMakeLists.txt` per 3.3 (two new source files, same
   `GTE_ENABLE_PROJECT_PANEL` block as `ModelRigCache`/`BoneViewerWindow`).
3. Add `tests/Editor/RigidBodyWireframeTests.cpp` per 3.4.
4. Edit `tests/CMakeLists.txt` per 3.5 (register the new test file, same
   `GTE_ENABLE_PROJECT_PANEL` block as `ModelRigCacheTests.cpp`).
5. Build `GreatTamanaEngineTests` and confirm every
   `RigidBodyWireframeTest.*` case passes, alongside the full existing
   suite (no regression in any other test).
