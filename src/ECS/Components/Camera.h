#pragma once

#include "../../Math/Mat4.h"
#include "../../Math/MathTypes.h"
#include "Transform.h"

namespace gte {

// The engine's third real component (see AGENTS.md, "Entity-Component-
// System", and Transform.h/MeshRenderer.h for the first two). Plain data
// only, same spirit as both of those - perspective-only for now (no
// orthographic mode yet, no separate "camera-only" transform fields): an
// entity that wants a camera adds a Transform (for eye position/
// orientation - see ViewMatrix() below, which falls back to an identity
// Transform if the camera entity happens not to have one) plus this
// Camera component.
//
// RenderSystem (src/Game/RenderSystem.h) is what actually resolves the
// first ACTIVE Camera in a Registry into a combined view-projection matrix
// each frame (RenderSystem::ResolveActiveCameraViewProjection(), Tier-1-
// testable exactly like CollectRenderables() - see
// tests/Game/RenderSystemTests.cpp) - this struct alone never touches
// Renderer/Vulkan.
struct Camera {
    // Vertical field of view, in DEGREES (human-facing edge of the API,
    // same "degrees at the edges, radians internally" convention as
    // Quat::FromEulerDegrees/ToEulerDegrees - see MathTypes.h). Converted to
    // radians only inside ProjectionMatrix() below.
    float fovYDegrees = 60.0f;
    float nearZ = 0.1f;
    float farZ = 1000.0f;

    // Only one camera is ever "the" active one a render target draws
    // through in a given frame - RenderSystem picks the first entity (in
    // ComponentStorage<Camera> order) with active == true it finds, and
    // ignores every other Camera in the Registry that frame. A future
    // multi-camera/render-layer system is a natural follow-up once there's
    // an actual need for more than one simultaneously-active camera.
    bool active = true;

    // Perspective projection matrix for a render target of the given
    // aspect ratio (width / height) - deliberately NOT baked into the
    // component itself, since the SAME Camera can render into render
    // targets of different sizes/aspects in the same frame (e.g. the
    // Editor's "Game" and "Scene" panels, each with their own RenderTexture
    // - see AGENTS.md, "Editor Module Structure"). flipY is always true:
    // matches this engine's Vulkan clip-space Y convention (see
    // Mat4::PerspectiveFovLH_ZO's own flipY comment).
    Mat4 ProjectionMatrix(float aspectWidthOverHeight) const noexcept
    {
        return Mat4::PerspectiveFovLH_ZO(DegToRad(fovYDegrees), aspectWidthOverHeight, nearZ, farZ, /*flipY=*/true);
    }

    // View matrix from a Transform's world position/rotation, via
    // Mat4::LookAtLH(): looks down the transform's rotated
    // Vec3::Forward(), with the transform's rotated Vec3::Up() as the up
    // vector - i.e. this camera's Transform is treated exactly like any
    // other entity's (TRS-composable, editable in the Inspector), not a
    // bespoke "eye/target/up" triple. Scale is deliberately ignored (a
    // camera's Transform.scale should always stay Vec3::One(); LookAtLH
    // only ever needs position/rotation, unlike Mat4::TRS()).
    static Mat4 ViewMatrix(const Transform& transform) noexcept
    {
        const Vec3 eye = transform.position;
        const Vec3 forward = transform.rotation.RotateVector(Vec3::Forward());
        const Vec3 up = transform.rotation.RotateVector(Vec3::Up());
        return Mat4::LookAtLH(eye, eye + forward, up);
    }
};

} // namespace gte
